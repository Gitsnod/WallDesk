#pragma once

#include <QComboBox>

class QWidget;

/**
 * 带弹出动画的下拉框：popup 容器「淡入 + 高度展开」250ms（Standard decelerate），
 * 收起 200ms（Standard accelerate），替代 Fusion 原生瞬间弹出。
 *
 * 说明：
 * - popup 容器是 view()->parentWidget()（QComboBoxPrivateContainer，Qt::Popup 窗），
 *   不是 view 本身。
 * - Esc / 选中项 / 失焦都走 QComboBox::hidePopup() 虚函数，动画可拦；
 *   点击 popup 外部由 Qt::Popup 窗口机制强制关闭（不走本类），该路径瞬消
 *   （用户注意力已转移，M3 也允许 dismiss 快于 enter）。
 * - 系统关闭动画时全部瞬时完成。
 */
class AnimatedComboBox : public QComboBox {
    Q_OBJECT

public:
    explicit AnimatedComboBox(QWidget* parent = nullptr);
    ~AnimatedComboBox() override;

protected:
    void showPopup() override;
    void hidePopup() override;
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QWidget* popupContainer();
    /** 首次弹出前把容器改成透明背景 + 圆角（改窗口属性必须在 show 之前）。 */
    void configurePopup();
    /** 自己算弹出层落点（Qt 默认落点会压住控件本身）。 */
    void placePopup();
    /** 按圆角路径给弹出窗口打 mask（原生窗口只能用 mask 切角）。 */
    void applyPopupMask();
    void animatePopup(bool entering);
    /** 展开/收起时箭头翻转 0°↔180°：光靠一个静态 V 形，下拉框和输入框太难分。 */
    void animateArrow(bool open);

    QWidget* m_popup = nullptr;
    bool m_closing = false;
    bool m_popupStyled = false;
    class QParallelAnimationGroup* m_popAnim = nullptr;
    class QVariantAnimation* m_arrowAnim = nullptr;
    double m_arrow = 0.0;   // 0=向下（收起），1=向上（展开）
};
