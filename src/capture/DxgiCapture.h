#pragma once

// DXGI Desktop Duplication 主捕获后端。
// 直接读取显示输出，可稳定捕获 DirectX 游戏，不受 DWM 合成限制。

#include <d3d11.h>
#include <dxgi1_2.h>

#include <string>

#include "capture/ScreenCapture.h"
#include "util/ComPtr.h"

namespace bys {

class DxgiCapture : public IScreenCapture {
public:
    DxgiCapture();
    ~DxgiCapture() override;

    DxgiCapture(const DxgiCapture&) = delete;
    DxgiCapture& operator=(const DxgiCapture&) = delete;

    const wchar_t* Name() const override { return L"DXGI Desktop Duplication"; }
    bool IsReady() const override { return duplication_ != nullptr; }
    const std::wstring& LastError() const override { return lastError_; }
    bool Capture(int x, int y, int w, int h, Image& out) override;
    std::wstring Diagnostics() const override { return frameInfo_; }

private:
    bool Initialize();
    void ReleaseDeviceResources();
    void Reinitialize();

    // 抓取一整帧输出画面。返回 false 表示本次没有新内容（应复用缓存）
    bool TryAcquireFullFrame(Image& out);

    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGIOutputDuplication> duplication_;

    // 被复制输出在虚拟桌面中的左上角坐标
    int outputLeft_ = 0;
    int outputTop_ = 0;

    // 整屏缓存：DXGI 只在画面变化时交付新帧，其余时刻复用这一帧
    Image cached_;
    bool hasCached_ = false;

    std::wstring lastError_;
    std::wstring frameInfo_ = L"-";
    bool disposed_ = false;
};

}  // namespace bys
