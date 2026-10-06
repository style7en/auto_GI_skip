#include "core/Image.h"

#include <windows.h>
#include <wincodec.h>

#include <algorithm>
#include <cmath>

#include "util/ComPtr.h"

namespace bys {

Image::Image(int width, int height) : width_(width), height_(height) {
    if (width_ > 0 && height_ > 0) {
        data_.assign((size_t)width_ * height_ * 3, 0);
    } else {
        width_ = 0;
        height_ = 0;
    }
}

Image Image::FromBgra(const uint8_t* src, int width, int height, int srcStride) {
    Image out(width, height);
    if (out.Empty() || src == nullptr) {
        return out;
    }

    for (int y = 0; y < height; ++y) {
        const uint8_t* s = src + (size_t)y * srcStride;
        uint8_t* d = out.Row(y);
        for (int x = 0; x < width; ++x) {
            d[x * 3 + 0] = s[x * 4 + 0];  // B
            d[x * 3 + 1] = s[x * 4 + 1];  // G
            d[x * 3 + 2] = s[x * 4 + 2];  // R
        }
    }
    return out;
}

Image Image::FromBgr(const uint8_t* src, int width, int height, int srcStride) {
    Image out(width, height);
    if (out.Empty() || src == nullptr) {
        return out;
    }

    size_t rowBytes = (size_t)width * 3;
    for (int y = 0; y < height; ++y) {
        const uint8_t* s = src + (size_t)y * srcStride;
        uint8_t* d = out.Row(y);
        if (srcStride == (int)rowBytes) {
            std::copy(s, s + rowBytes, d);
        } else {
            std::copy(s, s + rowBytes, d);
        }
    }
    return out;
}

Image Image::Crop(int x, int y, int w, int h) const {
    if (Empty() || w <= 0 || h <= 0) {
        return Image();
    }

    int x0 = std::max(0, x);
    int y0 = std::max(0, y);
    int x1 = std::min(width_, x + w);
    int y1 = std::min(height_, y + h);
    int cw = x1 - x0;
    int ch = y1 - y0;
    if (cw <= 0 || ch <= 0) {
        return Image();
    }

    Image out(cw, ch);
    for (int row = 0; row < ch; ++row) {
        const uint8_t* s = Row(y0 + row) + (size_t)x0 * 3;
        std::copy(s, s + (size_t)cw * 3, out.Row(row));
    }
    return out;
}

Image Image::Resize(int newWidth, int newHeight) const {
    if (Empty() || newWidth <= 0 || newHeight <= 0) {
        return Image();
    }
    if (newWidth == width_ && newHeight == height_) {
        return *this;
    }

    Image out(newWidth, newHeight);

    // 缩小用区域平均（避免锯齿导致匹配率下降），放大用双线性
    bool downscale = newWidth < width_ || newHeight < height_;

    if (downscale) {
        double sx = (double)width_ / newWidth;
        double sy = (double)height_ / newHeight;
        for (int y = 0; y < newHeight; ++y) {
            int sy0 = (int)(y * sy);
            int sy1 = std::min(height_, (int)((y + 1) * sy));
            if (sy1 <= sy0) sy1 = sy0 + 1;
            for (int x = 0; x < newWidth; ++x) {
                int sx0 = (int)(x * sx);
                int sx1 = std::min(width_, (int)((x + 1) * sx));
                if (sx1 <= sx0) sx1 = sx0 + 1;

                uint32_t sum[3] = {0, 0, 0};
                int count = 0;
                for (int yy = sy0; yy < sy1; ++yy) {
                    const uint8_t* row = Row(yy);
                    for (int xx = sx0; xx < sx1; ++xx) {
                        sum[0] += row[xx * 3 + 0];
                        sum[1] += row[xx * 3 + 1];
                        sum[2] += row[xx * 3 + 2];
                        ++count;
                    }
                }
                uint8_t* d = out.Row(y) + (size_t)x * 3;
                for (int c = 0; c < 3; ++c) {
                    d[c] = (uint8_t)(count > 0 ? sum[c] / count : 0);
                }
            }
        }
        return out;
    }

    // 双线性放大
    double sx = (double)(width_ - 1) / std::max(1, newWidth - 1);
    double sy = (double)(height_ - 1) / std::max(1, newHeight - 1);
    for (int y = 0; y < newHeight; ++y) {
        double fy = y * sy;
        int y0 = (int)fy;
        int y1 = std::min(height_ - 1, y0 + 1);
        double wy = fy - y0;
        for (int x = 0; x < newWidth; ++x) {
            double fx = x * sx;
            int x0 = (int)fx;
            int x1 = std::min(width_ - 1, x0 + 1);
            double wx = fx - x0;

            const uint8_t* p00 = Row(y0) + (size_t)x0 * 3;
            const uint8_t* p01 = Row(y0) + (size_t)x1 * 3;
            const uint8_t* p10 = Row(y1) + (size_t)x0 * 3;
            const uint8_t* p11 = Row(y1) + (size_t)x1 * 3;
            uint8_t* d = out.Row(y) + (size_t)x * 3;

            for (int c = 0; c < 3; ++c) {
                double top = p00[c] * (1 - wx) + p01[c] * wx;
                double bottom = p10[c] * (1 - wx) + p11[c] * wx;
                double value = top * (1 - wy) + bottom * wy;
                d[c] = (uint8_t)std::clamp(value, 0.0, 255.0);
            }
        }
    }
    return out;
}

GrayImage Image::ToGray() const {
    GrayImage out;
    if (Empty()) {
        return out;
    }

    out.width = width_;
    out.height = height_;
    out.pixels.resize((size_t)width_ * height_);

    for (int y = 0; y < height_; ++y) {
        const uint8_t* s = Row(y);
        uint8_t* d = out.Row(y);
        for (int x = 0; x < width_; ++x) {
            // BT.601：Y = 0.114*B + 0.587*G + 0.299*R
            int v = (114 * s[x * 3 + 0] + 587 * s[x * 3 + 1] + 299 * s[x * 3 + 2]) / 1000;
            d[x] = (uint8_t)std::clamp(v, 0, 255);
        }
    }
    return out;
}

bool Image::SavePng(const std::wstring& path) const {
    if (Empty()) {
        return false;
    }

    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                __uuidof(IWICImagingFactory),
                                (void**)factory.GetAddressOf()))) {
        return false;
    }

    ComPtr<IWICStream> stream;
    if (FAILED(factory->CreateStream(stream.GetAddressOf()))) return false;
    if (FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE))) return false;

    ComPtr<IWICBitmapEncoder> encoder;
    if (FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr,
                                      encoder.GetAddressOf()))) {
        return false;
    }
    if (FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache))) return false;

    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> props;
    if (FAILED(encoder->CreateNewFrame(frame.GetAddressOf(), props.GetAddressOf()))) return false;
    if (FAILED(frame->Initialize(props.Get()))) return false;
    if (FAILED(frame->SetSize((UINT)width_, (UINT)height_))) return false;

    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
    if (FAILED(frame->SetPixelFormat(&format))) return false;

    UINT stride = (UINT)Stride();
    UINT size = (UINT)(stride * (UINT)height_);
    if (FAILED(frame->WritePixels((UINT)height_, stride, size, const_cast<uint8_t*>(Data())))) {
        return false;
    }

    if (FAILED(frame->Commit())) return false;
    if (FAILED(encoder->Commit())) return false;
    return true;
}

