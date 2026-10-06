#pragma once

// 主窗口。Win32 原生实现：CreateWindowEx 建控件，WM_COMMAND 处理交互，
// 定时器轮询引擎状态刷新界面。
//
// 视觉风格：窗口背景浅灰，内容用自绘的白色圆角"卡片"承载，
// 静态控件背景色按所处卡片动态返回，避免出现灰色色块。

#include <windows.h>

#include <string>

#include "autoskip/AutoSkipEngine.h"
#include "input/KeyboardHook.h"

namespace bys {

class MainWindow {
public:
    MainWindow() = default;
    ~MainWindow();

    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    // 注册窗口类（返回 false 表示注册失败）
    static bool RegisterWindowClass(HINSTANCE instance);

    // 创建并显示主窗口。
    // offscreen=true 时把窗口放到屏幕外（用于 --render-ui 离屏渲染截图），
    // 不打扰正在使用电脑的用户。
    // forcedDpi > 0 时强制使用该 DPI 布局（用于在固定缩放下验证界面）。
    bool Create(HINSTANCE instance, bool offscreen = false, UINT forcedDpi = 0);

    // 在指定毫秒内泵消息（让布局与绘制完成）
    void PumpFor(int milliseconds);

    HWND Handle() const { return hwnd_; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    void OnCreate();
    void OnCommand(int controlId, int notifyCode);
    void OnPaint();
    void OnDpiChanged(UINT newDpi, const RECT* suggested);
    void OnDestroy();

    int Dp(int value) const;

    // 按当前 DPI 创建/重建字体
    void CreateFonts();
    void DestroyFonts();

    // 创建控件（位置由 Relayout 统一设置）
    void CreateControls();

    // 按当前 DPI 重新计算卡片区域与所有控件位置
    void Relayout();

    // 把当前字体应用到所有控件（DPI 变化后需重新调用）
    void ApplyFontsToControls();

    // 把窗口客户区调整成 kClientWidth x kClientHeight 逻辑尺寸
    void ResizeClientToLogical();

    // 取窗口当前所在显示器的 DPI
    UINT QueryWindowDpi() const;

    void RefreshState();
    void ToggleRunning();
    void SyncConfigToUi();

    // 判断某个控件是否落在某张卡片内（用于决定静态控件的背景色）
    bool IsInsideRect(HWND control, const RECT& rect) const;

    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;

    AutoSkipEngine engine_;
    KeyboardHook hotkey_;

    HFONT fontNormal_ = nullptr;
    HFONT fontTitle_ = nullptr;
    HFONT fontSection_ = nullptr;
    HFONT fontButton_ = nullptr;
    HFONT fontHotkey_ = nullptr;

    HBRUSH windowBrush_ = nullptr;
    HBRUSH cardBrush_ = nullptr;

    UINT dpi_ = 96;

    // 强制布局 DPI（0 表示按窗口所在显示器自动取值）
    UINT forcedDpi_ = 0;

    // 实际使用的窗口样式。离屏渲染时用 WS_POPUP（无标题栏边框），
    // 这样客户区尺寸与请求值完全相等，渲染结果才能精确反映布局。
    DWORD style_ = 0;
    DWORD exStyle_ = 0;

    // 卡片区域（客户区坐标）
    RECT hotkeyCardRect_{};
    RECT statusCardRect_{};
    RECT settingsCardRect_{};

    // 控件句柄
    HWND titleLabel_ = nullptr;
    HWND subtitleLabel_ = nullptr;
    HWND rightHintLabel_ = nullptr;
    HWND startStopButton_ = nullptr;
    HWND hotkeyLabel1_ = nullptr;
    HWND hotkeyLabel2_ = nullptr;

    HWND btnSaveFrame_ = nullptr;
    HWND btnSaveReport_ = nullptr;

    struct StatusRow {
        HWND label1 = nullptr;
        HWND value1 = nullptr;
        HWND label2 = nullptr;
        HWND value2 = nullptr;
    };
    StatusRow statusRows_[5] = {};

    HWND chkQuickSkip_ = nullptr;
    HWND chkInteractKey_ = nullptr;
    HWND chkPreferOrange_ = nullptr;
    HWND chkBlackScreen_ = nullptr;
    HWND chkClosePopup_ = nullptr;
    HWND labelStrategy_ = nullptr;
    HWND comboStrategy_ = nullptr;
    HWND labelBackend_ = nullptr;
    HWND comboBackend_ = nullptr;

    HWND logPathLabel_ = nullptr;
    HWND logPathValue_ = nullptr;
    HWND btnOpenLog_ = nullptr;

    // 防止程序化改控件时触发 WM_COMMAND 回写配置
    bool suppressCommand_ = false;

    // 启动/停止按钮上次绘制的状态：-1 未知、0 停止、1 运行。
    // 只在状态真正变化时重绘，否则 250ms 定时器刷新会让按钮一直闪。
    int lastButtonState_ = -1;
};

}  // namespace bys
