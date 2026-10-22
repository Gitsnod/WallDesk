#include "MenuFade.h"

#include "MotionTokens.h"

#include <QApplication>
#include <QEvent>
#include <QMenu>
#include <QVariantAnimation>
#include <QWidget>

namespace {
const char* const kAnimName = "menuFade";
}

MenuFade::MenuFade(QObject* parent)
    : QObject(parent)
{
}

MenuFade* MenuFade::instance()
{
    static MenuFade* s_instance = nullptr;
    if (!s_instance) {
        s_instance = new MenuFade(qApp);
        qApp->installEventFilter(s_instance);
    }
    return s_instance;
}

void MenuFade::install()
{
    if (!qApp) {
        return;
    }
    instance();
}

bool MenuFade::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::Show || event->type() == QEvent::Hide) {
        if (auto* menu = qobject_cast<QMenu*>(watched)) {
            if (event->type() == QEvent::Show) {
                fadeIn(menu);
            } else {
                reset(menu);
            }
        }
    }
    return QObject::eventFilter(watched, event);
}

void MenuFade::fadeIn(QWidget* menu)
{
    if (!menu->isWindow() || !MotionTokens::animationsEnabled()) {
        menu->setWindowOpacity(1.0);
        return;
    }

    auto* anim = menu->findChild<QVariantAnimation*>(QString::fromLatin1(kAnimName),
                                                     Qt::FindDirectChildrenOnly);
    if (!anim) {
        anim = new QVariantAnimation(menu);
        anim->setObjectName(QString::fromLatin1(kAnimName));
        anim->setEasingCurve(MotionTokens::standardDecelerate());
        connect(anim, &QVariantAnimation::valueChanged, menu,
                [menu](const QVariant& value) {
                    menu->setWindowOpacity(value.toDouble());
                });
        // 兜底：任何形式的结束都落到完全不透明，杜绝「菜单全透明」半态
        connect(anim, &QVariantAnimation::finished, menu,
                [menu]() { menu->setWindowOpacity(1.0); });
    }

    anim->stop();
    menu->setWindowOpacity(0.0);
    anim->setDuration(MotionTokens::duration(200));
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->start();
}

void MenuFade::reset(QWidget* menu)
{
    if (auto* anim = menu->findChild<QVariantAnimation*>(QString::fromLatin1(kAnimName),
                                                         Qt::FindDirectChildrenOnly)) {
        anim->stop();
    }
    menu->setWindowOpacity(1.0);
}
