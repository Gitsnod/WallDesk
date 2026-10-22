#pragma once

#include <QCheckBox>

class QPainter;
class QVariantAnimation;

/**
 * 勾选动画复选框：indicator 自绘，取代 QSS 的 hover/checked 硬切。
 *
 * - 勾按 120ms 分两段描出（先短边、再长边），底色与边框同时在
 *   card → accent 之间插值；
 * - hover 边框向 accent 靠拢（300ms Standard），不再瞬间变色；
 * - 键盘焦点画 2px accent 50% 圆角环（系统关动画时依然显示）。
 *
 * 程序化 setChecked（如载入配置）在控件不可见时直接落终态，不演动画。
 */
class AnimatedCheckBox : public QCheckBox {
    Q_OBJECT

public:
    explicit AnimatedCheckBox(QWidget* parent = nullptr);
    explicit AnimatedCheckBox(const QString& text, QWidget* parent = nullptr);
    ~AnimatedCheckBox() override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void init();
    QRect indicatorRect() const;
    void drawIndicator(QPainter& painter, const QRectF& box) const;
    void animateCheckTo(double target);
    void animateHoverTo(double target);

    QVariantAnimation* m_checkAnim = nullptr; // 值 = 勾选进度 0..1
    QVariantAnimation* m_hoverAnim = nullptr; // 值 = hover 强度 0..1
    /** 已绘制的勾选进度（自持，避免 from/to 反推导致首帧瞬跳）。 */
    double m_progress = 0.0;
    double m_hover = 0.0;
};
