#include "autoskip/AutoSkipEngine.h"

#include <objbase.h>  // CoInitializeEx / CoUninitialize

#include <algorithm>
#include <chrono>
#include <cmath>

#include "capture/WindowFinder.h"
#include "util/Log.h"
#include "util/StringUtil.h"

namespace bys {

namespace {

constexpr double kTalkUiThreshold = 0.75;      // 对话按钮模板匹配阈值
constexpr double kOptionThreshold = 0.70;      // 选项气泡模板匹配阈值
constexpr double kPageCloseThreshold = 0.80;   // 右上角关闭按钮阈值

constexpr int kOptionClickCooldownMs = 800;
constexpr int kTriangleClickCooldownMs = 1000;
constexpr int kBlackClickCooldownMs = 1200;
constexpr int kPopupCloseCooldownMs = 800;
constexpr int kPlayingGraceSeconds = 10;       // "剧情仍在进行"的宽限时间

// 游戏不在前台（或未就绪）时的轮询间隔。
// 此时不做任何检测，只是低频地看一眼焦点是否回来了，几乎不占 CPU。
constexpr int kIdleIntervalMs = 250;

// 底部"继续对话"指示器（金色菱形外框 + 内部实心倒三角）的 HSV 掩码。
//
// 实测数据来自真实游戏截图（1080p）：
//   指示器：  H 12~21, S 140~180, V 160~200
//   背景肤色：H  6~15, S  80~120, V  80~140
// 两者色相接近，但饱和度/亮度分得很开，所以用 S>=135 且 V>=155 就能干净分离。
//
// BetterGI 原始范围是 H∈[0,25]、S>=240、V>=229 —— 实测 S/V 都差得远，完全匹配不上，
// 这就是"识别不到底部三角"的根因。
constexpr int kTriYellowHLo = 5;
constexpr int kTriYellowSLo = 135;
constexpr int kTriYellowVLo = 155;
constexpr int kTriYellowHHi = 40;
constexpr int kTriYellowSHi = 255;
constexpr int kTriYellowVHi = 255;

// 指示器在 1080p 下的中心与尺寸（由真实截图测得）
constexpr int kIndicatorCenterX1080 = 956;
constexpr int kIndicatorCenterY1080 = 1045;
constexpr int kIndicatorSearchHalfW1080 = 50;
constexpr int kIndicatorSearchHalfH1080 = 45;

// 1080p 下指示器约有 270 个金色像素
constexpr double kIndicatorPixels1080 = 270.0;

// 判定阈值：金色像素数低于该比例视为不存在
constexpr double kIndicatorMinRatio = 0.35;

// 包围盒上限，防止把大片同色区域误判成指示器（指示器本体约 24x30 像素）
constexpr double kIndicatorMaxBox1080 = 45.0;

}  // namespace

AutoSkipEngine::AutoSkipEngine() {
    fpsWindowStart_ = std::chrono::steady_clock::now();
}

AutoSkipEngine::~AutoSkipEngine() {
    Stop();
    CleanupCapture();
}

bool AutoSkipEngine::IsCooldownElapsed(const std::chrono::steady_clock::time_point& last, int ms) {
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - last)
                       .count();
    return elapsed >= ms;
}

void AutoSkipEngine::Start() {
    if (running_.exchange(true)) {
        return;
    }

    thread_ = std::thread([this] { Loop(); });

    bool elevated = WindowFinder::IsCurrentProcessElevated();
    LOG_INFO(Format(L"自动剧情已启动（本程序提权=%s）", elevated ? L"true" : L"false"));
    if (!elevated) {
        LOG_INFO(L"若游戏以管理员运行，请以管理员身份重新启动本程序，否则输入会被系统拦截");
    }
}

void AutoSkipEngine::Stop() {
    if (!running_.exchange(false)) {
        return;
    }

    if (thread_.joinable()) {
        thread_.join();
    }

    LOG_INFO(L"自动剧情已停止");
    SetStatus(L"已停止");
}

