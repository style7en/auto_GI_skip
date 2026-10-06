#pragma once

// 识别用模板素材。素材以 1920x1080 为基准，加载时按游戏实际分辨率等比缩放。
//
// 查找顺序（两级，便于用户在不重新编译的情况下替换模板）：
//   1. <exe目录>\assets\1920x1080\<文件名>   —— 外部文件优先
//   2. exe 内嵌的 RCDATA 资源                —— 找不到外部文件时回退
//
// 因此 assets 目录是可选的：删掉它程序照样能跑（用内嵌模板）。

#include <string>
#include <vector>

#include "core/Image.h"

namespace bys {

class SkipAssets {
public:
    static constexpr int BaseWidth = 1920;
    static constexpr int BaseHeight = 1080;

    // 按缩放比例加载全部素材。scale 通常为 客户区宽度 / 1920
    static SkipAssets Load(double scale);

    // 对话界面左上角的"自动播放"按钮（未开启状态），用于判断是否在对话中
    Image disabledUiButton;

    // 对话界面左上角的"自动播放"按钮（已开启状态）
    Image stopAutoButton;

    // 对话选项气泡图标（···），用于定位每个选项
    Image optionIcon;

    // 右上角页面关闭按钮
    Image pageClose;

    // 邀约界面的跳过按钮
    Image hangoutSkip;

    // 两级都加载失败的文件名，供界面提示
    std::vector<std::wstring> missingFiles;

    // 各素材的实际来源（"file" / "resource"），用于诊断
    std::vector<std::wstring> sources;

private:
    void LoadOne(const std::wstring& dir, const wchar_t* fileName,
                 int resourceId, double scale, Image& out);
};

}  // namespace bys
