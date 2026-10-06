#include "core/Cv.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <thread>

namespace bys {

namespace {

// 归一化相关系数（CCoeffNormed）结果图计算所需：整幅 ROI 的积分图
struct IntegralImages {
    int width = 0;
    int height = 0;
    std::vector<long long> sum1;   // 灰度和
    std::vector<long long> sum2;   // 灰度平方和

    long long Sum1(int x, int y, int w, int h) const {
        int W = width + 1;
        return sum1[(size_t)(y + h) * W + (x + w)] - sum1[(size_t)y * W + (x + w)]
             - sum1[(size_t)(y + h) * W + x] + sum1[(size_t)y * W + x];
    }

    long long Sum2(int x, int y, int w, int h) const {
        int W = width + 1;
        return sum2[(size_t)(y + h) * W + (x + w)] - sum2[(size_t)y * W + (x + w)]
             - sum2[(size_t)(y + h) * W + x] + sum2[(size_t)y * W + x];
    }
};

IntegralImages BuildIntegral(const GrayImage& img) {
    IntegralImages out;
    out.width = img.width;
    out.height = img.height;
    int W = img.width + 1;
    int H = img.height + 1;
    out.sum1.assign((size_t)W * H, 0);
    out.sum2.assign((size_t)W * H, 0);

    for (int y = 0; y < img.height; ++y) {
        const uint8_t* row = img.Row(y);
        long long rowSum = 0;
        long long rowSum2 = 0;
        for (int x = 0; x < img.width; ++x) {
            int v = row[x];
            rowSum += v;
            rowSum2 += (long long)v * v;
            out.sum1[(size_t)(y + 1) * W + (x + 1)] = out.sum1[(size_t)y * W + (x + 1)] + rowSum;
            out.sum2[(size_t)(y + 1) * W + (x + 1)] = out.sum2[(size_t)y * W + (x + 1)] + rowSum2;
        }
    }
    return out;
}

// 模板统计量。
// 内层用原始整数模板做点积，分子 = ΣT·I - (ΣI·ΣT)/N（整数点积减去常数项），
// 这样避免"模板先减均值再取整"引入的精度损失（实测会让满匹配只得到 0.95）。
struct TemplateStats {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> values;  // 原始灰度
    long long sum = 0;            // Σ T
    double varSum = 0.0;          // Σ T² - (ΣT)²/N，即 Σ(T-T̄)²