void AutoSkipEngine::Loop() {
    // 关键：WIC（PNG 编解码）依赖 COM。
    // 主线程在 wWinMain 里已经初始化过 COM，但工作线程是独立的 COM apartment，
    // 必须自己再初始化一次。否则 CoCreateInstance(CLSID_WICImagingFactory) 会返回
    // CO_E_NOTINITIALIZED，导致 Image::LoadFromFile / LoadFromMemory 全部失败，
    // 表现为"识别素材加载失败"、所有匹配分数恒为 0。
    HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    while (running_.load()) {
        auto frameStart = std::chrono::steady_clock::now();

        // 本轮轮询间隔：游戏就绪时用配置值（默认 50ms），否则放大到空闲间隔
        const int intervalMs = IsGameReadyForProcessing() ? config_.loopIntervalMs
                                                          : kIdleIntervalMs;
        const bool processing = (intervalMs == config_.loopIntervalMs);

        if (processing) {
            try {
                Tick();
            } catch (const std::exception& e) {
                LOG_ERROR(Format(L"主循环异常：%hs", e.what()));
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            } catch (...) {
                LOG_ERROR(L"主循环发生未知异常");
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }

            // 帧率统计（只统计真正处理过的帧）
            ++fpsFrames_;
            auto now = std::chrono::steady_clock::now();
            double windowSeconds =
                std::chrono::duration<double>(now - fpsWindowStart_).count();
            if (windowSeconds >= 1.0) {
                std::lock_guard<std::mutex> lock(stateMutex_);
                state_.fps = (double)fpsFrames_ / windowSeconds;
                fpsFrames_ = 0;
                fpsWindowStart_ = now;
            }
        } else {
            // 空闲态：不采集、不识别，帧率显示为 0
            std::lock_guard<std::mutex> lock(stateMutex_);
            state_.fps = 0.0;
        }

        int elapsedMs = (int)std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - frameStart)
                            .count();
        int wait = intervalMs - elapsedMs;
        if (wait > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(wait));
        }
    }

    if (SUCCEEDED(comResult)) {
        CoUninitialize();
    }

    CleanupCapture();
}

bool AutoSkipEngine::IsGameReadyForProcessing() {
    // 1. 定位游戏窗口（缓存，窗口失效才重新枚举）
    if (gameWindow_ == nullptr || !IsWindow(gameWindow_)) {
        gameWindow_ = WindowFinder::FindGameWindow();
        if (gameWindow_ != nullptr) {
            gameProcessId_ = WindowFinder::GetProcessIdOf(gameWindow_);
            RefreshWindowDiagnostics();
        }
    }

    if (gameWindow_ == nullptr) {
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            state_.gameWindowFound = false;
            state_.gameForeground = false;
        }
        SetStatus(L"未找到游戏窗口，请先启动原神");
        LogDiagnosticsPeriodically();
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        state_.gameWindowFound = true;
    }

    if (IsIconic(gameWindow_)) {
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            state_.gameForeground = false;
        }
        SetStatus(L"游戏窗口已最小化");
        LogDiagnosticsPeriodically();
        return false;
    }

    // 2. 前台限制：焦点不在游戏上时完全停止处理，避免误触其他窗口
    const bool foreground = WindowFinder::IsForeground(gameWindow_);
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        state_.gameForeground = foreground;
    }
    if (!foreground) {
        SetStatus(L"游戏不在前台，已暂停检测（点击游戏窗口继续）");
        LogDiagnosticsPeriodically();
        return false;
    }

    return true;
}

