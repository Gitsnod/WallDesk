#include "AnimatedPushButton.h"
#include "../AppTheme.h"
#include "MotionTokens.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <QStyleOptionButton>
#include <QTimer>
#include <QVariantAnimation>

AnimatedPushButton::AnimatedPushButton(QWidget* parent)
    : QPushButton(parent)
    , m_layerAnim(nullptr)
    , m_rippleAnim(nullptr)
{
}

AnimatedPushButton::AnimatedPushButton(const QString& text, QWidget* parent)
    : QPushButton(text, parent)
    , m_layerAnim(nullptr)
    , m_rippleAnim(nullptr)
{
}

AnimatedPushButton::~AnimatedPushButton()
{
    stopAnimations();
}

bool AnimatedPushButton::isPrimary() const
{
    return objectName() == QLatin1String("primary");
}

QColor AnimatedPushButton::baseFill() const
{
    if (isPrimary()) {
        // 禁用态用 accentSoft（与 QSS #primary:disabled 同色），但形状仍由自绘保证
        return isEnabled() ? QColor(AppTheme::accentColor(AppTheme::current()))
                           : AppTheme::accentSoftColor(AppTheme::current());
    }
    return AppTheme::buttonBaseColor(AppTheme::current());
}

QColor AnimatedPushButton::borderColor() const
{
    if (isPrimary()) {
        return isEnabled() ? QColor(AppTheme::accentColor(AppTheme::current()))
                           : AppTheme::accentSoftColor(AppTheme::current());
    }
    return AppTheme::buttonBorderColor(AppTheme::current());
}

QColor AnimatedPushButton::layerColor() const
{
    if (isPrimary()) {
        return QColor(Qt::white); // M3 on-primary
    }
    return QColor(AppTheme::accentColor(AppTheme::current()));
}

double AnimatedPushButton::currentLayer() const
{
    return m_layerValue;
}

double AnimatedPushButton::rippleProgress() const
{
    return m_rippleAnim ? m_rippleAnim->currentValue().toDouble() : 0.0;
}

void AnimatedPushButton::animateLayerTo(double target)
{
    if (!MotionTokens::animationsEnabled()) {
        // 关动画：直接落终态
        if (m_layerAnim) {
            m_layerAnim->stop();
        }
        m_layerValue = target;
        update();
        return;
    }
    const double from = m_layerValue;
    if (qFuzzyCompare(1.0 + from, 1.0 + target)) {
        return;   // 已在目标态，避免空转
    }
    if (!m_layerAnim) {
        m_layerAnim = new QVariantAnimation(this);
        m_layerAnim->setEasingCurve(MotionTokens::standard());
        connect(m_layerAnim, &QVariantAnimation::valueChanged,
                this, [this]() { m_layerValue = m_layerAnim->currentValue().toDouble(); update(); });
        connect(m_layerAnim, &QVariantAnimation::finished,
                this, [this]() { m_layerValue = m_layerAnim->endValue().toDouble(); update(); });
    }
    m_layerAnim->stop();
    m_layerAnim->setDuration(MotionTokens::duration(MotionTokens::kStateLayerMs));
    m_layerAnim->setStartValue(from);
    m_layerAnim->setEndValue(target);
    m_layerAnim->start();
}

void AnimatedPushButton::startRipple(const QPointF& localPos)
{
    if (!MotionTokens::animationsEnabled()) {
        return; // 关动画：不画圈，只保留 state layer 终态
    }
    m_rippleOrigin = localPos;
    // 终点半径 = 触点到按钮最远角的距离（保证铺满，M3 行为）
    const QPointF corners[4] = {
        QPointF(0.0, 0.0),
        QPointF(width(), 0.0),
        QPointF(0.0, height()),
        QPointF(width(), height())
    };
    double maxDist = 0.0;
    for (const QPointF& corner : corners) {
        const double dist = QLineF(localPos, corner).length();
        if (dist > maxDist) {
            maxDist = dist;
        }
    }
    m_rippleMaxRadius = maxDist;
    m_rippleLevel = MotionTokens::kRippleOpacity;
    m_fadingRipple = false;

    if (!m_rippleAnim) {
        m_rippleAnim = new QVariantAnimation(this);
        m_rippleAnim->setEasingCurve(MotionTokens::rippleCurve());
        connect(m_rippleAnim, &QVariantAnimation::valueChanged,
                this, [this]() { update(); });
        connect(m_rippleAnim, &QVariantAnimation::finished,
                this, [this]() { m_rippleLevel = 0.0; update(); });
    }
    m_rippleAnim->stop();
    m_rippleAnim->setDuration(MotionTokens::duration(MotionTokens::kRippleMs));
    m_rippleAnim->setStartValue(0.0);
    m_rippleAnim->setEndValue(1.0);
    m_rippleAnim->start();
}

