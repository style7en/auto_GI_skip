#include "capture/GdiCapture.h"

#include <windows.h>

#include "util/Log.h"

namespace bys {

bool GdiCapture::Capture(int x, int y, int w, int h, Image& out) {
    if (disposed_ || w <= 0 || h <= 0) {
        return false;
    }

    HDC screenDc = nullptr;
    HDC memDc = nullptr;
    HBITMAP dib = nullptr;
    HGDIOBJ oldObject = nullptr;
    void* bits = nullptr;

    // RAII：无论从哪条路径返回都会释放 GDI 对象
    struct Cleanup {
        HDC screenDc;
        HDC memDc;
        HBITMAP dib;
        HGDIOBJ oldObject;
        ~Cleanup() {
            if (oldObject && memDc) {
                SelectObject(memDc, oldObject);
            }
            if (dib) {
                DeleteObject(dib);
            }
            if (memDc) {
                DeleteDC(memDc);
            }
            if (screenDc) {
                ReleaseDC(nullptr, screenDc);
            }
        }
    } cleanup{nullptr, nullptr, nullptr, nullptr};

    screenDc = GetDC(nullptr);
    if (screenDc == nullptr) {
        lastError_ = L"GetDC 失败";
        return false;
    }
    cleanup.screenDc = screenDc;

    memDc = CreateCompatibleDC(screenDc);
    if (memDc == nullptr) {
        lastError_ = L"CreateCompatibleDC 失败";
        return false;
    }
    cleanup.memDc = memDc;

    // 32 位、自上而下（biHeight 为负）的 DIB，内存布局与 BGRA 一致
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = w;
    info.bmiHeader.biHeight = -h;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    info.bmiHeader.biSizeImage = (DWORD)(w * h * 4);

    dib = CreateDIBSection(screenDc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (dib == nullptr || bits == nullptr) {
        lastError_ = L"CreateDIBSection 失败";
        return false;
    }
    cleanup.dib = dib;

    oldObject = SelectObject(memDc, dib);
    cleanup.oldObject = oldObject;

    if (!BitBlt(memDc, 0, 0, w, h, screenDc, x, y, SRCCOPY | CAPTUREBLT)) {
        lastError_ = L"BitBlt 失败";
        return false;
    }

    // DIB 行紧凑排列，行距即 宽度*4
    out = Image::FromBgra(static_cast<const uint8_t*>(bits), w, h, w * 4);
    return !out.Empty();
}

std::unique_ptr<IScreenCapture> CreateGdiScreenCapture() {
    return std::make_unique<GdiCapture>();
}

}  // namespace bys
