#pragma once

#include <QColor>
#include <QWidget>

class QVariantAnimation;

/** 场景级淡入遮罩（V4.17 / P2）。
 *
 * 为什么不用 QGraphicsOpacityEffect：本机实测（Windows + Qt 6.11 + DWM）给整页设
 * opacity 0→1 时，动画期间窗口不会逐帧重绘 —— 屏幕一直显示旧像素，动画结束瞬间才
 * 跳成新页面，等于没有动画。改成自己 paintEvent 画一层同色遮罩、每帧 update()，
 * 渲染路径和 ripple 一致，一定可见。
 */
class FadeOverlay : public QWidget {
    Q_OBJECT

public:
    /** 在 host 上盖一层 color 色遮罩，alpha 从 1 线性淡到 0 后自毁。 */
    static void play(QWidget* host, int ms, const QColor& color);

    explicit FadeOverlay(QWidget* host);
    void setAlpha(double alpha);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    double m_alpha = 1.0;
    QColor m_color;
    QVariantAnimation* m_anim = nullptr;
};
