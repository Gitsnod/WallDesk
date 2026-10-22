#include "AnimatedSlider.h"

#include "MotionTokens.h"
#include "../AppTheme.h"

#include <QEnterEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QVariantAnimation>

namespace {

/** 轨道高度（与 AppTheme QSS 的 groove 高度一致）。 */
constexpr double kTrackHeight = 4.0;
/** handle 基础半径（比旧版小 0.5，给 hover 光晕腾出空间）。 */
constexpr double kHandleRadius = 6.8;
/** 左右留白：必须 ≥ 光晕最大半径，否则 0%/100% 位置的光晕会被控件矩形切平。 */
constexpr double kInset = 12.0;

} // namespace

AnimatedSlider::AnimatedSlider(QWidget* parent)
    : QSlider(parent)
{
    // 17px 高放不下 24px 的光晕，hover 时上下直接被裁掉（旧版症状）。
    setMinimumHeight(24);
}

AnimatedSlider::AnimatedSlider(Qt::Orientation orientation, QWidget* parent)
    : QSlider(orientation, parent)
{
    setMinimumHeight(24);
}

AnimatedSlider::~AnimatedSlider() = default;

void AnimatedSlider::paintEvent(QPaintEvent* event)
{
    if (orientation() != Qt::Horizontal || maximum() <= minimum()) {
        QSlider::paintEvent(event);
        return;
    }
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    drawHorizontal(p);
}

void AnimatedSlider::drawHorizontal(QPainter& p) const
{
    const ThemeKind tk = AppTheme::current();
    QColor accent = QColor(AppTheme::accentColor(tk));
    const QColor card = AppTheme::buttonBaseColor(tk);
    const QColor border = AppTheme::buttonBorderColor(tk);
    if (!isEnabled()) {
        accent = border;
    }

    const double radius = kHandleRadius + 1.2 * m_hover + 1.0 * m_press;
    const double left = kInset;
    const double right = width() - kInset;
    const double available = qMax(1.0, right - left);

    double ratio = double(value() - minimum()) / double(maximum() - minimum());
    ratio = qBound(0.0, ratio, 1.0);
    if (invertedAppearance()) {
        ratio = 1.0 - ratio;
    }
    const double hx = left + available * ratio;
    const double cy = height() / 2.0;

    p.setPen(Qt::NoPen);

    // 轨道底色
    p.setBrush(border);
    p.drawRoundedRect(QRectF(left, cy - kTrackHeight / 2.0, available, kTrackHeight),
                      kTrackHeight / 2.0, kTrackHeight / 2.0);

    // 已走过部分
    p.setBrush(accent);
    p.drawRoundedRect(QRectF(left, cy - kTrackHeight / 2.0, qMax(0.0, hx - left), kTrackHeight),
                      kTrackHeight / 2.0, kTrackHeight / 2.0);

    // 触点光晕：只由 hover / press 驱动，避免轨道上出现常驻色块。
    // 半径上限 11.5 < kInset，且 < 控件高/2 ⇒ 四个方向都不会被裁。
    const double haloAlpha = 0.12 * m_hover + 0.10 * m_press;
    if (haloAlpha > 0.001) {
        QColor halo = accent;
        halo.setAlphaF(haloAlpha);
        p.setBrush(halo);
        p.drawEllipse(QPointF(hx, cy), radius + 2.8, radius + 2.8);
    }

    // handle：白底 + accent 描边
    p.setBrush(card);
    p.setPen(QPen(accent, 2.0));
    p.drawEllipse(QPointF(hx, cy), radius, radius);
}

void AnimatedSlider::animateTo(QVariantAnimation*& anim, double& slot, double target, int nominalMs)
{
    if (anim) {
        anim->stop();
    }
    if (!MotionTokens::animationsEnabled()) {
        slot = target;
        update();
        return;
    }
    if (!anim) {
        anim = new QVariantAnimation(this);
        anim->setEasingCurve(MotionTokens::standardDecelerate());
        connect(anim, &QVariantAnimation::valueChanged, this,
                [this, &slot](const QVariant& value) {
                    slot = value.toDouble();
                    update();
                });
    }
    anim->setDuration(MotionTokens::duration(nominalMs));
    anim->setStartValue(slot);
    anim->setEndValue(target);
    anim->start();
}

void AnimatedSlider::enterEvent(QEnterEvent* event)
{
    animateTo(m_hoverAnim, m_hover, 1.0, 200);
    QSlider::enterEvent(event);
}

void AnimatedSlider::leaveEvent(QEvent* event)
{
    animateTo(m_hoverAnim, m_hover, 0.0, 250);
    QSlider::leaveEvent(event);
}

void AnimatedSlider::mousePressEvent(QMouseEvent* event)
{
    animateTo(m_pressAnim, m_press, 1.0, 120);
    QSlider::mousePressEvent(event);
}

void AnimatedSlider::mouseReleaseEvent(QMouseEvent* event)
{
    animateTo(m_pressAnim, m_press, 0.0, 200);
    QSlider::mouseReleaseEvent(event);
}

void AnimatedSlider::changeEvent(QEvent* event)
{
    switch (event->type()) {
    case QEvent::PaletteChange:
    case QEvent::StyleChange:
    case QEvent::EnabledChange:
        update();
        break;
    default:
        break;
    }
    QSlider::changeEvent(event);
}