void AutoSkipEngine::Tick() {
    // 前置条件（窗口存在 / 未最小化 / 游戏在前台）已由 IsGameReadyForProcessing 保证，
    // 这里只做真正的采集与识别。

    // 1. 取客户区矩形
    RECT client{};
    if (!WindowFinder::GetClientRectOnScreen(gameWindow_, client)) {
        SetStatus(L"无法获取游戏画面区域");
        LogDiagnosticsPeriodically();
        return;
    }
    const int clientWidth = client.right - client.left;
    const int clientHeight = client.bottom - client.top;
    lastClientRect_ = client;

    // 2. 准备截图器与素材
    EnsureCapture();
    if (!capture_ || !capture_->IsReady()) {
        SetStatus(L"截图后端不可用：" +
                  (capture_ ? capture_->LastError() : std::wstring(L"初始化失败")));
        LogDiagnosticsPeriodically();
        return;
    }
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        state_.captureBackend = capture_->Name();
    }

    const double scale = (double)clientWidth / (double)SkipAssets::BaseWidth;
    EnsureAssets(scale);
    if (!assetsLoaded_) {
        SetStatus(L"识别素材加载失败");
        LogDiagnosticsPeriodically();
        return;
    }

    // 3. 截图
    Image frame;
    {
        std::lock_guard<std::mutex> lock(captureMutex_);
        if (!capture_->Capture(client.left, client.top, clientWidth, clientHeight, frame)) {
            SetStatus(L"截图失败或无新画面");
            return;
        }
    }
    if (frame.Empty()) {
        SetStatus(L"截图失败或无新画面");
        LogDiagnosticsPeriodically();
        return;
    }

    const int width = frame.Width();
    const int height = frame.Height();
    const double brightness = frame.MeanBrightness();
    const GrayImage gray = frame.ToGray();

    // 首次成功截图时自动落盘一帧原始画面，便于排查"识别不到"的问题。
    // 只存一次，文件名带时间戳，放在 log\frames\ 下。
    if (!savedFirstFrame_) {
        savedFirstFrame_ = true;
        std::wstring logDir = JoinPath(ExecutableDirectory(), L"log");
        std::wstring frameDir = JoinPath(logDir, L"frames");
        CreateDirectoryW(logDir.c_str(), nullptr);
        CreateDirectoryW(frameDir.c_str(), nullptr);

        std::wstring path = JoinPath(frameDir, L"first_frame_" + TimestampForFileName() + L".png");
        if (frame.SavePng(path)) {
            LOG_INFO(L"已自动保存首帧原始画面（排查用）：" + path);
        }
    }

    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        ++state_.totalFrames;
        state_.frameWidth = width;
        state_.frameHeight = height;
        state_.brightness = brightness;
    }

    // 4. 决策
    double talkScore = 0.0;
    bool talking = DetectTalkUi(gray, width, height, talkScore);

    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        state_.talkUiDetected = talking;
        state_.talkUiScore = talkScore;
    }

    if (talking) {
        lastPlayingTime_ = std::chrono::steady_clock::now();

        // 优先处理对话选项：橙色优先 > 兜底策略（全部走键盘，不动鼠标）
        bool hasOption = TrySelectOptionByKeyboard(frame, gray, width, height, scale);
        if (hasOption) {
            SetStatus(L"对话中：已选择选项");
            LogDiagnosticsPeriodically();
            return;
        }

        if (config_.quicklySkipConversations && IsCooldownElapsed(lastSkipTime_, config_.actionIntervalMs)) {
            std::wstring how = AdvanceDialogue();
            lastSkipTime_ = std::chrono::steady_clock::now();
            SetStatus(L"对话中：推进对话（" + how + L"）");
        } else {
            SetStatus(L"对话中");
        }

        LogDiagnosticsPeriodically();
        return;
    }

    // 非对话状态：底部三角 / 关闭弹窗 / 黑屏点击
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        state_.optionBubbleCount = 0;
        state_.triangleFound = false;
    }

    auto sincePlaying = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::steady_clock::now() - lastPlayingTime_)
                            .count();
    bool recentlyPlaying = sincePlaying <= kPlayingGraceSeconds;

    if (recentlyPlaying && config_.autoClosePopup) {
        // 与 BetterGI 一致：剧情仍在进行但对话 UI 已收起时，
        // 点击底部实心小三角来关闭道具弹窗 / 推进剧情
        if (IsCooldownElapsed(lastTriangleClickTime_, kTriangleClickCooldownMs)) {
            MatchResult triangle;
            bool found = DetectBottomTriangle(frame, width, height, scale, triangle);
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                state_.triangleFound = found;
            }
            if (found) {
                input_.ClickAt(client.left + triangle.CenterX(), client.top + triangle.CenterY());
                lastTriangleClickTime_ = std::chrono::steady_clock::now();
                lastPlayingTime_ = std::chrono::steady_clock::now();
                SetStatus(L"点击底部三角（关闭道具弹窗/推进）");
                LogDiagnosticsPeriodically();
                return;
            }
        }

        if (TryClosePopup(gray, width, height)) {
            SetStatus(L"已关闭弹出页面");
            LogDiagnosticsPeriodically();
            return;
        }
    }

    if (config_.blackScreenClickEnabled && IsCooldownElapsed(lastBlackClickTime_, kBlackClickCooldownMs)) {
        double dark = Cv::DarkRatio(gray);
        // 太黑说明是加载过场，点击无效；只有"黑屏 + 少量元素"才是可推进的剧情黑屏
        if (dark >= 0.5 && dark < 0.99) {
            input_.ClickAt(client.left + width / 2, client.top + height / 2);
            lastBlackClickTime_ = std::chrono::steady_clock::now();
            SetStatus(L"黑屏剧情：点击推进");
            LogDiagnosticsPeriodically();
            return;
        }
    }

    SetStatus(L"待机中（未检测到对话）");
    LogDiagnosticsPeriodically();
}

