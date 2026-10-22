#include "AnimatedComboBox.h"
#include "../AppTheme.h"
#include "Chevron.h"
#include "MotionTokens.h"

#include <QAbstractAnimation>
#include <QAbstractItemView>
#include <QPainter>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>

AnimatedComboBox::AnimatedComboBox(QWidget* parent)
    : QComboBox(parent)
    , m_popup(nullptr)
    , m_popAnim(nullptr)
{
}

AnimatedComboBox::~AnimatedComboBox()
{
    delete m_popAnim;
    m_popAnim = nullptr;
}

QWidget* AnimatedComboBox::popupContainer()
{
    if (m_popup) {
        return m_popup;
    }
    if (!this->view()) {
        return nullptr;
    }
    m_popup = this->view()->parentWidget();
    return m_popup;
}

void AnimatedComboBox::animatePopup(bool entering)
{
    QWidget* container = popupContainer();
    if (!container) {
        return;
    }

    if (m_popAnim) {
        m_popAnim->stop();
        delete m_popAnim;
        m_popAnim = nullptr;
    }

    if (!MotionTokens::animationsEnabled()) {
        m_closing = false;
        if (entering) {
            container->setWindowOpacity(1.0);
        } else {
            QComboBox::hidePopup();
        }
        return;
    }

    const QRect target = container->geometry();
    const QRect start = entering
        ? QRect(target.x(), target.y(), target.width(), 0)
        : container->geometry();

    auto* geomAnim = new QPropertyAnimation(container, "geometry", m_popAnim);
    geomAnim->setDuration(entering ? MotionTokens::kPopupEnterMs
                                   : MotionTokens::kPopupExitMs);
    geomAnim->setStartValue(start);
    geomAnim->setEndValue(entering ? target
                                   : QRect(target.x(), target.y(),
                                           target.width(), 0));
    geomAnim->setEasingCurve(entering ? MotionTokens::standardDecelerate()
                                      : MotionTokens::standardAccelerate());

    auto* fadeAnim = new QPropertyAnimation(container, "windowOpacity", m_popAnim);
    fadeAnim->setDuration(geomAnim->duration());
    fadeAnim->setStartValue(entering ? 0.0 : 1.0);
    fadeAnim->setEndValue(entering ? 1.0 : 0.0);
    fadeAnim->setEasingCurve(geomAnim->easingCurve());

    m_popAnim = new QParallelAnimationGroup(this);
    m_popAnim->addAnimation(geomAnim);
    m_popAnim->addAnimation(fadeAnim);

    if (!entering) {
        m_closing = true;
        connect(m_popAnim, &QParallelAnimationGroup::finished, this, [this]() {
            m_closing = false;
            QComboBox::hidePopup();
        });
    } else {
        m_closing = false;
    }
    m_popAnim->start();
}

void AnimatedComboBox::showPopup()
{
    QComboBox::showPopup();
    animatePopup(true);
}

void AnimatedComboBox::hidePopup()
{
    if (m_closing) {
        return; // 收起动画中重入：忽略，等 finished 后真正隐藏
    }
    QWidget* container = popupContainer();
    if (!container || !container->isVisible()) {
        QComboBox::hidePopup();
        return;
    }
    animatePopup(false);
}

void AnimatedComboBox::paintEvent(QPaintEvent* event)
{
    QComboBox::paintEvent(event);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // 下拉箭头：QSS 只能给 ::drop-down 宽度，给不出图像，右侧原本是空白，
    // 整个控件看着就是个输入框。这里自绘 chevron，位置与 QSS 的 26px 区对齐。
    const qreal arrowZone = 26.0;
    const QRectF arrowBox(width() - arrowZone, 0.0, arrowZone, height());
    QColor arrowColor = isEnabled() ? AppTheme::subTextColor(AppTheme::current())
                                    : QColor(148, 163, 184);
    if (isEnabled() && underMouse()) {
        arrowColor = QColor(AppTheme::accentColor(AppTheme::current()));
    }
    Chevron::drawDown(p, arrowBox, arrowColor);

    if (!isEnabled() || !hasFocus()) {
        return;
    }
    // 焦点环（QSS 无 outline，自绘 2px accent 50%）
    QColor ring = QColor(AppTheme::accentColor(AppTheme::current()));
    ring.setAlphaF(0.5);
    p.setPen(QPen(ring, 2.0));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0), 4.0, 4.0);
}
