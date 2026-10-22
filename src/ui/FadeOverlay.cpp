#include "FadeOverlay.h"

#include "MotionTokens.h"

#include <QPainter>
#include <QVariantAnimation>

FadeOverlay::FadeOverlay(QWidget* host)
    : QWidget(host)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setFocusPolicy(Qt::NoFocus);
}

void FadeOverlay::setAlpha(double alpha)
{
    m_alpha = qBound(0.0, alpha, 1.0);
    update();
}

void FadeOverlay::play(QWidget* host, int ms, const QColor& color)
{
    if (!host || ms <= 0 || !MotionTokens::animationsEnabled()) {
        return;
    }
    // 同一个 host 上已有遮罩在跑（快速连点）：先收掉，避免两层叠着加倍变白
    const auto siblings = host->findChildren<FadeOverlay*>(QString(), Qt::FindDirectChildrenOnly);
    for (FadeOverlay* old : siblings) {
        old->deleteLater();
    }

    auto* overlay = new FadeOverlay(host);
    overlay->m_color = color;
    overlay->setGeometry(host->rect());
    overlay->raise();
    overlay->show();

    auto* anim = new QVariantAnimation(overlay);
    anim->setDuration(ms);
    anim->setStartValue(1.0);
    anim->setEndValue(0.0);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    QObject::connect(anim, &QVariantAnimation::valueChanged, overlay,
                     [overlay](const QVariant& v) { overlay->setAlpha(v.toDouble()); });
    QObject::connect(anim, &QVariantAnimation::finished, overlay, &QObject::deleteLater);
    overlay->m_anim = anim;
    anim->start();
}

void FadeOverlay::paintEvent(QPaintEvent*)
{
    if (m_alpha <= 0.001) {
        return;
    }
    QColor c = m_color;
    c.setAlphaF(m_alpha);
    QPainter p(this);
    p.fillRect(rect(), c);
}