// ---------------------------------------------------------------------------
// 识别
// ---------------------------------------------------------------------------

bool AutoSkipEngine::DetectTalkUi(const GrayImage& gray, int width, int height, double& bestScore) {
    // ROI 取自 BetterGI：左上角 1/3 宽、1/8 高
    const int roiX = 0;
    const int roiY = 0;
    const int roiW = std::max(1, width / 3);
    const int roiH = std::max(1, height / 8);

    double disabledScore = 0.0;
    double stopScore = 0.0;

    if (!assets_.disabledUiButton.Empty()) {
        disabledScore = Cv::MatchBestScore(gray, assets_.disabledUiButton.ToGray(),
                                           roiX, roiY, roiW, roiH);
        if (disabledScore >= kTalkUiThreshold) {
            bestScore = disabledScore;
            return true;
        }
    }

    if (!assets_.stopAutoButton.Empty()) {
        stopScore = Cv::MatchBestScore(gray, assets_.stopAutoButton.ToGray(),
                                       roiX, roiY, roiW, roiH);
        if (stopScore >= kTalkUiThreshold) {
            bestScore = stopScore;
            return true;
        }
    }

    bestScore = std::max(disabledScore, stopScore);
    return false;
}

bool AutoSkipEngine::TrySelectOptionByKeyboard(const Image& frame, const GrayImage& gray,
                                               int width, int height, double scale) {
    if (assets_.optionIcon.Empty()) {
        return false;
    }

    // 选项气泡出现在屏幕右侧区域
    const int roiX = width / 2;
    const int roiY = height / 12;
    const int roiW = std::max(1, width - width / 2 - width / 6);
    const int roiH = std::max(1, height - height / 12 - 10);

    GrayImage optionTemplate = assets_.optionIcon.ToGray();
    auto bubbles = Cv::MatchAll(gray, optionTemplate, roiX, roiY, roiW, roiH,
                                kOptionThreshold, 32);

    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        state_.optionBubbleCount = (int)bubbles.size();
    }

    if (bubbles.empty()) {
        return false;
    }

    // 有选项但仍在冷却中：不重复选择，避免连点跳过选项
    if (!IsCooldownElapsed(lastOptionClickTime_, kOptionClickCooldownMs)) {
        return true;
    }

    // 按 Y 升序：下标 0 = 最上方 = 第一个选项（键盘默认焦点所在）
    std::sort(bubbles.begin(), bubbles.end(),
              [](const MatchResult& a, const MatchResult& b) { return a.y < b.y; });

    int targetIndex = ResolveTargetOptionIndex(frame, bubbles, width, height, scale);
    SelectOptionByKey(targetIndex);
    lastOptionClickTime_ = std::chrono::steady_clock::now();

    LOG_INFO(Format(L"键盘选择选项：第 %d 个（共 %d 个）", targetIndex + 1, (int)bubbles.size()));
    return true;
}

