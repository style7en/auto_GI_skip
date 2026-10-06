#pragma once

// 自动剧情配置。

#include "capture/ScreenCapture.h"

namespace bys {

// 对话选项的兜底选择策略（橙色优先不命中时使用）
enum class ChatOptionStrategy {
    Last,    // 优先选择最后一个（最下方）选项
    First,   // 优先选择第一个选项
    Random,  // 随机选择
};

struct AutoSkipConfig {
    // 自动推进对话（按空格）
    bool quicklySkipConversations = true;

    // 推进对话时改用交互键（F）而不是空格
    bool useInteractKey = false;

    // 优先选择橙色选项（通常含奖励或推进剧情）
    bool preferOrangeOption = true;

    // 没有橙色选项时的兜底策略
    ChatOptionStrategy optionStrategy = ChatOptionStrategy::Last;

    // 黑屏剧情自动点击推进（使用鼠标）
    bool blackScreenClickEnabled = true;

    // 自动关闭对话中弹出的页面（使用鼠标点底部三角 / 按 ESC）
    bool autoClosePopup = true;

    // 核心动作的最小间隔（毫秒）
    int actionIntervalMs = 200;

    // 主循环间隔（毫秒）
    int loopIntervalMs = 50;

    // 截图后端
    CaptureBackend captureBackend = CaptureBackend::Dxgi;
};

}  // namespace bys