    const uint8_t* Row(int y) const { return values.data() + (size_t)y * width; }
};

TemplateStats BuildTemplateStats(const GrayImage& tpl) {
    TemplateStats out;
    out.width = tpl.width;
    out.height = tpl.height;
    out.values = tpl.pixels;

    long long sum = 0;
    long long sumSq = 0;
    for (uint8_t v : tpl.pixels) {
        sum += v;
        sumSq += (long long)v * v;
    }
    out.sum = sum;
    const double n = (double)tpl.pixels.size();
    out.varSum = (double)sumSq - (double)sum * (double)sum / n;
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// 模板匹配
// ---------------------------------------------------------------------------

namespace {

// 计算整幅 ROI 的匹配分数图。result 尺寸为 (roiW-tw+1) x (roiH-th+1)
void ComputeMatchMap(const GrayImage& roi, const TemplateStats& tpl,
                     std::vector<float>& result, int& resW, int& resH) {
    resW = roi.width - tpl.width + 1;
    resH = roi.height - tpl.height + 1;
    result.assign((size_t)resW * resH, -1.0f);
    if (resW <= 0 || resH <= 0) {
        return;
    }

    IntegralImages integral = BuildIntegral(roi);

    const int tw = tpl.width;
    const int th = tpl.height;
    const double N = (double)tw * th;
    const double varT = tpl.varSum;
    const double sumT = (double)tpl.sum;

    auto work = [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            float* dst = result.data() + (size_t)y * resW;
            for (int x = 0; x < resW; ++x) {
                // 精确整数点积 Σ T·I
                long long dot = 0;
                for (int ty = 0; ty < th; ++ty) {
                    const uint8_t* s = roi.Row(y + ty) + x;
                    const uint8_t* t = tpl.Row(ty);
                    for (int tx = 0; tx < tw; ++tx) {
                        dot += (long long)t[tx] * s[tx];
                    }
                }

                // 窗口统计量（来自积分图，精确整数）
                long long s1 = integral.Sum1(x, y, tw, th);
                long long s2 = integral.Sum2(x, y, tw, th);

                double varI = (double)s2 - (double)s1 * (double)s1 / N;
                if (varI <= 1e-9 || varT <= 1e-9) {
                    dst[x] = 0.0f;
                    continue;
                }

                double numerator = (double)dot - (double)s1 * sumT / N;
                double denom = std::sqrt(varT * varI);
                double r = denom > 1e-9 ? numerator / denom : 0.0;
                dst[x] = (float)std::clamp(r, -1.0, 1.0);
            }
        }
    };

    unsigned threads = std::max(1u, std::thread::hardware_concurrency());
    // 任务太小就不值得开线程
    if ((long long)resW * resH < 40000 || threads == 1) {
        work(0, resH);
        return;
    }

    threads = std::min<unsigned>(threads, (unsigned)resH);
    std::vector<std::thread> pool;
    pool.reserve(threads);
    int chunk = (resH + (int)threads - 1) / (int)threads;
    for (unsigned i = 0; i < threads; ++i) {
        int y0 = (int)i * chunk;
        int y1 = std::min(resH, y0 + chunk);
        if (y0 >= y1) break;
        pool.emplace_back(work, y0, y1);
    }
    for (auto& t : pool) {
        t.join();
    }
}

}  // namespace

double Cv::MatchBestScore(const GrayImage& source, const GrayImage& templ,
                          int roiX, int roiY, int roiW, int roiH) {
    if (source.Empty() || templ.Empty()) {
        return 0.0;
    }

    // 裁剪 ROI 并与图像边界求交
    int x0 = std::max(0, roiX);
    int y0 = std::max(0, roiY);
    int x1 = std::min(source.width, roiX + roiW);
    int y1 = std::min(source.height, roiY + roiH);
    if (x1 - x0 < templ.width || y1 - y0 < templ.height) {
        return 0.0;
    }

    GrayImage roi;
    roi.width = x1 - x0;
    roi.height = y1 - y0;
    roi.pixels.resize((size_t)roi.width * roi.height);
    for (int y = 0; y < roi.height; ++y) {
        const uint8_t* s = source.Row(y0 + y) + x0;
        std::copy(s, s + roi.width, roi.Row(y));
    }

    TemplateStats tpl = BuildTemplateStats(templ);
    std::vector<float> map;
    int resW = 0;
    int resH = 0;
    ComputeMatchMap(roi, tpl, map, resW, resH);
    if (map.empty()) {
        return 0.0;
    }
    return (double)*std::max_element(map.begin(), map.end());
}

bool Cv::MatchBest(const GrayImage& source, const GrayImage& templ,
                   int roiX, int roiY, int roiW, int roiH,
                   double threshold, MatchResult& out) {
    auto all = MatchAll(source, templ, roiX, roiY, roiW, roiH, threshold, 1);
    if (all.empty()) {
        return false;
    }
    out = all[0];
    return true;
}

std::vector<MatchResult> Cv::MatchAll(const GrayImage& source, const GrayImage& templ,
                                      int roiX, int roiY, int roiW, int roiH,
                                      double threshold, int maxResults) {
    std::vector<MatchResult> results;
    if (source.Empty() || templ.Empty() || maxResults <= 0) {
        return results;
    }

    int x0 = std::max(0, roiX);
    int y0 = std::max(0, roiY);
    int x1 = std::min(source.width, roiX + roiW);
    int y1 = std::min(source.height, roiY + roiH);
    if (x1 - x0 < templ.width || y1 - y0 < templ.height) {
        return results;
    }

    GrayImage roi;
    roi.width = x1 - x0;
    roi.height = y1 - y0;
    roi.pixels.resize((size_t)roi.width * roi.height);
    for (int y = 0; y < roi.height; ++y) {
        const uint8_t* s = source.Row(y0 + y) + x0;
        std::copy(s, s + roi.width, roi.Row(y));
    }

    TemplateStats tpl = BuildTemplateStats(templ);
    std::vector<float> map;
    int resW = 0;
    int resH = 0;
    ComputeMatchMap(roi, tpl, map, resW, resH);
    if (map.empty()) {
        return results;
    }

    // 局部极大值（与 8 邻域比较）且超过阈值
    std::vector<MatchResult> candidates;
    for (int y = 0; y < resH; ++y) {
        for (int x = 0; x < resW; ++x) {
            float v = map[(size_t)y * resW + x];
            if (v < threshold) {
                continue;
            }

            bool isMax = true;
            for (int dy = -1; dy <= 1 && isMax; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    int nx = x + dx;
                    int ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= resW || ny >= resH) continue;
                    if (map[(size_t)ny * resW + nx] > v) {
                        isMax = false;
                        break;
                    }
                }
            }

            if (isMax) {
                MatchResult m;
                m.x = x0 + x;
                m.y = y0 + y;
                m.w = templ.width;
                m.h = templ.height;
                m.score = v;
                candidates.push_back(m);
            }
        }
    }

    // 非极大值抑制：按分数降序，剔除重叠框
    std::sort(candidates.begin(), candidates.end(),
              [](const MatchResult& a, const MatchResult& b) { return a.score > b.score; });

    for (const auto& c : candidates) {
        bool overlap = false;
        for (const auto& kept : results) {
            int ix1 = std::max(c.x, kept.x);
            int iy1 = std::max(c.y, kept.y);
            int ix2 = std::min(c.x + c.w, kept.x + kept.w);
            int iy2 = std::min(c.y + c.h, kept.y + kept.h);
            if (ix2 <= ix1 || iy2 <= iy1) {
                continue;
            }
            double inter = (double)(ix2 - ix1) * (iy2 - iy1);
            double uni = (double)c.w * c.h + (double)kept.w * kept.h - inter;
            if (uni > 0 && inter / uni > 0.3) {
                overlap = true;
                break;
            }
        }

        if (!overlap) {
            results.push_back(c);
            if ((int)results.size() >= maxResults) {
                break;
            }
        }
    }

