#include "input/InputSimulator.h"

#include <algorithm>

namespace bys {

InputSimulator::InputSimulator() : rng_(std::random_device{}()) {}

void InputSimulator::PressKey(WORD virtualKey) {
    INPUT inputs[2] = {};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = virtualKey;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = virtualKey;
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, inputs, sizeof(INPUT));
    SleepInterval();
}

void InputSimulator::ClickAt(int screenX, int screenY) {
    // 把屏幕像素换算成 SendInput 需要的 0..65535 归一化绝对坐标
    const int vLeft = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int vTop = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int vWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int vHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (vWidth <= 1 || vHeight <= 1) {
        return;
    }

    const int absX = (int)((screenX - vLeft) * 65535.0 / (vWidth - 1));
    const int absY = (int)((screenY - vTop) * 65535.0 / (vHeight - 1));

    INPUT inputs[3] = {};
    inputs[0].type = INPUT_MOUSE;
    inputs[0].mi.dx = absX;
    inputs[0].mi.dy = absY;
    inputs[0].mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
    inputs[1].type = INPUT_MOUSE;
    inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    inputs[2].type = INPUT_MOUSE;
    inputs[2].mi.dwFlags = MOUSEEVENTF_LEFTUP;

    SendInput(3, inputs, sizeof(INPUT));
    SleepInterval();
}

void InputSimulator::SleepInterval() {
    int wait = baseIntervalMs_;
    if (jitterMs_ > 0) {
        std::uniform_int_distribution<int> dist(-jitterMs_, jitterMs_);
        wait += dist(rng_);
    }
    if (wait > 0) {
        Sleep((DWORD)wait);
    }
}

}  // namespace bys
