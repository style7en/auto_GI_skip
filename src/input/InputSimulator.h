#pragma once

// 输入模拟。统一走 SendInput（模拟真实硬件输入），不注入进程、不读写游戏内存。

#include <windows.h>

#include <random>

namespace bys {

// 常用虚拟键码
namespace vk {
constexpr WORD Space = VK_SPACE;
constexpr WORD F = 'F';
constexpr WORD W = 'W';
constexpr WORD S = 'S';
constexpr WORD Escape = VK_ESCAPE;
constexpr WORD Enter = VK_RETURN;
constexpr WORD F12 = VK_F12;
}  // namespace vk

class InputSimulator {
public:
    InputSimulator();

    // 按下空格推进对话
    void PressSpace() { PressKey(vk::Space); }

    // 按下交互键（F）
    void PressInteract() { PressKey(vk::F); }

    void PressEscape() { PressKey(vk::Escape); }

    // 按下并抬起指定虚拟键
    void PressKey(WORD virtualKey);

    // 在屏幕绝对坐标点击左键
    void ClickAt(int screenX, int screenY);

    // 两次输入之间的基础间隔与抖动（毫秒）。
    // 游戏对键鼠的响应有 30~60ms 延迟，过快会丢输入。
    void SetInterval(int baseMs, int jitterMs) {
        baseIntervalMs_ = baseMs;
        jitterMs_ = jitterMs;
    }

private:
    void SleepInterval();

    int baseIntervalMs_ = 40;
    int jitterMs_ = 25;
    std::mt19937 rng_;
};

}  // namespace bys
