#pragma once

#include <QObject>

class QWidget;

/**
 * 菜单淡入（全局事件过滤器，零侵入）。
 *
 * 现有皮肤里 QMenu 是硬弹出，没有过渡。这里不替换任何构造点：
 * 安装一个全局过滤器，拦截 QMenu 的 Show 事件，对弹层做
 * 200ms Standard decelerate 的 windowOpacity 0→1 淡入。
 *
 * 系统「关闭动画效果」时直接置不透明，不产生半态。
 */
class MenuFade : public QObject {
public:
    /** 安装到 QApplication（幂等，重复调用无副作用）。 */
    static void install();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    explicit MenuFade(QObject* parent);
    static MenuFade* instance();
    void fadeIn(QWidget* menu);
    void reset(QWidget* menu);
};
