#pragma once

// 低级键盘钩子，用于捕获 F12。
//
// 为什么不用 RegisterHotKey：F12 被 Windows 保留给内核调试器，
// RegisterHotKey 注册 F12 必然失败（返回 false）。
//
// 钩子回调必须立刻返回：超过系统的 LowLevelHooksTimeout 会导致钩子被静默移除，
// 因此这里只向目标窗口 PostMessage，实际处理在 UI 线程完成。

#include <windows.h>

namespace bys {

class KeyboardHook {
public:
    KeyboardHook() = default;
    ~KeyboardHook();

    KeyboardHook(const KeyboardHook&) = delete;
    KeyboardHook& operator=(const KeyboardHook&) = delete;

    // 安装钩子。捕获到 F12 按下时向 notifyWindow 发送 message
    bool Install(HWND notifyWindow, UINT message);

    void Uninstall();

    bool IsInstalled() const { return hook_ != nullptr; }

private:
    static LRESULT CALLBACK HookProc(int nCode, WPARAM wParam, LPARAM lParam);

    static KeyboardHook* instance_;

    HHOOK hook_ = nullptr;
    HWND notifyWindow_ = nullptr;
    UINT notifyMessage_ = 0;

    // 用于过滤长按重复触发
    bool f12Down_ = false;
};

}  // namespace bys