int AutoSkipEngine::ResolveTargetOptionIndex(const Image& frame,
                                             const std::vector<MatchResult>& ascending,
                                             int width, int height, double scale) {
    // 优先橙色选项：逐个判断文字是否为橙色，命中则返回其下标
    if (config_.preferOrangeOption && !frame.Empty()) {
        for (size_t i = 0; i < ascending.size(); ++i) {
            const MatchResult& bubble = ascending[i];

            int textX = bubble.x + bubble.w + (int)std::lround(8 * scale);
            int textW = (int)std::lround(535 * scale);
            int textY = bubble.y - (int)std::lround(8 * scale);
            int textH = bubble.h + (int)std::lround(16 * scale);

            Image region = frame.Crop(textX, textY, textW, textH);
            if (!region.Empty() && Cv::IsOrangeRegion(region)) {
                return (int)i;
            }
        }
    }

    switch (config_.optionStrategy) {
        case ChatOptionStrategy::First:
            return 0;
        case ChatOptionStrategy::Random:
            return ascending.empty() ? 0 : (int)(std::rand() % ascending.size());
        case ChatOptionStrategy::Last:
        default:
            return (int)ascending.size() - 1;
    }
}

void AutoSkipEngine::SelectOptionByKey(int index) {
    // 与 BetterGI 一致：默认焦点在第一个选项，S 向下移动，F 确认
    for (int i = 0; i < index; ++i) {
        input_.PressKey(vk::S);
        Sleep(100);
    }
    input_.PressInteract();
}

bool AutoSkipEngine::DetectBottomTriangle(const Image& frame, int width, int height,
                                          double scale, MatchResult& out) {
    return DetectBottomTriangleIn(frame, scale, out);
}

bool AutoSkipEngine::DetectBottomTriangleIn(const Image& frame, double scale, MatchResult& out) {
    // 底部"继续对话"指示器：金色菱形外框 + 内部实心倒三角。
    // 1080p 下中心约 (956,1045)，整体约 24x25 像素，位置固定，随分辨率等比缩放。
    const int cx = (int)std::lround(kIndicatorCenterX1080 * scale);
    const int cy = (int)std::lround(kIndicatorCenterY1080 * scale);
    const int halfW = (int)std::lround(kIndicatorSearchHalfW1080 * scale);
    const int halfH = (int)std::lround(kIndicatorSearchHalfH1080 * scale);

    // 手动与边界求交，便于把局部坐标换算回整帧坐标
    const int rx = std::max(0, cx - halfW);
    const int ry = std::max(0, cy - halfH);
    const int rw = std::min(frame.Width(), cx + halfW) - rx;
    const int rh = std::min(frame.Height(), cy + halfH) - ry;
    if (rw <= 2 || rh <= 2) {
        return false;
    }

    Image region = frame.Crop(rx, ry, rw, rh);
    if (region.Empty()) {
        return false;
    }

    GrayImage mask = Cv::MaskHsvRange(region, kTriYellowHLo, kTriYellowSLo, kTriYellowVLo,
                                      kTriYellowHHi, kTriYellowSHi, kTriYellowVHi);

    long long count = 0;
    long long sumX = 0;
    long long sumY = 0;
    int minX = region.Width();
    int maxX = -1;
    int minY = region.Height();
    int maxY = -1;

    for (int y = 0; y < region.Height(); ++y) {
        const uint8_t* row = mask.Row(y);
        for (int x = 0; x < region.Width(); ++x) {
            if (row[x] == 0) {
                continue;
            }
            ++count;
            sumX += x;
            sumY += y;
            if (x < minX) minX = x;
            if (x > maxX) maxX = x;
            if (y < minY) minY = y;
            if (y > maxY) maxY = y;
        }
    }

    // 金色像素数不足 -> 指示器不存在
    const double expectedPixels = kIndicatorPixels1080 * scale * scale;
    if ((double)count < expectedPixels * kIndicatorMinRatio) {
        return false;
    }

    // 包围盒不应过分铺开，否则是把大片同色区域误判
    const int boxW = maxX - minX + 1;
    const int boxH = maxY - minY + 1;
    const double maxBox = kIndicatorMaxBox1080 * scale;
    if ((double)boxW > maxBox || (double)boxH > maxBox) {
        return false;
    }

    // 以包围盒中心作为点击点（指示器左右对称，与金色像素质心基本一致）
    out.x = rx + minX;
    out.y = ry + minY;
    out.w = boxW;
    out.h = boxH;
    out.score = (double)count;
    return true;
}

