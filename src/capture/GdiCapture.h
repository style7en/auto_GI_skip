#pragma once

// GDI BitBlt 兜底捕获后端。
// 使用 CreateDIBSection 直接拿到像素指针，绕开 GDI+ 的位图转换。
// 适用于窗口化 / 无边框窗口模式；独占全屏的 DirectX 游戏可能返回黑帧。

#include <string>

#include "capture/ScreenCapture.h"

namespace bys {

class GdiCapture : public IScreenCapture {
public:
    GdiCapture() = default;
    ~GdiCapture() override = default;

    const wchar_t* Name() const override { return L"GDI BitBlt"; }
    bool IsReady() const override { return !disposed_; }
    const std::wstring& LastError() const override { return lastError_; }
    bool Capture(int x, int y, int w, int h, Image& out) override;

private:
    std::wstring lastError_;
    bool disposed_ = false;
};

}  // namespace bys
