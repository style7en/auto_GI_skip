#include "capture/DxgiCapture.h"

#include <algorithm>
#include <vector>

#include "util/Log.h"
#include "util/StringUtil.h"

namespace bys {

namespace {

// 结构体判等：用于判断输出是否为主显示器
bool IsPrimaryOutput(const DXGI_OUTPUT_DESC& desc) {
    return desc.DesktopCoordinates.left == 0 && desc.DesktopCoordinates.top == 0;
}

}  // namespace

DxgiCapture::DxgiCapture() {
    try {
        if (!Initialize()) {
            ReleaseDeviceResources();
        }
    } catch (...) {
        lastError_ = L"初始化 DXGI 捕获时发生未知异常";
        ReleaseDeviceResources();
    }
}

DxgiCapture::~DxgiCapture() {
    disposed_ = true;
    ReleaseDeviceResources();
}

bool DxgiCapture::Initialize() {
    ComPtr<IDXGIFactory1> factory;
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1),
                                    (void**)factory.GetAddressOf());
    if (FAILED(hr)) {
        lastError_ = Format(L"CreateDXGIFactory1 失败 hr=0x%08lX", (unsigned long)hr);
        return false;
    }

    // 收集所有 (adapter, output) 组合
    struct OutputEntry {
        IDXGIAdapter1* adapter = nullptr;
        IDXGIOutput* output = nullptr;
    };
    std::vector<OutputEntry> outputs;

    for (UINT ai = 0;; ++ai) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(ai, adapter.GetAddressOf()) == DXGI_ERROR_NOT_FOUND) {
            break;
        }

        for (UINT oi = 0;; ++oi) {
            ComPtr<IDXGIOutput> output;
            if (adapter->EnumOutputs(oi, output.GetAddressOf()) == DXGI_ERROR_NOT_FOUND) {
                break;
            }
            // 保持引用：后面创建设备与 duplication 需要它们存活
            OutputEntry entry;
            adapter.Get()->AddRef();
            output.Get()->AddRef();
            entry.adapter = adapter.Get();
            entry.output = output.Get();
            outputs.push_back(entry);
        }
    }

    if (outputs.empty()) {
        lastError_ = L"未找到可用的显示输出";
        return false;
    }

    // 优先主显示器（桌面坐标左上角为 0,0）
    size_t chosen = 0;
    for (size_t i = 0; i < outputs.size(); ++i) {
        DXGI_OUTPUT_DESC desc{};
        if (SUCCEEDED(outputs[i].output->GetDesc(&desc)) && IsPrimaryOutput(desc)) {
            chosen = i;
            break;
        }
    }

    DXGI_OUTPUT_DESC desc{};
    outputs[chosen].output->GetDesc(&desc);
    outputLeft_ = desc.DesktopCoordinates.left;
    outputTop_ = desc.DesktopCoordinates.top;

    // 创建 D3D11 设备。
    // 注意：传入了显式 adapter 时，DriverType 必须是 UNKNOWN，
    // 传 D3D_DRIVER_TYPE_HARDWARE 会返回 E_INVALIDARG。
    D3D_FEATURE_LEVEL levels11[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL levels10[] = {D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL obtained{};

    HRESULT devHr = D3D11CreateDevice(
        outputs[chosen].adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        levels11, (UINT)(sizeof(levels11) / sizeof(levels11[0])),
        D3D11_SDK_VERSION, device_.GetAddressOf(), &obtained, context_.GetAddressOf());

    // 请求 FeatureLevel 11_1 在部分系统上会返回 E_INVALIDARG，退回只请求 11_0
    if (FAILED(devHr)) {
        device_.Reset();
        context_.Reset();
        devHr = D3D11CreateDevice(
            outputs[chosen].adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            levels10, (UINT)(sizeof(levels10) / sizeof(levels10[0])),
            D3D11_SDK_VERSION, device_.GetAddressOf(), &obtained, context_.GetAddressOf());
    }

    for (auto& entry : outputs) {
        entry.adapter->Release();
        entry.output->Release();
    }
    outputs.clear();

    if (FAILED(devHr) || !device_ || !context_) {
        lastError_ = Format(L"创建 D3D11 设备失败 hr=0x%08lX", (unsigned long)devHr);
        return false;
    }

    // 重新取一次 output 用于创建 duplication（上面已释放）
    ComPtr<IDXGIAdapter1> adapter;
    ComPtr<IDXGIOutput> output;
    ComPtr<IDXGIOutput1> output1;

    if (FAILED(factory->EnumAdapters1(0, adapter.GetAddressOf()))) {
        lastError_ = L"枚举适配器失败";
        return false;
    }
    if (FAILED(adapter->EnumOutputs(0, output.GetAddressOf()))) {
        lastError_ = L"枚举输出失败";
        return false;
    }
    if (FAILED(output->QueryInterface(__uuidof(IDXGIOutput1), (void**)output1.GetAddressOf()))) {
        lastError_ = L"获取 IDXGIOutput1 失败（系统可能低于 Windows 8）";
        return false;
    }
    if (FAILED(output1->DuplicateOutput(device_.Get(), duplication_.GetAddressOf()))) {
        lastError_ = L"DuplicateOutput 失败（可能已有其他程序占用桌面复制）";
        return false;
    }

    LOG_INFO(Format(L"DXGI 捕获已就绪，输出 %d,%d %ldx%ld",
                    outputLeft_, outputTop_,
                    desc.DesktopCoordinates.right - desc.DesktopCoordinates.left,
                    desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top));
    return true;
}

void DxgiCapture::ReleaseDeviceResources() {
    duplication_.Reset();
    context_.Reset();
    device_.Reset();
}

void DxgiCapture::Reinitialize() {
    ReleaseDeviceResources();
    lastError_.clear();
    if (!Initialize()) {
        LOG_WARN(L"DXGI 重建失败：" + lastError_);
    }
}

bool DxgiCapture::TryAcquireFullFrame(Image& out) {
    DXGI_OUTDUPL_FRAME_INFO frameInfo{};
    ComPtr<IDXGIResource> desktopResource;

    HRESULT hr = duplication_->AcquireNextFrame(200, &frameInfo, desktopResource.GetAddressOf());
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        frameInfo_ = L"timeout";
        return false;  // 画面无变化，属于正常情况
    }
    if (FAILED(hr)) {
        frameInfo_ = Format(L"acquire-failed(0x%08lX)", (unsigned long)hr);
        return false;
    }

    // 无论走哪条分支，离开前都要 ReleaseFrame
    struct FrameGuard {
        IDXGIOutputDuplication* dup;
        ~FrameGuard() { dup->ReleaseFrame(); }
    } guard{duplication_.Get()};

    if (!desktopResource) {
        frameInfo_ = L"null-resource";
        return false;
    }

    // 关键：LastPresentTime 与 AccumulatedFrames 同时为 0 表示桌面没有新内容。
    // 此时 DXGI 仍会返回一帧，但表面内容未初始化（通常是全黑），必须丢弃。
    frameInfo_ = Format(L"present=%lld acc=%u mouse=%lld",
                        (long long)frameInfo.LastPresentTime.QuadPart,
                        frameInfo.AccumulatedFrames,
                        (long long)frameInfo.LastMouseUpdateTime.QuadPart);

    if (frameInfo.LastPresentTime.QuadPart == 0 && frameInfo.AccumulatedFrames == 0) {
        return false;
    }

    ComPtr<ID3D11Texture2D> texture;
    if (FAILED(desktopResource->QueryInterface(__uuidof(ID3D11Texture2D),
                                               (void**)texture.GetAddressOf()))) {
        frameInfo_ += L" qi-failed";
        return false;
    }

    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    const int width = (int)desc.Width;
    const int height = (int)desc.Height;

    // 拷到可 CPU 读取的 staging 纹理
    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.BindFlags = 0;
    stagingDesc.MiscFlags = 0;
    stagingDesc.ArraySize = 1;
    stagingDesc.MipLevels = 1;

    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(device_->CreateTexture2D(&stagingDesc, nullptr, staging.GetAddressOf()))) {
        frameInfo_ += L" staging-failed";
        return false;
    }

    context_->CopyResource(staging.Get(), texture.Get());

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        frameInfo_ += L" map-failed";
        return false;
    }

    out = Image::FromBgra(static_cast<const uint8_t*>(mapped.pData), width, height,
                          (int)mapped.RowPitch);
    context_->Unmap(staging.Get(), 0);
    return true;
}

bool DxgiCapture::Capture(int x, int y, int w, int h, Image& out) {
    if (disposed_ || !duplication_ || !device_ || !context_) {
        return false;
    }

    try {
        Image full;
        if (TryAcquireFullFrame(full)) {
            cached_ = std::move(full);
            hasCached_ = true;
        } else if (!hasCached_) {
            return false;  // 还没有任何缓存帧，本帧放弃
        }

        // 屏幕坐标 -> 输出内局部坐标
        int localX = x - outputLeft_;
        int localY = y - outputTop_;
        out = cached_.Crop(localX, localY, w, h);
        return !out.Empty();
    } catch (...) {
        lastError_ = L"捕获过程中发生异常";
        return false;
    }
}

std::unique_ptr<IScreenCapture> CreateDxgiScreenCapture() {
    return std::make_unique<DxgiCapture>();
}

}  // namespace bys