std::wstring AutoSkipEngine::AdvanceDialogue() {
    if (config_.useInteractKey) {
        input_.PressInteract();
        return L"交互键";
    }
    input_.PressSpace();
    return L"空格";
}

bool AutoSkipEngine::TryClosePopup(const GrayImage& gray, int width, int height) {
    if (assets_.pageClose.Empty()) {
        return false;
    }

    if (!IsCooldownElapsed(lastSkipTime_, kPopupCloseCooldownMs)) {
        return false;
    }

    const int roiX = std::max(0, width - width / 8);
    const int roiY = 0;
    const int roiW = std::max(1, width / 8);
    const int roiH = std::max(1, height / 8);

    GrayImage closeTemplate = assets_.pageClose.ToGray();
    MatchResult match;
    if (!Cv::MatchBest(gray, closeTemplate, roiX, roiY, roiW, roiH, kPageCloseThreshold, match)) {
        return false;
    }

    input_.PressEscape();
    lastSkipTime_ = std::chrono::steady_clock::now();
    return true;
}

// ---------------------------------------------------------------------------
// 资源与状态
// ---------------------------------------------------------------------------

void AutoSkipEngine::EnsureCapture() {
    if (capture_) {
        return;
    }

    capture_ = CreateScreenCapture(config_.captureBackend);
    if (capture_ && capture_->IsReady()) {
        LOG_INFO(std::wstring(L"截图后端已就绪：") + capture_->Name());
    } else {
        LOG_WARN(L"截图后端初始化失败：" + (capture_ ? capture_->LastError() : std::wstring(L"null")));
    }
}

void AutoSkipEngine::EnsureAssets(double scale) {
    if (assetsLoaded_ && std::fabs(assetsScale_ - scale) < 0.001) {
        return;
    }

    assets_ = SkipAssets::Load(scale);
    assetsScale_ = scale;
    assetsLoaded_ = true;

    if (!assets_.missingFiles.empty()) {
        std::wstring names;
        for (const auto& n : assets_.missingFiles) {
            if (!names.empty()) names += L"、";
            names += n;
        }
        LOG_WARN(L"缺少识别素材（外部文件与内嵌资源都没有）：" + names);
    } else {
        // 统计素材来源，便于确认用的是外部文件还是内嵌资源
        int fromFile = 0;
        int fromResource = 0;
        for (const auto& s : assets_.sources) {
            if (s == L"file") ++fromFile;
            else if (s == L"resource") ++fromResource;
        }
        LOG_INFO(Format(L"识别素材已加载（缩放 %.3f，外部文件 %d 个 / 内嵌资源 %d 个）",
                        scale, fromFile, fromResource));
    }
}

void AutoSkipEngine::RefreshWindowDiagnostics() {
    std::wstring title = WindowFinder::GetTitle(gameWindow_);
    std::wstring className = WindowFinder::GetClassNameOf(gameWindow_);

    bool selfElevated = WindowFinder::IsCurrentProcessElevated();
    bool gameElevated = false;
    bool known = WindowFinder::TryGetProcessElevation(gameProcessId_, gameElevated);

    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        state_.windowInfo = Format(L"%s [%s] pid=%lu",
                                   title.c_str(), className.c_str(),
                                   (unsigned long)gameProcessId_);
        state_.elevationInfo = Format(L"本程序提权=%s，游戏提权=%s",
                                      selfElevated ? L"true" : L"false",
                                      known ? (gameElevated ? L"true" : L"false") : L"未知");
    }

    LOG_INFO(L"已定位游戏窗口：" + title + L" [" + className + L"]");
    LOG_INFO(std::wstring(L"权限检查：") +
             (known ? (gameElevated ? L"游戏已提权" : L"游戏未提权") : L"游戏提权未知"));

    if (!selfElevated && known && gameElevated) {
        LOG_ERROR(L"游戏以管理员运行，但本程序未提权 —— 输入会被 UIPI 拦截，请以管理员身份重新启动本程序！");
    }
}