namespace {

// 把解码器的一帧转换成 24bppBGR 的 Image
bool DecodeFirstFrame(IWICImagingFactory* factory, IWICBitmapDecoder* decoder, Image& out) {
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, frame.GetAddressOf()))) {
        return false;
    }

    ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(converter.GetAddressOf()))) {
        return false;
    }
    if (FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat24bppBGR,
                                     WICBitmapDitherTypeNone, nullptr, 0.0,
                                     WICBitmapPaletteTypeCustom))) {
        return false;
    }

    UINT w = 0;
    UINT h = 0;
    if (FAILED(converter->GetSize(&w, &h)) || w == 0 || h == 0) {
        return false;
    }

    out = Image((int)w, (int)h);
    if (out.Empty()) {
        return false;
    }

    UINT stride = (UINT)out.Stride();
    UINT size = stride * h;
    return SUCCEEDED(converter->CopyPixels(nullptr, stride, size, out.Data()));
}

ComPtr<IWICImagingFactory> CreateWicFactory() {
    ComPtr<IWICImagingFactory> factory;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                     __uuidof(IWICImagingFactory), (void**)factory.GetAddressOf());
    return factory;
}

}  // namespace

bool Image::LoadFromFile(const std::wstring& path, Image& out) {
    ComPtr<IWICImagingFactory> factory = CreateWicFactory();
    if (!factory) {
        return false;
    }

    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                  WICDecodeMetadataCacheOnLoad,
                                                  decoder.GetAddressOf()))) {
        return false;
    }

    return DecodeFirstFrame(factory.Get(), decoder.Get(), out);
}

bool Image::LoadFromMemory(const void* data, size_t size, Image& out) {
    if (data == nullptr || size == 0 || size > 0xFFFFFFFFull) {
        return false;
    }

    ComPtr<IWICImagingFactory> factory = CreateWicFactory();
    if (!factory) {
        return false;
    }

    ComPtr<IWICStream> stream;
    if (FAILED(factory->CreateStream(stream.GetAddressOf()))) {
        return false;
    }
    // InitializeFromMemory 不拷贝数据，调用期间 data 必须保持有效
    if (FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(static_cast<const BYTE*>(data)),
                                            (DWORD)size))) {
        return false;
    }

    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr,
                                                WICDecodeMetadataCacheOnLoad,
                                                decoder.GetAddressOf()))) {
        return false;
    }

    return DecodeFirstFrame(factory.Get(), decoder.Get(), out);
}

void Image::DrawRect(int x, int y, int w, int h,
                     uint8_t b, uint8_t g, uint8_t r, int thickness) {
    if (Empty() || w <= 0 || h <= 0) {
        return;
    }

    auto put = [&](int px, int py) {
        if (px < 0 || py < 0 || px >= width_ || py >= height_) return;
        uint8_t* p = Row(py) + (size_t)px * 3;
        p[0] = b;
        p[1] = g;
        p[2] = r;
    };

    for (int t = 0; t < thickness; ++t) {
        for (int i = x; i < x + w; ++i) {
            put(i, y + t);
            put(i, y + h - 1 - t);
        }
        for (int i = y; i < y + h; ++i) {
            put(x + t, i);
            put(x + w - 1 - t, i);
        }
    }
}

double Image::MeanBrightness() const {
    if (Empty()) {
        return 0.0;
    }

    GrayImage gray = ToGray();
    uint64_t sum = 0;
    for (uint8_t v : gray.pixels) {
        sum += v;
    }
    return gray.pixels.empty() ? 0.0 : (double)sum / gray.pixels.size();
}

}  // namespace bys
