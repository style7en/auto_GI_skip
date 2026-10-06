#include "input/KeyboardHook.h"

#include "util/Log.h"
#include "util/StringUtil.h"

namespace bys {

KeyboardHook* KeyboardHook::instance_ = nullptr;

KeyboardHook::~KeyboardHook() {
    Uninstall();
}

bool KeyboardHook::Install(HWND notifyWindow, UINT message) {
    if (hook_ != nullptr) {
        return true;
    }

    notifyWindow_ = notifyWindow;
    notifyMessage_ = message;
    instance_ = this;

    // 低级键盘钩子不需要模块句柄，但传 GetModuleHandle(nullptr) 兼容性更好
    hook_ = SetWindowsHookExW(WH_KEYBOARD_LL, HookProc, GetModuleHandleW(nullptr), 0);
    if (hook_ == nullptr) {
        instance_ = nullptr;
        LOG_WARN(Format(L"安装 F12 键盘钩子失败，错误码 %lu", GetLastError()));
        return false;
    }

    LOG_INFO(L"已注册全局热键 F12（启动 / 停止自动剧情）");
    return true;
}

void KeyboardHook::Uninstall() {
    if (hook_ != nullptr) {
        UnhookWindowsHookEx(hook_);
        hook_ = nullptr;
    }
    if (instance_ == this) {
        instance_ = nullptr;
    }
}

LRESULT CALLBACK KeyboardHook::HookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    // 注意：这里绝不能做耗时操作，否则钩子会被系统移除。
    // 只判断按键并 PostMessage 给 UI 线程。
    if (nCode >= 0 && instance_ != nullptr) {
        auto* data = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);

        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
            if (data->vkCode == VK_F12 && !instance_->f12Down_) {
                instance_->f12Down_ = true;
                if (instance_->notifyWindow_ != nullptr) {
                    PostMessageW(instance_->notifyWindow_, instance_->notifyMessage_, 0, 0);
                }
            }
        } else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
            if (data->vkCode == VK_F12) {
                instance_->f12Down_ = false;
            }
        }
    }

    return CallNextHookEx(instance_ != nullptr ? instance_->hook_ : nullptr, nCode, wParam, lParam);
}

}  // namespace bys