void AutoSkipEngine::LogDiagnosticsPeriodically() {
    if (!IsCooldownElapsed(lastDiagnosticLog_, 2000)) {
        return;
    }
    lastDiagnosticLog_ = std::chrono::steady_clock::now();

    EngineState s = GetState();
    LOG_INFO(Format(L"[诊断] 画面=%dx%d 客户区=(%ld,%ld) %ldx%ld 亮度=%.1f "
                    L"对话=%s(匹配%.2f) 选项气泡=%d 指示器=%s 帧率=%.1f",
                    s.frameWidth, s.frameHeight,
                    (long)lastClientRect_.left, (long)lastClientRect_.top,
                    (long)(lastClientRect_.right - lastClientRect_.left),
                    (long)(lastClientRect_.bottom - lastClientRect_.top),
                    s.brightness,
                    s.talkUiDetected ? L"true" : L"false", s.talkUiScore,
                    s.optionBubbleCount,
                    s.triangleFound ? L"true" : L"false", s.fps));
}

void AutoSkipEngine::SetStatus(const std::wstring& text) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    state_.status = text;
}

EngineState AutoSkipEngine::GetState() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    EngineState copy = state_;
    copy.running = running_.load();
    return copy;
}

void AutoSkipEngine::ResetCapture() {
    std::lock_guard<std::mutex> lock(captureMutex_);
    CleanupCapture();
}

void AutoSkipEngine::CleanupCapture() {
    capture_.reset();
}

// ---------------------------------------------------------------------------
// 调试辅助
// ---------------------------------------------------------------------------

std::wstring AutoSkipEngine::SaveDebugFrame() {
    if (gameWindow_ == nullptr) {
        return {};
    }

    RECT client{};
    if (!WindowFinder::GetClientRectOnScreen(gameWindow_, client)) {
        return {};
    }

    EnsureCapture();
    if (!capture_ || !capture_->IsReady()) {
        return {};
    }

    const int clientWidth = client.right - client.left;
    const int clientHeight = client.bottom - client.top;
    const double scale = (double)clientWidth / (double)SkipAssets::BaseWidth;
    EnsureAssets(scale);

    Image frame;
    {
        std::lock_guard<std::mutex> lock(captureMutex_);
        // 多抓几次，规避 DXGI "无新帧"返回空的情况
        for (int i = 0; i < 5 && frame.Empty(); ++i) {
            capture_->Capture(client.left, client.top, clientWidth, clientHeight, frame);
            if (frame.Empty()) {
                Sleep(80);
            }
        }
    }

    if (frame.Empty()) {
        return {};
    }

    const int width = frame.Width();
    const int height = frame.Height();

    // 对话按钮识别区（绿色）
    frame.DrawRect(0, 0, std::max(1, width / 3), std::max(1, height / 8), 0, 255, 0, 2);

    // 选项识别区（橙色）
    frame.DrawRect(width / 2, height / 12,
                   std::max(1, width - width / 2 - width / 6),
                   std::max(1, height - height / 12 - 10), 0, 128, 255, 2);

    // 命中的选项气泡（红色）
    if (!assets_.optionIcon.Empty()) {
        GrayImage gray = frame.ToGray();
        auto bubbles = Cv::MatchAll(gray, assets_.optionIcon.ToGray(),
                                    width / 2, height / 12,
                                    std::max(1, width - width / 2 - width / 6),
                                    std::max(1, height - height / 12 - 10),
                                    kOptionThreshold, 32);
        for (const auto& b : bubbles) {
            frame.DrawRect(b.x, b.y, b.w, b.h, 0, 0, 255, 2);
        }
    }

    // 识别到的底部三角（洋红）
    MatchResult triangle;
    if (DetectBottomTriangle(frame, width, height, scale, triangle)) {
        frame.DrawRect(triangle.x, triangle.y, triangle.w, triangle.h, 255, 0, 255, 2);
    }

    std::wstring dir = JoinPath(ExecutableDirectory(), L"log\\frames");
    CreateDirectoryW(JoinPath(ExecutableDirectory(), L"log").c_str(), nullptr);
    CreateDirectoryW(dir.c_str(), nullptr);

    std::wstring stamp = TimestampForFileName();

    // 额外导出底部指示器区域的放大图，并统计金色像素数。
    // 指示器只在特定场景出现，光看主截图很难判断"到底没有指示器，还是颜色没匹配上"。
    {
        const int cx = (int)std::lround(kIndicatorCenterX1080 * scale);
        const int cy = (int)std::lround(kIndicatorCenterY1080 * scale);
        const int halfW = (int)std::lround(kIndicatorSearchHalfW1080 * scale);
        const int halfH = (int)std::lround(kIndicatorSearchHalfH1080 * scale);

        Image region = frame.Crop(cx - halfW, cy - halfH, halfW * 2, halfH * 2);
        if (!region.Empty()) {
            GrayImage mask = Cv::MaskHsvRange(region, kTriYellowHLo, kTriYellowSLo, kTriYellowVLo,
                                              kTriYellowHHi, kTriYellowSHi, kTriYellowVHi);
            int hits = 0;
            for (uint8_t v : mask.pixels) {
                if (v != 0) {
                    ++hits;
                }
            }

            MatchResult m;
            bool found = DetectBottomTriangleIn(frame, scale, m);
            LOG_INFO(Format(L"底部指示器：%s（区域 %dx%d 内金色像素 %d 个，判定阈值 %.0f）",
                            found ? L"命中" : L"未命中", halfW * 2, halfH * 2, hits,
                            kIndicatorPixels1080 * scale * scale * kIndicatorMinRatio));

            std::wstring triPath = JoinPath(dir, L"frame_" + stamp + L"_indicator.png");
            region.Resize(region.Width() * 5, region.Height() * 5).SavePng(triPath);
            LOG_INFO(L"指示器区域放大图已保存：" + triPath);
        }
    }

    std::wstring path = JoinPath(dir, L"frame_" + stamp + L".png");
    if (!frame.SavePng(path)) {
        return {};
    }

    LOG_INFO(Format(L"已保存调试截图：%s（%dx%d）", path.c_str(), width, height));
    return path;
}

