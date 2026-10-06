#pragma once

// 最小视觉算法集。为不依赖 OpenCV，这里自行实现用到的几个函数：
//   - 模板匹配（归一化相关系数 CCoeffNormed），多线程
//   - 橙色像素占比（HSV 阈值，用于判断对话选项是否为橙色）
//   - 暗色像素占比（用于黑屏剧情判定）
//   - 连通域标记（替代 findContours，用于识别底部实心三角）

#include <vector>

#include "core/Image.h"

namespace bys {

struct MatchResult {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    double score = 0.0;

    int CenterX() const { return x + w / 2; }
    int CenterY() const { return y + h / 2; }
};

struct Blob {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    int area = 0;
};

class Cv {
public:
    // 在 source 的指定区域内查找模板的最佳匹配位置。
    // 低于阈值返回 false。
    static bool MatchBest(const GrayImage& source, const GrayImage& templ,
                          int roiX, int roiY, int roiW, int roiH,
                          double threshold, MatchResult& out);

    // 返回区域内与模板的最高匹配分数（0~1），用于诊断"差多少"
    static double MatchBestScore(const GrayImage& source, const GrayImage& templ,
                                 int roiX, int roiY, int roiW, int roiH);

    // 查找所有匹配：阈值筛选 + 局部极大值 + 非极大值抑制
    static std::vector<MatchResult> MatchAll(const GrayImage& source, const GrayImage& templ,
                                             int roiX, int roiY, int roiW, int roiH,
                                             double threshold, int maxResults = 32);

    // 区域内橙色像素占比是否超过阈值
    static bool IsOrangeRegion(const Image& region, double minRatio = 0.06);

    // 生成 HSV 范围掩码（H∈[0,180]，S,V∈[0,255]）；命中像素为 255，其余为 0。
    // 用于底部实心三角的黄/蓝双色判定。
    static GrayImage MaskHsvRange(const Image& bgr,
                                  int hLo, int sLo, int vLo,
                                  int hHi, int sHi, int vHi);

    // 区域内暗色（近黑）像素占比
    static double DarkRatio(const GrayImage& gray, int threshold = 30);

    // 连通域标记（8 邻接）
    static std::vector<Blob> FindBlobs(const GrayImage& mask);
};

}  // namespace bys
