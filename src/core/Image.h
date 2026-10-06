#pragma once

// 图像类型。为了不依赖 OpenCV，这里自己实现最小需要的图像表示：
//   - Image    : 8 位 3 通道 BGR，行连续（stride == width*3）
//   - GrayImage: 8 位单通道灰度，行连续（stride == width）
// PNG 读写走系统自带的 WIC（Windows Imaging Component），无需第三方库。

#include <cstdint>
#include <string>
#include <vector>

namespace bys {

class GrayImage {
public:
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;  // 大小 = width * height

    bool Empty() const { return width <= 0 || height <= 0 || pixels.empty(); }

    uint8_t* Row(int y) { return pixels.data() + (size_t)y * width; }
    const uint8_t* Row(int y) const { return pixels.data() + (size_t)y * width; }

    uint8_t At(int x, int y) const { return pixels[(size_t)y * width + x]; }
};

class Image {
public:
    Image() = default;
    Image(int width, int height);

    int Width() const { return width_; }
    int Height() const { return height_; }
    bool Empty() const { return width_ <= 0 || height_ <= 0 || data_.empty(); }
    size_t Stride() const { return (size_t)width_ * 3; }

    uint8_t* Data() { return data_.data(); }
    const uint8_t* Data() const { return data_.data(); }

    uint8_t* Row(int y) { return data_.data() + (size_t)y * Stride(); }
    const uint8_t* Row(int y) const { return data_.data() + (size_t)y * Stride(); }

    // 从外部 BGRA / BGR 缓冲构造（会去掉 alpha 通道）
    static Image FromBgra(const uint8_t* src, int width, int height, int srcStride);
    static Image FromBgr(const uint8_t* src, int width, int height, int srcStride);

    // 按区域裁剪，自动与图像边界求交；完全越界时返回空图
    Image Crop(int x, int y, int w, int h) const;

    // 双线性缩放
    Image Resize(int newWidth, int newHeight) const;

    // 转灰度（BT.601：Y = 0.114B + 0.587G + 0.299R）
    GrayImage ToGray() const;

    // PNG 读写（WIC）。
    // 注意：加载函数刻意不叫 LoadImage —— Windows 头文件把 LoadImage 定义成了宏
    // （映射到 LoadImageW），同名会冲突。
    bool SavePng(const std::wstring& path) const;
    static bool LoadFromFile(const std::wstring& path, Image& out);

    // 从内存中的 PNG 数据解码（用于读取嵌入到 exe 里的模板资源）
    static bool LoadFromMemory(const void* data, size_t size, Image& out);

    // 调试绘制：画矩形框（thickness 为线宽）
    void DrawRect(int x, int y, int w, int h,
                  uint8_t b, uint8_t g, uint8_t r, int thickness = 2);

    // 平均亮度（0~255），用于判断是否抓到黑帧
    double MeanBrightness() const;

private:
    int width_ = 0;
    int height_ = 0;
    std::vector<uint8_t> data_;
};

}  // namespace bys
