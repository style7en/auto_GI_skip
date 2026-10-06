// BetterYuanshen —— 原神自动剧情小工具（Win32 + C++ 实现）
//
// 入口：解析命令行 -> 初始化 COM -> 权限检查 -> 进入消息循环或测试模式。

#include <windows.h>
#include <objbase.h>   // CoInitializeEx / CoUninitialize
#include <shellapi.h>

#include <string>
#include <thread>
#include <vector>

#include "autoskip/AutoSkipEngine.h"
#include "autoskip/SkipAssets.h"
#include "capture/ScreenCapture.h"
#include "capture/WindowFinder.h"
#include "core/Cv.h"
#include "resources/resource.h"
#include "ui/MainWindow.h"
#include "util/Log.h"
#include "util/StringUtil.h"

using namespace bys;

namespace {

// ---------------------------------------------------------------------------
// 命令行
// ---------------------------------------------------------------------------

std::vector<std::wstring> GetCommandLineArgs() {
    std::vector<std::wstring> args;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr) {
        return args;
    }
    for (int i = 0; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }
    LocalFree(argv);
    return args;
}

bool HasFlag(const std::vector<std::wstring>& args, const wchar_t* flag) {
    for (const auto& a : args) {
        if (_wcsicmp(a.c_str(), flag) == 0) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// 以管理员身份重新启动
// ---------------------------------------------------------------------------

void RelaunchAsAdministrator() {
    wchar_t path[MAX_PATH * 2] = {};
    if (GetModuleFileNameW(nullptr, path, MAX_PATH * 2) == 0) {
        return;
    }

    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.lpVerb = L"runas";
    info.lpFile = path;
    info.nShow = SW_SHOWNORMAL;
    ShellExecuteExW(&info);
}

// ---------------------------------------------------------------------------
// 截图自检：验证 DXGI / GDI 能否抓到非黑帧
// ---------------------------------------------------------------------------

int RunCaptureTest(const std::wstring& outputDir) {
    CreateDirectoryW(outputDir.c_str(), nullptr);

    std::wstring reportPath = JoinPath(outputDir, L"capture-report.txt");
    std::wstring report;

    auto append = [&](const std::wstring& line) {
        report += line + L"\r\n";
        HANDLE file = CreateFileW(reportPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            const unsigned char bom[3] = {0xEF, 0xBB, 0xBF};
            WriteFile(file, bom, 3, &written, nullptr);
            std::string utf8 = WideToUtf8(report);
            WriteFile(file, utf8.data(), (DWORD)utf8.size(), &written, nullptr);
            CloseHandle(file);
        }
    };

    const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    append(Format(L"虚拟桌面: %d,%d %dx%d", vx, vy, vw, vh));

    struct BackendDef {
        const wchar_t* label;
        bys::CaptureBackend backend;
    };
    const BackendDef backends[] = {
        {L"DXGI", bys::CaptureBackend::Dxgi},
        {L"GDI", bys::CaptureBackend::Gdi},
    };

    for (const auto& def : backends) {
        auto capture = bys::CreateScreenCapture(def.backend);
        append(Format(L"[%s] 名称=%s 就绪=%s 错误=%s", def.label, capture->Name(),
                      capture->IsReady() ? L"true" : L"false",
                      capture->LastError().empty() ? L"无" : capture->LastError().c_str()));

        bys::Image frame;
        for (int i = 0; i < 15 && frame.Empty(); ++i) {
            capture->Capture(vx, vy, vw, vh, frame);
            append(Format(L"[%s] 第 %d 次截图 -> %s  诊断=%s", def.label, i + 1,
                          frame.Empty() ? L"null" : L"ok", capture->Diagnostics().c_str()));
            if (frame.Empty()) {
                Sleep(150);
            }
        }

        if (frame.Empty()) {
            append(Format(L"[%s] 未取得画面", def.label));
            continue;
        }

        append(Format(L"[%s] 画面=%dx%d 平均亮度=%.2f", def.label,
                      frame.Width(), frame.Height(), frame.MeanBrightness()));

        std::wstring file = JoinPath(outputDir, std::wstring(L"capture-") + def.label + L".png");
        if (frame.SavePng(file)) {
            append(Format(L"[%s] 已保存 %s", def.label, file.c_str()));
        } else {
            append(Format(L"[%s] 保存 PNG 失败", def.label));
        }
    }

    append(L"全部完成");
    return 0;
}

// ---------------------------------------------------------------------------
// 视觉算法自检：把模板贴到合成图上，验证能否原位置找回
// ---------------------------------------------------------------------------

int RunCvTest(const std::wstring& outputDir) {
    CreateDirectoryW(outputDir.c_str(), nullptr);

    std::wstring reportPath = JoinPath(outputDir, L"cv-report.txt");
    std::wstring report;
    auto append = [&](const std::wstring& line) { report += line + L"\r\n"; };

    // 走 SkipAssets 加载：优先外部 assets 目录，找不到回退到 exe 内嵌资源
    SkipAssets assets = SkipAssets::Load(1.0);
    Image tpl = assets.optionIcon;
    if (tpl.Empty()) {
        append(L"无法加载模板（外部文件与内嵌资源都没有）");
    } else {
        std::wstring source = assets.sources.empty() ? L"?" : assets.sources[0];
        append(Format(L"模板尺寸: %dx%d  来源=%s", tpl.Width(), tpl.Height(), source.c_str()));

        // 合成一张 400x300 的图，把模板贴在 (137, 89)
        const int canvasW = 400;
        const int canvasH = 300;
        const int pasteX = 137;
        const int pasteY = 89;

        bys::Image canvas(canvasW, canvasH);
        // 铺一个渐变背景，避免出现"全同色导致方差为 0"的退化情况
        for (int y = 0; y < canvasH; ++y) {
            uint8_t* row = canvas.Row(y);
            for (int x = 0; x < canvasW; ++x) {
                row[x * 3 + 0] = (uint8_t)((x * 3 + y) % 200 + 20);
                row[x * 3 + 1] = (uint8_t)((x + y * 2) % 180 + 30);
                row[x * 3 + 2] = (uint8_t)((x * 2 + y * 3) % 160 + 40);
            }
        }
        for (int y = 0; y < tpl.Height(); ++y) {
            uint8_t* dst = canvas.Row(pasteY + y) + (size_t)pasteX * 3;
            const uint8_t* src = tpl.Row(y);
            for (int x = 0; x < tpl.Width(); ++x) {
                dst[x * 3 + 0] = src[x * 3 + 0];
                dst[x * 3 + 1] = src[x * 3 + 1];
                dst[x * 3 + 2] = src[x * 3 + 2];
            }
        }

        bys::GrayImage gray = canvas.ToGray();
        double score = bys::Cv::MatchBestScore(gray, tpl.ToGray(), 0, 0, canvasW, canvasH);
        bys::MatchResult best;
        bool found = bys::Cv::MatchBest(gray, tpl.ToGray(), 0, 0, canvasW, canvasH, 0.9, best);

        append(Format(L"最高匹配分数: %.4f", score));
        append(Format(L"定位结果: %s  位置=(%d,%d) 期望=(%d,%d)",
                      found ? L"命中" : L"未命中", best.x, best.y, pasteX, pasteY));

        bool positionOk = found && best.x == pasteX && best.y == pasteY;
        append(positionOk ? L"位置校验: 通过" : L"位置校验: 失败");
        append(score > 0.99 ? L"分数校验: 通过（合成图应接近 1.0）" : L"分数校验: 偏低");

        // 橙色判定自检
        bys::Image orangeSwatch(40, 20);
        for (int y = 0; y < 20; ++y) {
            uint8_t* row = orangeSwatch.Row(y);
            for (int x = 0; x < 40; ++x) {
                row[x * 3 + 0] = 40;    // B
                row[x * 3 + 1] = 165;   // G
                row[x * 3 + 2] = 250;   // R  -> 橙色
            }
        }
        bys::Image graySwatch(40, 20);
        append(bys::Cv::IsOrangeRegion(orangeSwatch) ? L"橙色判定: 通过" : L"橙色判定: 失败");
        append(bys::Cv::IsOrangeRegion(graySwatch) ? L"灰色误判为橙色: 失败" : L"灰色误判为橙色: 通过（正确）");

        std::wstring png = JoinPath(outputDir, L"cv-canvas.png");
        canvas.SavePng(png);
        append(L"合成图已保存: " + png);
    }

    // ---- 底部指示器识别自检 ----
    // 指示器 = 金色菱形外框 + 内部实心倒三角，1080p 下中心约 (956,1045)、约 24x25 像素
    {
        const double scale = 2560.0 / 1920.0;  // 模拟 2560x1440 游戏窗口
        const int W = 2560;
        const int H = 1440;

        const int cx = (int)std::lround(956 * scale);
        const int cy = (int)std::lround(1045 * scale);
        const int halfW = (int)std::lround(12 * scale);
        const int halfH = (int)std::lround(12 * scale);

        // 正例：在指示器位置画金色实心倒三角（颜色取真实截图实测值 BGR(72,155,190)）
        {
            Image f(W, H);
            for (int dy = -halfH; dy <= halfH; ++dy) {
                int half = halfW * (halfH - std::abs(dy)) / std::max(1, halfH);
                for (int dx = -half; dx <= half; ++dx) {
                    int px = cx + dx;
                    int py = cy + dy;
                    if (px < 0 || py < 0 || px >= W || py >= H) {
                        continue;
                    }
                    uint8_t* p = f.Row(py) + (size_t)px * 3;
                    p[0] = 72;
                    p[1] = 155;
                    p[2] = 190;
                }
            }

            MatchResult m;
            bool found = AutoSkipEngine::DetectBottomTriangleIn(f, scale, m);
            append(Format(L"指示器自检[存在] -> %s  包围盒=(%d,%d) %dx%d  金色像素=%.0f",
                          found ? L"命中" : L"未命中", m.x, m.y, m.w, m.h, m.score));
        }

        // 反例：空白帧不应命中
        {
            Image f(W, H);
            MatchResult m;
            bool found = AutoSkipEngine::DetectBottomTriangleIn(f, scale, m);
            append(Format(L"指示器自检[不存在] -> %s",
                          found ? L"误判命中" : L"正确未命中"));
        }
    }

    // ---- 工作线程 COM 初始化自检 ----
    // 引擎跑在独立线程上，而 WIC（PNG 编解码）依赖 COM。
    // 这里验证：工作线程不初始化 COM 时素材会全部加载失败，初始化后正常。
    {
        auto loadOnWorkerThread = [](bool initCom) -> int {
            int loaded = 0;
            std::thread worker([&] {
                if (initCom) {
                    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
                }
                SkipAssets assets = SkipAssets::Load(1.0);
                loaded = 5 - (int)assets.missingFiles.size();
                if (initCom) {
                    CoUninitialize();
                }
            });
            worker.join();
            return loaded;
        };

        int withoutCom = loadOnWorkerThread(false);
        int withCom = loadOnWorkerThread(true);
        append(Format(L"工作线程素材加载：不初始化 COM -> %d/5 成功；初始化 COM -> %d/5 成功",
                      withoutCom, withCom));
        append(withCom == 5 && withoutCom < 5
                   ? L"  => 符合预期：工作线程必须自行初始化 COM"
                   : L"  => 与预期不符，请检查");
    }

    append(L"全部完成");

    HANDLE file = CreateFileW(reportPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        const unsigned char bom[3] = {0xEF, 0xBB, 0xBF};
        WriteFile(file, bom, 3, &written, nullptr);
        std::string utf8 = bys::WideToUtf8(report);
        WriteFile(file, utf8.data(), (DWORD)utf8.size(), &written, nullptr);
        CloseHandle(file);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 界面离屏渲染：把主窗口画到 PNG，用于检查布局（不打扰当前用户）
// ---------------------------------------------------------------------------

#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

int RunRenderUi(const std::wstring& outputPath, UINT forcedDpi) {
    HINSTANCE instance = GetModuleHandleW(nullptr);

    // 该模式在 wWinMain 里早于正式注册流程执行，这里必须先注册窗口类
    if (!MainWindow::RegisterWindowClass(instance)) {
        LOG_ERROR(L"注册窗口类失败，无法渲染界面");
        return 1;
    }

    MainWindow window;
    if (!window.Create(instance, /*offscreen=*/true, forcedDpi)) {
        LOG_ERROR(Format(L"创建离屏窗口失败，错误码 %lu", GetLastError()));
        return 1;
    }

    // 等布局与首帧绘制完成
    window.PumpFor(500);

    HWND hwnd = window.Handle();
    RECT windowRect{};
    RECT clientRect{};
    GetWindowRect(hwnd, &windowRect);
    GetClientRect(hwnd, &clientRect);

    const int fullW = windowRect.right - windowRect.left;
    const int fullH = windowRect.bottom - windowRect.top;
    const int clientW = clientRect.right - clientRect.left;
    const int clientH = clientRect.bottom - clientRect.top;
    if (fullW <= 0 || fullH <= 0 || clientW <= 0 || clientH <= 0) {
        return 1;
    }

    // 客户区在整窗中的偏移
    POINT origin{0, 0};
    ClientToScreen(hwnd, &origin);
    const int offsetX = origin.x - windowRect.left;
    const int offsetY = origin.y - windowRect.top;

    HDC screenDc = GetDC(nullptr);
    HDC memDc = CreateCompatibleDC(screenDc);

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = fullW;
    info.bmiHeader.biHeight = -fullH;  // 自上而下
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(screenDc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ oldObject = SelectObject(memDc, dib);

    BOOL ok = PrintWindow(hwnd, memDc, PW_RENDERFULLCONTENT);

    SelectObject(memDc, oldObject);

    int result = 0;
    if (ok && bits != nullptr) {
        Image full = Image::FromBgra(static_cast<const uint8_t*>(bits), fullW, fullH, fullW * 4);
        Image client = full.Crop(offsetX, offsetY, clientW, clientH);
        if (!client.Empty() && client.SavePng(outputPath)) {
            LOG_INFO(Format(L"界面已渲染到 %s（%dx%d）", outputPath.c_str(), clientW, clientH));
        } else {
            result = 1;
        }
    } else {
        LOG_ERROR(Format(L"PrintWindow 失败，错误码 %lu", GetLastError()));
        result = 1;
    }

    DeleteObject(dib);
    DeleteDC(memDc);
    ReleaseDC(nullptr, screenDc);

    return result;
}

// ---------------------------------------------------------------------------
// 离线识别自检：对指定图片跑底部指示器识别，用于验证真实截图
// ---------------------------------------------------------------------------

int RunDetectImage(const std::wstring& imagePath, double scale) {
    Image frame;
    if (!Image::LoadFromFile(imagePath, frame) || frame.Empty()) {
        LOG_ERROR(L"无法加载图片：" + imagePath);
        return 1;
    }

    std::wstring reportPath = imagePath + L".detect.txt";
    std::wstring report;
    auto append = [&](const std::wstring& line) { report += line + L"\r\n"; };

    append(Format(L"图片: %s", imagePath.c_str()));
    append(Format(L"尺寸: %dx%d   缩放系数: %.4f", frame.Width(), frame.Height(), scale));

    // ---- 对话界面识别（左上角"自动播放"按钮模板匹配）----
    SkipAssets assets = SkipAssets::Load(scale);
    append(Format(L"素材来源: %s", assets.sources.empty() ? L"?" : assets.sources[0].c_str()));
    append(Format(L"  disabled_ui 模板: %dx%d", assets.disabledUiButton.Width(),
                  assets.disabledUiButton.Height()));
    append(Format(L"  stop_auto   模板: %dx%d", assets.stopAutoButton.Width(),
                  assets.stopAutoButton.Height()));
    append(Format(L"  icon_option 模板: %dx%d", assets.optionIcon.Width(),
                  assets.optionIcon.Height()));

    {
        GrayImage gray = frame.ToGray();
        const int roiW = std::max(1, frame.Width() / 3);
        const int roiH = std::max(1, frame.Height() / 8);

        double disabledScore = 0.0;
        double stopScore = 0.0;
        if (!assets.disabledUiButton.Empty()) {
            disabledScore = Cv::MatchBestScore(gray, assets.disabledUiButton.ToGray(),
                                               0, 0, roiW, roiH);
        }
        if (!assets.stopAutoButton.Empty()) {
            stopScore = Cv::MatchBestScore(gray, assets.stopAutoButton.ToGray(),
                                           0, 0, roiW, roiH);
        }
        append(Format(L"对话识别（ROI %dx%d，阈值 0.75）:", roiW, roiH));
        append(Format(L"  disabled_ui 最高分 = %.4f", disabledScore));
        append(Format(L"  stop_auto   最高分 = %.4f", stopScore));
        append(Format(L"  => 判定: %s",
                      (disabledScore >= 0.75 || stopScore >= 0.75) ? L"在对话中" : L"不在对话中"));

        // ---- 选项气泡识别 ----
        if (!assets.optionIcon.Empty()) {
            const int ox = frame.Width() / 2;
            const int oy = frame.Height() / 12;
            const int ow = std::max(1, frame.Width() - frame.Width() / 2 - frame.Width() / 6);
            const int oh = std::max(1, frame.Height() - frame.Height() / 12 - 10);
            auto bubbles = Cv::MatchAll(gray, assets.optionIcon.ToGray(), ox, oy, ow, oh, 0.70, 32);
            append(Format(L"选项气泡（ROI %dx%d，阈值 0.70）: 命中 %d 个",
                          ow, oh, (int)bubbles.size()));
        }
    }

    MatchResult m;
    bool found = AutoSkipEngine::DetectBottomTriangleIn(frame, scale, m);
    append(Format(L"底部指示器: %s", found ? L"命中" : L"未命中"));
    append(Format(L"  包围盒 = (%d,%d) %dx%d   金色像素数 = %.0f", m.x, m.y, m.w, m.h, m.score));

    // 把结果标注到图上，便于人工核对
    if (found) {
        frame.DrawRect(m.x, m.y, m.w, m.h, 255, 0, 255, 3);
    }
    std::wstring outPng = imagePath + L".detect.png";
    frame.SavePng(outPng);
    append(L"标注图已保存: " + outPng);

    HANDLE file = CreateFileW(reportPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        const unsigned char bom[3] = {0xEF, 0xBB, 0xBF};
        WriteFile(file, bom, 3, &written, nullptr);
        std::string utf8 = WideToUtf8(report);
        WriteFile(file, utf8.data(), (DWORD)utf8.size(), &written, nullptr);
        CloseHandle(file);
    }

    return found ? 0 : 2;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
    // 让 WIC / ShellExecute 等 COM 组件可用
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // 与清单里的声明保持一致（清单已声明，这里再设一次以防清单被忽略）
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    std::vector<std::wstring> args = GetCommandLineArgs();

    // ---- 测试模式（无需管理员） ----
    if (args.size() >= 3 && args[1] == L"--capture-test") {
        int code = RunCaptureTest(args[2]);
        CoUninitialize();
        return code;
    }
    if (args.size() >= 3 && args[1] == L"--cv-test") {
        int code = RunCvTest(args[2]);
        CoUninitialize();
        return code;
    }
    if (args.size() >= 3 && args[1] == L"--detect-image") {
        // 可选第三个参数：缩放系数（默认按 1920 宽推算）
        double scale = 1.0;
        if (args.size() >= 4) {
            scale = _wtof(args[3].c_str());
        } else {
            Image probe;
            if (Image::LoadFromFile(args[2], probe) && !probe.Empty()) {
                scale = (double)probe.Width() / 1920.0;
            }
        }
        int code = RunDetectImage(args[2], scale);
        CoUninitialize();
        return code;
    }
    if (args.size() >= 3 && args[1] == L"--render-ui") {
        // 可选第三个参数：强制布局 DPI（不传则按窗口所在显示器自动取）
        UINT forcedDpi = 0;
        if (args.size() >= 4) {
            int parsed = _wtoi(args[3].c_str());
            if (parsed >= 72 && parsed <= 480) {
                forcedDpi = (UINT)parsed;
            }
        }
        int code = RunRenderUi(args[2], forcedDpi);
        CoUninitialize();
        return code;
    }

    // ---- 需要管理员权限 ----
    bool skipElevationCheck = HasFlag(args, L"--no-elevation-check");
    if (!skipElevationCheck && !bys::WindowFinder::IsCurrentProcessElevated()) {
        int choice = MessageBoxW(
            nullptr,
            L"本程序需要管理员权限才能向游戏发送模拟输入。\n\n"
            L"如果游戏以管理员身份运行而本程序没有提权，点击游戏将没有任何反应。\n\n"
            L"是否以管理员身份重新启动？",
            L"需要管理员权限", MB_YESNO | MB_ICONWARNING);

        if (choice == IDYES) {
            RelaunchAsAdministrator();
        }
        CoUninitialize();
        return 0;
    }

    bool elevated = bys::WindowFinder::IsCurrentProcessElevated();
    LOG_INFO(Format(L"BetterYuanshen 已启动（自动剧情，管理员权限=%s）",
                    elevated ? L"true" : L"false"));
    if (!elevated) {
        LOG_WARN(L"当前未以管理员身份运行，若游戏提权则输入会被系统拦截");
    }

    // ---- 主窗口与消息循环 ----
    if (!bys::MainWindow::RegisterWindowClass(instance)) {
        MessageBoxW(nullptr, L"注册窗口类失败。", L"启动失败", MB_OK | MB_ICONERROR);
        CoUninitialize();
        return 1;
    }

    bys::MainWindow window;
    if (!window.Create(instance)) {
        MessageBoxW(nullptr, L"创建主窗口失败。", L"启动失败", MB_OK | MB_ICONERROR);
        CoUninitialize();
        return 1;
    }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CoUninitialize();
    return (int)msg.wParam;
}
