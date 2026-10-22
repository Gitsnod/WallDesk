#include "AnimatedComboBox.h"
#include "../AppTheme.h"
#include "Chevron.h"
#include "MotionTokens.h"

#include <QAbstractAnimation>
#include <QAbstractItemView>
#include <QLayout>
#include <QPainter>
#include <QPainterPath>
#include <QRegion>
#include <QStyledItemDelegate>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QScreen>
#include <QStyle>

namespace {

/** 去掉焦点矩形：弹出列表拿到焦点时，Fusion 会在当前项外面画一圈灰框，
 *  和圆角面板摆在一起像没画完的边框。选中态仍由 QSS 的 selection 色负责。 */
class FlatItemDelegate : public QStyledItemDelegate {
public:
    explicit FlatItemDelegate(QObject* parent)
        : QStyledItemDelegate(parent)
    {
    }

protected:
    void initStyleOption(QStyleOptionViewItem* option, const QModelIndex& index) const override
    {
        QStyledItemDelegate::initStyleOption(option, index);
        option->state &= ~QStyle::State_HasFocus;
    }
};

} // namespace

AnimatedComboBox::AnimatedComboBox(QWidget* parent)
    : QComboBox(parent)
    , m_popup(nullptr)
    , m_popAnim(nullptr)
    , m_arrowAnim(nullptr)
{
}

AnimatedComboBox::~AnimatedComboBox()
{
    delete m_popAnim;
    m_popAnim = nullptr;
}

bool AnimatedComboBox::eventFilter(QObject* watched, QEvent* event)
{
    if (m_popup && watched == m_popup) {
        auto* w = qobject_cast<QWidget*>(watched);
        if (!w) {
            return false;
        }
        if (event->type() == QEvent::Paint) {
            // 自己画弹出面板：圆角 + 1px 描边。返回 true 挡掉基类那次方角绘制，
            // 子控件（列表）是独立 widget，照常自己画，不受影响。
            QPainter p(w);
            p.setRenderHint(QPainter::Antialiasing, true);
            // 描边往内收 1px：mask 正好卡在圆角边界上，画在 0.5px 处会被切掉一半
            const QRectF r = QRectF(w->rect()).adjusted(1.0, 1.0, -1.0, -1.0);
            p.setPen(QPen(AppTheme::buttonBorderColor(AppTheme::current()), 1.0));
            p.setBrush(AppTheme::buttonBaseColor(AppTheme::current()));
            p.drawRoundedRect(r, 9.0, 9.0);
            return true;
        }
        if (event->type() == QEvent::Resize) {
            applyPopupMask();
        }
    }
    return QComboBox::eventFilter(watched, event);
}

void AnimatedComboBox::applyPopupMask()
{
    if (!m_popup) {
        return;
    }
    QPainterPath path;
    path.addRoundedRect(QRectF(m_popup->rect()).adjusted(0, 0, 0, 0), 10.0, 10.0);
    m_popup->setMask(QRegion(path.toFillPolygon().toPolygon()));
}