    return results;
}

// ---------------------------------------------------------------------------
// 颜色判定
// ---------------------------------------------------------------------------

namespace {

// BGR -> HSV，取值与 OpenCV 一致：H∈[0,180]，S,V∈[0,255]
inline void BgrToHsv(int b, int g, int r, int& h, int& s, int& v) {
    v = std::max({b, g, r});
    int mn = std::min({b, g, r});
    if (v == 0) {
        h = 0;
        s = 0;
        return;
    }

    // 与 OpenCV 一致：S 用四舍五入，而非截断
    s = (int)std::lround((v - mn) * 255.0 / v);
    int delta = v - mn;
    if (delta == 0) {
        h = 0;
        return;
    }

    int hh;
    if (v == r) {
        hh = (int)std::lround(60.0 * (g - b) / delta);
    } else if (v == g) {
        hh = (int)std::lround(120.0 + 60.0 * (b - r) / delta);
    } else {
        hh = (int)std::lround(240.0 + 60.0 * (r - g) / delta);
    }
    if (hh < 0) {
        hh += 360;
    }
    h = hh / 2;
}

}  // namespace

bool Cv::IsOrangeRegion(const Image& region, double minRatio) {
    if (region.Empty()) {
        return false;
    }

    int orange = 0;
    const int total = region.Width() * region.Height();

    for (int y = 0; y < region.Height(); ++y) {
        const uint8_t* row = region.Row(y);
        for (int x = 0; x < region.Width(); ++x) {
            int h = 0;
            int s = 0;
            int v = 0;
            BgrToHsv(row[x * 3 + 0], row[x * 3 + 1], row[x * 3 + 2], h, s, v);

            // 橙色区间
            if (h >= 10 && h <= 30 && s >= 120 && v >= 140) {
                ++orange;
            }
        }
    }

    return total > 0 && (double)orange / total > minRatio;
}

GrayImage Cv::MaskHsvRange(const Image& bgr,
                           int hLo, int sLo, int vLo,
                           int hHi, int sHi, int vHi) {
    GrayImage mask;
    if (bgr.Empty()) {
        return mask;
    }

    mask.width = bgr.Width();
    mask.height = bgr.Height();
    mask.pixels.assign((size_t)mask.width * mask.height, 0);

    for (int y = 0; y < mask.height; ++y) {
        const uint8_t* src = bgr.Row(y);
        uint8_t* dst = mask.Row(y);
        for (int x = 0; x < mask.width; ++x) {
            int h = 0;
            int s = 0;
            int v = 0;
            BgrToHsv(src[x * 3 + 0], src[x * 3 + 1], src[x * 3 + 2], h, s, v);
            if (h >= hLo && h <= hHi && s >= sLo && s <= sHi && v >= vLo && v <= vHi) {
                dst[x] = 255;
            }
        }
    }
    return mask;
}

double Cv::DarkRatio(const GrayImage& gray, int threshold) {
    if (gray.Empty()) {
        return 0.0;
    }

    long long dark = 0;
    for (uint8_t v : gray.pixels) {
        if (v < threshold) {
            ++dark;
        }
    }
    return (double)dark / (double)gray.pixels.size();
}

// ---------------------------------------------------------------------------
// 连通域
// ---------------------------------------------------------------------------

std::vector<Blob> Cv::FindBlobs(const GrayImage& mask) {
    std::vector<Blob> blobs;
    if (mask.Empty()) {
        return blobs;
    }

    const int W = mask.width;
    const int H = mask.height;
    std::vector<uint8_t> visited((size_t)W * H, 0);
    std::vector<int> stack;

    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            int start = y * W + x;
            if (visited[start] || mask.pixels[start] == 0) {
                continue;
            }

            int minX = x;
            int maxX = x;
            int minY = y;
            int maxY = y;
            int area = 0;

            stack.clear();
            stack.push_back(start);
            visited[start] = 1;

            while (!stack.empty()) {
                int cur = stack.back();
                stack.pop_back();
                int cx = cur % W;
                int cy = cur / W;
                ++area;
                if (cx < minX) minX = cx;
                if (cx > maxX) maxX = cx;
                if (cy < minY) minY = cy;
                if (cy > maxY) maxY = cy;

                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dy == 0) continue;
                        int nx = cx + dx;
                        int ny = cy + dy;
                        if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
                        int idx = ny * W + nx;
                        if (visited[idx] || mask.pixels[idx] == 0) continue;
                        visited[idx] = 1;
                        stack.push_back(idx);
                    }
                }
            }

            Blob blob;
            blob.x = minX;
            blob.y = minY;
            blob.w = maxX - minX + 1;
            blob.h = maxY - minY + 1;
            blob.area = area;
            blobs.push_back(blob);
        }
    }

    return blobs;
}

}  // namespace bys
