#include "input/InputSimulator.h"

#include <algorithm>
#include <cmath>

namespace bys {

namespace {

// 发一次绝对移动
void SendMove(int absX, int absY) {
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dx = absX;
    input.mi.dy = absY;
    input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
    SendInput(1, &input, sizeof(INPUT));
}

}  // namespace

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

bool InputSimulator::GetCursorScreenPos(int& x, int& y) {
    POINT point{};
    if (GetCursorPos(&point) == FALSE) {
        return false;
    }
    x = point.x;
    y = point.y;
    return true;
}

void InputSimulator::MoveToAbsolute(int screenX, int screenY) {
    // 屏幕像素 -> SendInput 需要的 0..65535 归一化绝对坐标
    const int vLeft = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int vTop = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int vWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int vHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (vWidth <= 1 || vHeight <= 1) {
        return;
    }

    const double scaleX = 65535.0 / (vWidth - 1);
    const double scaleY = 65535.0 / (vHeight - 1);

    int absX = (int)std::lround((screenX - vLeft) * scaleX);
    int absY = (int)std::lround((screenY - vTop) * scaleY);
    absX = std::clamp(absX, 0, 65535);
    absY = std::clamp(absY, 0, 65535);

    SendMove(absX, absY);

    // 归一化换算存在取整误差（最多 1px）。
    // 点击后要把光标精确还原到用户原来的位置，若每次差 1px，
    // 几百次点击后会累积成明显偏移，所以按实测误差校正最多 3 次。
    for (int i = 0; i < 3; ++i) {
        int actualX = 0;
        int actualY = 0;
        if (!GetCursorScreenPos(actualX, actualY)) {
            break;
        }
        const int dx = screenX - actualX;
        const int dy = screenY - actualY;
        if (dx == 0 && dy == 0) {
            break;
        }

        absX = std::clamp(absX + (int)std::lround(dx * scaleX), 0, 65535);
        absY = std::clamp(absY + (int)std::lround(dy * scaleY), 0, 65535);
        SendMove(absX, absY);
    }
}

void InputSimulator::ClickAt(int screenX, int screenY) {
    // 先记下用户当前的光标位置
    int originalX = 0;
    int originalY = 0;
    const bool hasOriginal = GetCursorScreenPos(originalX, originalY);

    MoveToAbsolute(screenX, screenY);

    INPUT inputs[2] = {};
    inputs[0].type = INPUT_MOUSE;
    inputs[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    inputs[1].type = INPUT_MOUSE;
    inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(2, inputs, sizeof(INPUT));

    // 立刻还原光标，避免把用户的鼠标抢走钉在固定位置
    if (hasOriginal) {
        MoveToAbsolute(originalX, originalY);
    }

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