void AnimatedComboBox::animateArrow(bool open)
{
    const double target = open ? 1.0 : 0.0;
    if (!MotionTokens::animationsEnabled() || !isVisible()) {
        if (m_arrowAnim) {
            m_arrowAnim->stop();
        }
        m_arrow = target;
        update();
        return;
    }
    if (!m_arrowAnim) {
        m_arrowAnim = new QVariantAnimation(this);
        m_arrowAnim->setEasingCurve(MotionTokens::standard());
        connect(m_arrowAnim, &QVariantAnimation::valueChanged, this,
                [this](const QVariant& v) {
                    m_arrow = v.toDouble();
                    update();
                });
    }
    m_arrowAnim->stop();
    m_arrowAnim->setDuration(MotionTokens::duration(180));
    m_arrowAnim->setStartValue(m_arrow);
    m_arrowAnim->setEndValue(target);
    m_arrowAnim->start();
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

void AnimatedComboBox::configurePopup()
{
    QWidget* container = popupContainer();
    if (!container || m_popupStyled) {
        return;
    }
    m_popupStyled = true;

    // 弹出层是独立顶层窗。默认是「不透明白底 + 直角」，和 8px 圆角的输入框摆在一起
    // 像两个年代的控件。开透明背景后由 QSS 的 #comboPopup 画圆角：
    // 没有透明底的话，圆角外侧那几个像素仍会被窗口底色填满，看着还是方的。
    container->setObjectName(QStringLiteral("comboPopup"));
    container->setAutoFillBackground(false);
    container->setWindowFlag(Qt::NoDropShadowWindowHint, true);
    // QComboBoxPrivateContainer 自己重写了 paintEvent（画一个方角菜单面板），
    // QSS 的 background / border-radius 到不了它。这里接管绘制（见 eventFilter），
    // 再用窗口 mask 把四角切掉 —— 容器是原生窗口，不 mask 的话圆角外侧仍是实色。
    container->installEventFilter(this);
    if (QLayout* lay = container->layout()) {
        lay->setContentsMargins(4, 4, 4, 4);
    }
    // 改完 objectName 必须重新 polish，否则 QSS 的 #comboPopup 不会重新匹配
    container->style()->unpolish(container);
    container->style()->polish(container);
    container->update();

    if (QAbstractItemView* v = view()) {
        v->setStyleSheet(QStringLiteral(
            "background: transparent; border: none; outline: none;"));
        v->setItemDelegate(new FlatItemDelegate(v));
        // 列表宽度跟下拉框对齐：Qt 默认按最长条目算，设置页里会比输入框窄一截
        v->setMinimumWidth(qMax(0, width() - 12));
    }
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

    // 只做「淡入 + 上移 6px」：早先按高度 0→H 展开，弹层会在动画期间被 Qt
    // 重新约束尺寸，最后停在半覆盖下拉框的位置（截图里压住了上一行）。
    // 位移 + 透明度不碰尺寸，落点稳，观感也够。
    const QRect target = container->geometry();
    const int slide = 6;
    const QRect start = entering
        ? QRect(target.x(), target.y() - slide, target.width(), target.height())
        : target;

    auto* slideAnim = new QPropertyAnimation(container, "geometry");
    slideAnim->setDuration(entering ? MotionTokens::kPopupEnterMs
                                    : MotionTokens::kPopupExitMs);
    slideAnim->setStartValue(start);
    slideAnim->setEndValue(entering
                               ? target
                               : QRect(target.x(), target.y() - slide,
                                       target.width(), target.height()));
    slideAnim->setEasingCurve(entering ? MotionTokens::standardDecelerate()
                                       : MotionTokens::standardAccelerate());

    auto* fadeAnim = new QPropertyAnimation(container, "windowOpacity");
    fadeAnim->setDuration(slideAnim->duration());
    fadeAnim->setStartValue(entering ? 0.0 : 1.0);
    fadeAnim->setEndValue(entering ? 1.0 : 0.0);
    fadeAnim->setEasingCurve(slideAnim->easingCurve());

    m_popAnim = new QParallelAnimationGroup(this);
    m_popAnim->addAnimation(slideAnim);
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

void AnimatedComboBox::placePopup()
{
    QWidget* container = popupContainer();
    if (!container) {
        return;
    }
    const QRect g = container->geometry();
    const QPoint anchorTop = mapToGlobal(QPoint(0, 0));
    const QPoint anchorBottom = mapToGlobal(QPoint(0, height()));
    const int popupW = qMax(g.width(), width());

    const QRect avail = screen() ? screen()->availableGeometry()
                                 : QRect(anchorTop, QSize(popupW, 400));
    // 下面放不下就翻到上面；两侧夹在可用区里，避免贴边或跑出屏幕
    int y = anchorBottom.y() + 2;
    if (y + g.height() > avail.bottom() - 4) {
        y = anchorTop.y() - g.height() - 2;
    }
    int x = qBound(avail.left() + 4, anchorTop.x(), avail.right() - popupW - 4);

    // Qt 自己的落点会偏（实测压住下拉框上半截），这里自己算一次，确定性更强
    container->setGeometry(x, y, popupW, g.height());
    applyPopupMask();
}

void AnimatedComboBox::showPopup()
{
    configurePopup();
    QComboBox::showPopup();
    placePopup();
    animateArrow(true);
    animatePopup(true);
}

void AnimatedComboBox::hidePopup()
{
    if (m_closing) {
        return; // 收起动画中重入：忽略，等 finished 后真正隐藏
    }
    animateArrow(false);
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
    // 底色已由 QSS 换成浅灰填充（与白底输入框区分），箭头才是第二层提示。
    constexpr qreal kArrowZone = 26.0;
    const QRectF arrowBox(width() - kArrowZone + 6.0, 0.0, 12.0, height());

    const ThemeKind tk = AppTheme::current();
    QColor arrowColor = AppTheme::subTextColor(tk);
    if (!isEnabled()) {
        arrowColor.setAlphaF(0.45);
    } else if (m_arrow > 0.01 || hasFocus() || underMouse()) {
        // 展开中/聚焦/悬停：箭头转主色，明确「这里可以点开」
        arrowColor = QColor(AppTheme::accentColor(tk));
    }
    Chevron::drawDown(p, arrowBox, arrowColor, 1.6, 180.0 * m_arrow);

    if (!isEnabled() || !hasFocus()) {
        return;
    }
    // 焦点环：圆角必须与 QSS 的 8px 一致，否则会看到里外两层圆角打架
    QColor ring = QColor(AppTheme::accentColor(tk));
    ring.setAlphaF(0.5);
    p.setPen(QPen(ring, 2.0));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0), 8.0, 8.0);
}
