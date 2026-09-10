#pragma once

#include <QImage>
#include <QString>

/**
 * 画面效果参数。图片与视频共用一套语义：
 * 图片侧由 ImageEffects::apply 逐像素处理；视频侧映射到 libVLC 的 adjust 滤镜。
 *
 * 取值范围统一为 -100..100（模糊/暗角为 0..100），界面滑块与序列化都按这个来。
 */
struct ImageEffect {
    int brightness = 0; // 亮度 -100..100，0 为原样
    int contrast = 0;   // 对比度 -100..100
    int saturation = 0; // 饱和度 -100..100，-100 等同灰度
    int blur = 0;       // 模糊半径 0..20（像素）
    int vignette = 0;   // 暗角强度 0..100
    bool grayscale = false;

    bool isDefault() const
    {
        return brightness == 0 && contrast == 0 && saturation == 0 && blur == 0
               && vignette == 0 && !grayscale;
    }

    /** 用于缓存文件名的指纹：参数一致即可复用上次处理结果。 */
    QString key() const
    {
        return QStringLiteral("%1_%2_%3_%4_%5_%6")
            .arg(brightness)
            .arg(contrast)
            .arg(saturation)
            .arg(blur)
            .arg(vignette)
            .arg(grayscale ? 1 : 0);
    }
};

namespace ImageEffects {
/**
 * 对图片套用效果。输入为空或效果为默认时原样返回。
 * 处理顺序：亮度/对比度/饱和度 → 灰度 → 模糊 → 暗角。
 * 大图（4K）耗时在百毫秒级，调用方应在后台或一次性生成后缓存。
 */
QImage apply(const QImage& source, const ImageEffect& fx);
}
