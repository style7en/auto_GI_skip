#pragma once

// 屏幕捕获抽象。所有坐标均为虚拟桌面像素坐标。

#include <memory>
#include <string>

#include "core/Image.h"

namespace bys {

enum class CaptureBackend {
    Dxgi,  // DXGI Desktop Duplication，可捕获 DirectX 游戏
    Gdi,   // GDI BitBlt，简单但对独占全屏可能黑屏
};

class IScreenCapture {
public:
    virtual ~IScreenCapture() = default;

    // 后端名称，用于界面展示
    virtual const wchar_t* Name() const = 0;

    // 后端是否成功初始化
    virtual bool IsReady() const = 0;

    // 最近一次错误信息
    virtual const std::wstring& LastError() const = 0;

    // 捕获指定屏幕区域。返回 false 表示本帧无可用画面（无新帧或捕获失败）
    virtual bool Capture(int x, int y, int w, int h, Image& out) = 0;

    // 附加诊断信息（DXGI 会返回帧内容标志）
    virtual std::wstring Diagnostics() const { return L"-"; }
};

// 各后端工厂
std::unique_ptr<IScreenCapture> CreateDxgiScreenCapture();
std::unique_ptr<IScreenCapture> CreateGdiScreenCapture();

// 按后端类型创建捕获器；初始化失败时返回的对象 IsReady() 为 false
inline std::unique_ptr<IScreenCapture> CreateScreenCapture(CaptureBackend backend) {
    return backend == CaptureBackend::Gdi ? CreateGdiScreenCapture() : CreateDxgiScreenCapture();
}

}  // namespace bys
