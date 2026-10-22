#pragma once

#include <QPushButton>
#include <QPointF>

class QVariantAnimation;

/**
 * 带 Material 3 交互反馈的按钮：state layer（hover 8% / pressed 10%）
 * + ripple 触点扩散（400ms）+ 焦点环 + 按压下沉 1px。
 *
 * 取代 QSS 的 hover/pressed 硬切（QSS 无 transition 能力）。
 * 时长/曲线/强度全部走 MotionTokens；系统关闭动画时全部瞬时落终态。
 * 主题色现取（AppTheme 静态接口），主题热切换后下一帧自动正确。
 */
class AnimatedPushButton : public QPushButton {
    Q_OBJECT

public:
    explicit AnimatedPushButton(QWidget* parent = nullptr);
    explicit AnimatedPushButton(const QString& text, QWidget* parent = nullptr);
    ~AnimatedPushButton() override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    /** 主按钮（objectName == "primary"）走 on-primary 白反馈，其余走主色。 */
    bool isPrimary() const;
    QColor baseFill() const;
    QColor borderColor() const;
    QColor layerColor() const;
    /** 当前 state layer 强度（自持值，动画进行中由 valueChanged 回写）。 */
    double currentLayer() const;
    double rippleProgress() const;

    void animateLayerTo(double target);
    void startRipple(const QPointF& localPos);
    void fadeOutRipple();
    void stopAnimations();

    QVariantAnimation* m_layerAnim = nullptr;  // 值 = 强度（0.08 / 0.10）
    QVariantAnimation* m_rippleAnim = nullptr; // 值 = 扩散进度 0..1
    /**
     * 已绘制的 state layer 强度（自持，不依赖 underMouse()）。
     * 必须自持：Enter 到达时 underMouse() 已为 true，若每次从
     * 「当前是否悬停」反推起点，首个 hover 会算出 from == to（瞬跳，无过渡）。
     */
    double m_layerValue = 0.0;
    QPointF m_pressPos;      // 按下点（ripple 起点）
    QPointF m_rippleOrigin;
    double m_rippleMaxRadius = 0.0;
    double m_rippleLevel = 0.0; // 当前 ripple 不透明度
    bool m_pressed = false;
    bool m_fadingRipple = false;
};
