#pragma once

// 自动剧情引擎。后台线程按固定间隔循环：截图 → 识别 → 决策 → 输入。
// 仅处理前台运行的游戏，全程使用 SendInput 模拟真实输入。

#include <windows.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "autoskip/AutoSkipConfig.h"
#include "autoskip/SkipAssets.h"
#include "capture/ScreenCapture.h"
#include "core/Cv.h"
#include "input/InputSimulator.h"

namespace bys {

// 引擎状态快照（供 UI 线程读取）
struct EngineState {
    bool running = false;
    std::wstring status = L"未启动";
    bool gameWindowFound = false;
    bool gameForeground = false;
    std::wstring windowInfo = L"-";
    std::wstring elevationInfo = L"-";
    std::wstring captureBackend = L"-";
    int frameWidth = 0;
    int frameHeight = 0;
    double brightness = 0.0;
    bool talkUiDetected = false;
    double talkUiScore = 0.0;
    int optionBubbleCount = 0;
    bool triangleFound = false;
    double fps = 0.0;
    long long totalFrames = 0;
    std::wstring lastError;
};

class AutoSkipEngine {
public:
    AutoSkipEngine();
    ~AutoSkipEngine();

    AutoSkipEngine(const AutoSkipEngine&) = delete;
    AutoSkipEngine& operator=(const AutoSkipEngine&) = delete;

    AutoSkipConfig& Config() { return config_; }

    void Start();
    void Stop();
    bool IsRunning() const { return running_.load(); }

    // 线程安全地取一份状态快照
    EngineState GetState() const;

    // 立即抓一帧保存，并在图上标出识别区域与结果。返回保存路径（失败返回空）
    std::wstring SaveDebugFrame();

    // 生成纯文本诊断报告
    std::wstring BuildDiagnosticReport();

    // 重建截图会话（切换后端后调用）
    void ResetCapture();

    // 识别底部实心小三角（黄色或蓝色）。位置与颜色参考 BetterGI。
    // 做成 static 便于自检直接调用（不依赖引擎实例与游戏窗口）
    static bool DetectBottomTriangleIn(const Image& frame, double scale, MatchResult& out);

private:
    void Loop();
    void Tick();

    // 判断当前是否值得进入完整处理流程：
    // 窗口存在、未最小化、且游戏在前台。任一不满足返回 false，
    // 此时主循环会跳过 Tick 并把轮询间隔放大，避免空转。
    bool IsGameReadyForProcessing();

    void EnsureCapture();
    void EnsureAssets(double scale);
    void RefreshWindowDiagnostics();

    // 判断是否处于对话界面。bestScore 返回最高匹配分数
    bool DetectTalkUi(const GrayImage& gray, int width, int height, double& bestScore);

    // 识别对话选项并用键盘选择（不移动鼠标）。返回是否存在选项
    bool TrySelectOptionByKeyboard(const Image& frame, const GrayImage& gray,
                                   int width, int height, double scale);

    // 用键盘把焦点从第一个选项移到第 index 个并确认
    void SelectOptionByKey(int index);

    // 决定要选第几个选项（下标从 0 开始，0 为最上方）
    int ResolveTargetOptionIndex(const Image& frame, const std::vector<MatchResult>& ascending,
                                 int width, int height, double scale);

    // 识别底部实心小三角（黄色或蓝色）。位置与颜色参考 BetterGI
    bool DetectBottomTriangle(const Image& frame, int width, int height, double scale,
                              MatchResult& out);

    // 推进对话（按空格或交互键）
    std::wstring AdvanceDialogue();

    // 关闭右上角弹窗（识别关闭按钮 + ESC）
    bool TryClosePopup(const GrayImage& gray, int width, int height);

    void SetStatus(const std::wstring& text);
    void LogDiagnosticsPeriodically();
    void CleanupCapture();

    static bool IsCooldownElapsed(const std::chrono::steady_clock::time_point& last, int ms);

    AutoSkipConfig config_;
    InputSimulator input_;

    std::thread thread_;
    std::atomic<bool> running_{false};

    // 保护截图器：DXGI duplication 不是线程安全的，界面手动抓图需与主循环互斥
    mutable std::mutex captureMutex_;

    std::unique_ptr<IScreenCapture> capture_;
    SkipAssets assets_;
    bool assetsLoaded_ = false;
    double assetsScale_ = -1.0;

    HWND gameWindow_ = nullptr;
    DWORD gameProcessId_ = 0;

    // 首次成功截图时自动落盘一帧，便于排查"识别不到"的问题
    bool savedFirstFrame_ = false;

    // 最近一次取到的客户区矩形，供诊断输出
    RECT lastClientRect_{};

    // 各类动作的冷却时间戳
    std::chrono::steady_clock::time_point lastSkipTime_{};
    std::chrono::steady_clock::time_point lastOptionClickTime_{};
    std::chrono::steady_clock::time_point lastBlackClickTime_{};
    std::chrono::steady_clock::time_point lastTriangleClickTime_{};
    std::chrono::steady_clock::time_point lastDiagnosticLog_{};

    // 最近一次处于对话界面的时间（宽限 10 秒，与 BetterGI 一致）
    std::chrono::steady_clock::time_point lastPlayingTime_{};

    // 状态（由 stateMutex_ 保护）
    mutable std::mutex stateMutex_;
    EngineState state_;

    std::chrono::steady_clock::time_point fpsWindowStart_{};
    long long fpsFrames_ = 0;
};

}  // namespace bys
