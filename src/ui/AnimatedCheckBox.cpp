#include "AnimatedCheckBox.h"

#include "MotionTokens.h"
#include "../AppTheme.h"

#include <QEnterEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOptionButton>
#include <QVariantAnimation>

namespace {

QColor lerpColor(const QColor& a, const QColor& b, double t)
{
    t = qBound(0.0, t, 1.0);
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                            a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t);
}

QPointF lerpPoint(const QPointF& a, const QPointF& b, double t)
{
    return a + (b - a) * qBound(0.0, t, 1.0);
}

} // namespace

AnimatedCheckBox::AnimatedCheckBox(QWidget* parent)
    : QCheckBox(parent)
{
    init();
}

AnimatedCheckBox::AnimatedCheckBox(const QString& text, QWidget* parent)
    : QCheckBox(text, parent)
{
    init();
}

AnimatedCheckBox::~AnimatedCheckBox() = default;

void AnimatedCheckBox::init()
{
    m_progress = isChecked() ? 1.0 : 0.0;
    connect(this, &QAbstractButton::toggled, this,
            [this](bool on) { animateCheckTo(on ? 1.0 : 0.0); });
}

QRect AnimatedCheckBox::indicatorRect() const
{
    QStyleOptionButton opt;
    initStyleOption(&opt);
    return style()->subElementRect(QStyle::SE_CheckBoxIndicator, &opt, this);
}

void AnimatedCheckBox::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing, true);

    QStyleOptionButton opt;
    initStyleOption(&opt);

    drawIndicator(p, QRectF(indicatorRect()));

    // 文字/图标仍交给样式（QSS 负责字体与主题文字色）
    QStyleOptionButton labelOpt = opt;
    labelOpt.rect = style()->subElementRect(QStyle::SE_CheckBoxContents, &labelOpt, this);
    labelOpt.state &= ~QStyle::State_HasFocus; // 焦点环自绘，避免双份
    style()->drawControl(QStyle::CE_CheckBoxLabel, &labelOpt, &p, this);

    if (hasFocus()) {
        QColor ring = QColor(AppTheme::accentColor(AppTheme::current()));
        ring.setAlphaF(0.5);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(ring, 2.0));
        p.drawRoundedRect(QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0), 8.0, 8.0);
    }
}

void AnimatedCheckBox::drawIndicator(QPainter& p, const QRectF& box) const
{
    const ThemeKind tk = AppTheme::current();
    const QColor accent = QColor(AppTheme::accentColor(tk));
    const QColor card = AppTheme::buttonBaseColor(tk);
    const QColor border = AppTheme::buttonBorderColor(tk);

    const double progress = qBound(0.0, m_progress, 1.0);
    // hover 只把边框往 accent 推 40%，避免和「已勾选」混淆
    const double borderMix = isEnabled() ? qMax(progress, m_hover * 0.4) : 0.0;

    QColor fill = isEnabled() ? lerpColor(card, accent, progress) : card;
    QColor stroke = isEnabled() ? lerpColor(border, accent, borderMix) : border;

    const QRectF r = box.adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(QPen(stroke, 1.0));
    p.setBrush(fill);
    p.drawRoundedRect(r, 4.0, 4.0);

    if (progress <= 0.001) {
        return;
    }

    // 勾：短边占进度前 35%，长边占余下 65%，圆头笔画
    const QPointF a(r.left() + r.width() * 0.22, r.top() + r.height() * 0.52);
    const QPointF b(r.left() + r.width() * 0.43, r.top() + r.height() * 0.72);
    const QPointF c(r.left() + r.width() * 0.79, r.top() + r.height() * 0.30);
    constexpr double kShortSegment = 0.35;

    QPainterPath path(a);
    if (progress <= kShortSegment) {
        path.lineTo(lerpPoint(a, b, progress / kShortSegment));
    } else {
        path.lineTo(b);
        path.lineTo(lerpPoint(b, c, (progress - kShortSegment) / (1.0 - kShortSegment)));
    }

    QColor ink = isEnabled() ? Qt::white : QColor(QStringLiteral("#9CA3AF"));
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(ink, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPath(path);
}

void AnimatedCheckBox::animateCheckTo(double target)
{
    if (m_checkAnim) {
        m_checkAnim->stop();
    }
    // 不可见（构造期载入配置）或系统关动画：直接落终态
    if (!isVisible() || !MotionTokens::animationsEnabled()) {
        m_progress = target;
        update();
        return;
    }
    if (!m_checkAnim) {
        m_checkAnim = new QVariantAnimation(this);
        m_checkAnim->setEasingCurve(MotionTokens::standard());
        connect(m_checkAnim, &QVariantAnimation::valueChanged, this,
                [this](const QVariant& value) {
                    m_progress = value.toDouble();
                    update();
                });
    }
    m_checkAnim->setDuration(MotionTokens::duration(120));
    m_checkAnim->setStartValue(m_progress);
    m_checkAnim->setEndValue(target);
    m_checkAnim->start();
}

void AnimatedCheckBox::animateHoverTo(double target)
{
    if (m_hoverAnim) {
        m_hoverAnim->stop();
    }
    if (!MotionTokens::animationsEnabled()) {
        m_hover = target;
        update();
        return;
    }
    if (!m_hoverAnim) {
        m_hoverAnim = new QVariantAnimation(this);
        m_hoverAnim->setEasingCurve(MotionTokens::standard());
        connect(m_hoverAnim, &QVariantAnimation::valueChanged, this,
                [this](const QVariant& value) {
                    m_hover = value.toDouble();
                    update();
                });
    }
    m_hoverAnim->setDuration(MotionTokens::duration(MotionTokens::kStateLayerMs));
    m_hoverAnim->setStartValue(m_hover);
    m_hoverAnim->setEndValue(target);
    m_hoverAnim->start();
}

void AnimatedCheckBox::enterEvent(QEnterEvent* event)
{
    animateHoverTo(1.0);
    QCheckBox::enterEvent(event);
}

void AnimatedCheckBox::leaveEvent(QEvent* event)
{
    animateHoverTo(0.0);
    QCheckBox::leaveEvent(event);
}

void AnimatedCheckBox::changeEvent(QEvent* event)
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
    QCheckBox::changeEvent(event);
}
