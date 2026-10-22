#pragma once

#include <QSlider>

class QPainter;
class QVariantAnimation;

/**
 * 带 Material 3 反馈的滑块：handle 悬停/按压时连续放大（不再瞬切），
 * 悬停并带一圈 accent 光晕。
 *
 * 只接管横向绘制；纵向或缺省平台行为回落到 QSlider::paintEvent。
 * QSS 的 QSlider 规则保留（仅用于尺寸计算，绘制已由本类接管）。
 */
class AnimatedSlider : public QSlider {
    Q_OBJECT

public:
    explicit AnimatedSlider(QWidget* parent = nullptr);
    explicit AnimatedSlider(Qt::Orientation orientation, QWidget* parent = nullptr);
    ~AnimatedSlider() override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void animateTo(QVariantAnimation*& anim, double& slot, double target, int nominalMs);
    void drawHorizontal(QPainter& painter) const;

    QVariantAnimation* m_hoverAnim = nullptr;
    QVariantAnimation* m_pressAnim = nullptr;
    double m_hover = 0.0;
    double m_press = 0.0;
};