void AnimatedPushButton::fadeOutRipple()
{
    if (!m_rippleAnim || m_fadingRipple) {
        return;
    }
    const double progress = m_rippleAnim->currentValue().toDouble();
    if (progress >= 1.0) {
        m_rippleLevel = 0.0;
        update();
        return;
    }
    if (!MotionTokens::animationsEnabled()) {
        m_rippleLevel = 0.0;
        m_rippleAnim->stop();
        update();
        return;
    }
    // 抬手不打断扩散：把当前进度映射到剩余淡出时长（按剩余距离等比例），
    // 动画值继续走向 1.0，paint 侧 alpha = level * (1 - progress) 归零
    m_fadingRipple = true;
    m_rippleAnim->stop();
    m_rippleAnim->setEasingCurve(MotionTokens::standardAccelerate());
    m_rippleAnim->setDuration(qMax(1, int(MotionTokens::duration(
        MotionTokens::kRippleFadeMs) * (1.0 - progress))));
    m_rippleAnim->setStartValue(progress);
    m_rippleAnim->setEndValue(1.0);
    m_rippleAnim->start();
}

void AnimatedPushButton::stopAnimations()
{
    if (m_layerAnim) {
        m_layerAnim->stop();
    }
    if (m_rippleAnim) {
        m_rippleAnim->stop();
    }
    m_layerValue = 0.0;
    m_rippleLevel = 0.0;
    m_fadingRipple = false;
}

void AnimatedPushButton::paintEvent(QPaintEvent* event)
{
    // 禁用态也自绘：交给基类会走 QSS，而 #primary:disabled 这类更具体的选择器
    // 不带 border-radius，渲染出来是直角，与其它按钮形状不一致。
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QStyleOptionButton opt;
    initStyleOption(&opt);

    // 20px 半径 = 32px 控件高下的等效 full（M3 按钮默认 full，
    // full 取过大值边框抗锯齿发虚，用高度 2/3 收敛）
    const qreal radius = 10.0;
    const QRectF bg = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);

    // 底色 + 边框（替代 QSS background/border，绕过其无过渡的硬切）
    p.setPen(Qt::NoPen);
    p.setBrush(baseFill());
    p.drawRoundedRect(bg, radius, radius);
    p.setPen(QPen(borderColor(), 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(bg, radius, radius);

    // state layer（M3：hover 8% / pressed 10% 叠加，不改整块色）
    const double layer = currentLayer();
    if (layer > 0.001) {
        QColor c = layerColor();
        c.setAlphaF(layer);
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawRoundedRect(bg, radius, radius);
    }

    // ripple：裁剪在圆角矩形内，半径/透明度双插值（expand + fade）
    const double progress = rippleProgress();
    if (m_rippleLevel > 0.001 && progress > 0.0 && progress < 1.0) {
        QPainterPath clip;
        clip.addRoundedRect(bg, radius, radius);
        p.setClipPath(clip);
        QColor rc = layerColor();
        rc.setAlphaF(m_rippleLevel * (1.0 - progress));
        p.setPen(Qt::NoPen);
        p.setBrush(rc);
        p.drawEllipse(m_rippleOrigin, m_rippleMaxRadius * progress,
                      m_rippleMaxRadius * progress);
        p.setClipping(false);
    }

    // 焦点环（QSS 无 outline，必须自绘；键盘可达性）
    if (hasFocus() && isEnabled()) {
        QColor ring = QColor(AppTheme::accentColor(AppTheme::current()));
        ring.setAlphaF(0.5);
        p.setPen(QPen(ring, 2.0));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0),
                          radius, radius);
    }

    // 文字/图标交给 QStyle 画（QSS color/font 经 palette 生效）；
    // pressed 时下沉 1px（paint 内 offset，不动 geometry）
    opt.rect.translate(0, m_pressed ? 1 : 0);
    style()->drawControl(QStyle::CE_PushButtonLabel, &opt, &p, this);
}

void AnimatedPushButton::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && isEnabled()) {
        m_pressed = true;
        m_pressPos = event->position();
        animateLayerTo(MotionTokens::kLayerPressed);
        // M3：按压 >=225ms 才触发 ripple；短按只有 state layer
        QTimer::singleShot(MotionTokens::kRippleMinPressMs, this, [this]() {
            if (m_pressed) {
                startRipple(m_pressPos);
            }
        });
    }
    QPushButton::mousePressEvent(event);
}

void AnimatedPushButton::mouseReleaseEvent(QMouseEvent* event)
{
    const bool wasPressed = m_pressed;
    m_pressed = false;
    if (wasPressed) {
        // 抬手不打断扩散：未完成的 ripple 接 200ms 淡出
        if (m_rippleAnim && !m_fadingRipple
            && m_rippleAnim->state() == QAbstractAnimation::Running) {
            fadeOutRipple();
        }
        animateLayerTo(underMouse() ? MotionTokens::kLayerHover : 0.0);
    }
    QPushButton::mouseReleaseEvent(event);
}

void AnimatedPushButton::enterEvent(QEnterEvent* event)
{
    QPushButton::enterEvent(event);
    if (isEnabled()) {
        animateLayerTo(m_pressed ? MotionTokens::kLayerPressed
                                 : MotionTokens::kLayerHover);
    }
}

void AnimatedPushButton::leaveEvent(QEvent* event)
{
    QPushButton::leaveEvent(event);
    if (!m_pressed) {
        animateLayerTo(0.0);
    }
}

void AnimatedPushButton::changeEvent(QEvent* event)
{
    QPushButton::changeEvent(event);
    if (event->type() == QEvent::EnabledChange && !isEnabled()) {
        // 禁用时立即清理：按压途中禁用会留下残留圈画在灰化控件上
        m_pressed = false;
        stopAnimations();
        update();
    }
}
