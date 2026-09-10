#include "ImageEffects.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

/** 亮度/对比度查表：把 -100..100 的界面量映射到像素乘加系数。 */
struct Lut {
    std::vector<unsigned char> table;

    explicit Lut(int brightness, int contrast)
    {
        // 对比度：以 128 为支点做线性拉伸；亮度：整体平移
        const double c = 1.0 + contrast / 100.0;
        const double b = brightness * 2.55;
        table.resize(256);
        for (int i = 0; i < 256; ++i) {
            const double v = (static_cast<double>(i) - 128.0) * c + 128.0 + b;
            table[i] = static_cast<unsigned char>(std::clamp(v, 0.0, 255.0));
        }
    }
};

/** 可分离盒式模糊：横向 + 纵向各做一遍，重复两轮接近高斯效果。 */
void boxBlur(QImage& image, int radius)
{
    if (radius <= 0) {
        return;
    }
    const int w = image.width();
    const int h = image.height();
    const int span = radius * 2 + 1;
    std::vector<int> tmpR(static_cast<size_t>(w * h));
    std::vector<int> tmpG(tmpR.size());
    std::vector<int> tmpB(tmpR.size());

    // 两轮「横向 + 纵向」盒式模糊：效果接近高斯，且复杂度仍是 O(n)
    for (int pass = 0; pass < 2; ++pass) {
        // 横向：滑动窗口求和，边界用钳位索引复用边缘像素
        for (int y = 0; y < h; ++y) {
            const QRgb* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
            int r = 0;
            int g = 0;
            int b = 0;
            for (int x = -radius; x <= radius; ++x) {
                const QRgb px = line[std::clamp(x, 0, w - 1)];
                r += qRed(px);
                g += qGreen(px);
                b += qBlue(px);
            }
            for (int x = 0; x < w; ++x) {
                const QRgb pa = line[std::min(x + radius, w - 1)];
                const QRgb ps = line[std::max(x - radius, 0)];
                r += qRed(pa) - qRed(ps);
                g += qGreen(pa) - qGreen(ps);
                b += qBlue(pa) - qBlue(ps);
                const size_t idx = static_cast<size_t>(y * w + x);
                tmpR[idx] = r / span;
                tmpG[idx] = g / span;
                tmpB[idx] = b / span;
            }
        }
        // 纵向
        for (int x = 0; x < w; ++x) {
            int r = 0;
            int g = 0;
            int b = 0;
            for (int y = -radius; y <= radius; ++y) {
                const size_t idx = static_cast<size_t>(std::clamp(y, 0, h - 1) * w + x);
                r += tmpR[idx];
                g += tmpG[idx];
                b += tmpB[idx];
            }
            for (int y = 0; y < h; ++y) {
                const size_t add = static_cast<size_t>(std::min(y + radius, h - 1) * w + x);
                const size_t sub = static_cast<size_t>(std::max(y - radius, 0) * w + x);
                r += tmpR[add] - tmpR[sub];
                g += tmpG[add] - tmpG[sub];
                b += tmpB[add] - tmpB[sub];
                image.setPixel(x, y,
                               qRgb(std::clamp(r / span, 0, 255), std::clamp(g / span, 0, 255),
                                    std::clamp(b / span, 0, 255)));
            }
        }
    }
}

/** 暗角：以画面中心为圆心，按归一化半径压暗边缘。 */
void applyVignette(QImage& image, int strength)
{
    if (strength <= 0) {
        return;
    }
    const int w = image.width();
    const int h = image.height();
    const double cx = w / 2.0;
    const double cy = h / 2.0;
    const double maxDist = std::sqrt(cx * cx + cy * cy);
    const double amount = strength / 100.0; // 边缘最多压到 (1-amount)
    for (int y = 0; y < h; ++y) {
        const double dy = y - cy;
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const double dx = x - cx;
            const double d = std::sqrt(dx * dx + dy * dy) / maxDist;
            const double f = 1.0 - amount * d * d;
            const QRgb px = line[x];
            line[x] = qRgb(static_cast<int>(qRed(px) * f), static_cast<int>(qGreen(px) * f),
                           static_cast<int>(qBlue(px) * f));
        }
    }
}

} // namespace

QImage ImageEffects::apply(const QImage& source, const ImageEffect& fx)
{
    if (source.isNull() || fx.isDefault()) {
        return source;
    }
    QImage image = source.convertToFormat(QImage::Format_RGB32);
    const bool needTone = fx.brightness != 0 || fx.contrast != 0 || fx.saturation != 0
                          || fx.grayscale;
    if (needTone) {
        const Lut lut(fx.brightness, fx.contrast);
        const double sat = fx.grayscale ? 0.0 : (1.0 + fx.saturation / 100.0);
        for (int y = 0; y < image.height(); ++y) {
            QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
            for (int x = 0; x < image.width(); ++x) {
                const QRgb px = line[x];
                int r = lut.table[qRed(px)];
                int g = lut.table[qGreen(px)];
                int b = lut.table[qBlue(px)];
                if (sat != 1.0) {
                    // 与亮度权重混合：0 = 灰度，1 = 原色，>1 增艳
                    const int gray = (r * 299 + g * 587 + b * 114) / 1000;
                    r = std::clamp(static_cast<int>(gray + (r - gray) * sat), 0, 255);
                    g = std::clamp(static_cast<int>(gray + (g - gray) * sat), 0, 255);
                    b = std::clamp(static_cast<int>(gray + (b - gray) * sat), 0, 255);
                }
                line[x] = qRgb(r, g, b);
            }
        }
    }
    if (fx.blur > 0) {
        boxBlur(image, std::clamp(fx.blur, 0, 20));
    }
    applyVignette(image, fx.vignette);
    return image;
}