std::wstring AutoSkipEngine::BuildDiagnosticReport() {
    EngineState s = GetState();

    SYSTEMTIME st{};
    GetLocalTime(&st);

    std::wstring out;
    out += L"===== BetterYuanshen 诊断报告 =====\r\n";
    out += Format(L"时间: %04d-%02d-%02d %02d:%02d:%02d\r\n",
                  st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    out += Format(L"运行中: %s\r\n", s.running ? L"true" : L"false");
    out += L"状态: " + s.status + L"\r\n";
    out += L"游戏窗口: " + s.windowInfo + L"\r\n";
    out += Format(L"找到窗口: %s，前台: %s\r\n",
                  s.gameWindowFound ? L"true" : L"false",
                  s.gameForeground ? L"true" : L"false");
    out += L"权限: " + s.elevationInfo + L"\r\n";
    out += L"截图后端: " + s.captureBackend + L"\r\n";
    out += Format(L"画面尺寸: %dx%d\r\n", s.frameWidth, s.frameHeight);
    out += Format(L"画面亮度: %.2f（长期为 0 表示抓到黑帧）\r\n", s.brightness);
    out += Format(L"对话识别: %s，最高匹配=%.3f（阈值 0.75）\r\n",
                  s.talkUiDetected ? L"true" : L"false", s.talkUiScore);
    out += Format(L"选项气泡数: %d\r\n", s.optionBubbleCount);
    out += Format(L"底部三角: %s\r\n", s.triangleFound ? L"true" : L"false");
    out += Format(L"累计帧数: %lld，帧率: %.1f\r\n", s.totalFrames, s.fps);
    out += L"最近错误: " + (s.lastError.empty() ? std::wstring(L"无") : s.lastError) + L"\r\n";
    out += L"==================================\r\n";
    return out;
}

}  // namespace bys
