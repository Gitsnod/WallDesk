#include "AppTheme.h"

#include <QApplication>
#include <QHash>
#include <QPalette>
#include <QSettings>
#include <QStyle>
#include <QStyleFactory>

namespace {

/** 主题配色表。集中定义，避免样式表里散落魔法色值。 */
struct Palette {
    const char* window;
    const char* base;
    const char* altBase;
    const char* text;
    const char* subText;
    const char* border;
    const char* card;
    const char* hover;
    const char* accent;
    const char* accentHover;
    const char* accentSoft;
    const char* disabled;
    const char* tooltipBase;
    const char* tooltipText;
};

const Palette kLight = {
    "#F5F7FA", // window
    "#FFFFFF", // base
    "#F8FAFC", // altBase
    "#1F2937", // text
    "#64748B", // subText
    "#E2E8F0", // border
    "#FFFFFF", // card
    "#F1F5F9", // hover
    "#2563EB", // accent
    "#1D4ED8", // accentHover
    "#DBEAFE", // accentSoft
    "#94A3B8", // disabled
    "#1F2937", // tooltipBase
    "#F8FAFC", // tooltipText
};

const Palette kDark = {
    "#0F172A", // window
    "#1E293B", // base
    "#172033", // altBase
    "#E2E8F0", // text
    "#94A3B8", // subText
    "#334155", // border
    "#1E293B", // card
    "#243449", // hover
    "#3B82F6", // accent
    "#60A5FA", // accentHover
    "#1E3A8A", // accentSoft
    "#64748B", // disabled
    "#334155", // tooltipBase
    "#E2E8F0", // tooltipText
};

const Palette& paletteFor(ThemeKind kind)
{
    return (kind == ThemeKind::Dark) ? kDark : kLight;
}

/** 样式表模板，占位符形如 {window}，由 applyTemplate 替换。 */
const char* const kTemplate = R"(
QMainWindow, QDialog { background: {window}; }
QWidget { color: {text}; }

/* ---- 分组卡片 ---- */
QGroupBox {
    background: {card};
    border: 1px solid {border};
    border-radius: 10px;
    margin-top: 16px;
    padding: 14px 12px 12px 12px;
    font-weight: 600;
}
QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    left: 14px;
    padding: 0 6px;
    color: {subText};
}

/* ---- 按钮 ---- */
QPushButton {
    background: {card};
    border: 1px solid {border};
    border-radius: 8px;
    padding: 7px 14px;
    min-height: 16px;
}
QPushButton:hover { border-color: {accent}; background: {hover}; }
QPushButton:pressed { background: {accentSoft}; }
QPushButton:disabled { color: {disabled}; border-color: {border}; background: {card}; }
QPushButton#primary {
    background: {accent};
    border: 1px solid {accent};
    color: #FFFFFF;
    font-weight: 600;
}
QPushButton#primary:hover { background: {accentHover}; border-color: {accentHover}; }
QPushButton#primary:disabled { background: {accentSoft}; border-color: {accentSoft}; color: {disabled}; }
QPushButton#danger:hover { border-color: #DC2626; color: #DC2626; }

/* ---- 输入控件 ---- */
QComboBox, QSpinBox, QLineEdit {
    background: {card};
    border: 1px solid {border};
    border-radius: 8px;
    padding: 5px 10px;
    min-height: 18px;
}
QComboBox:hover, QSpinBox:hover { border-color: {accent}; }
QComboBox QAbstractItemView {
    background: {card};
    border: 1px solid {border};
    border-radius: 8px;
    selection-background-color: {accentSoft};
    selection-color: {text};
    padding: 4px;
}
QComboBox::drop-down { border: none; width: 18px; }
QSpinBox::up-button, QSpinBox::down-button { width: 16px; border: none; background: transparent; }

/* ---- 列表（缩略图画廊） ---- */
QListWidget {
    background: {card};
    border: 1px solid {border};
    border-radius: 10px;
    padding: 6px;
    outline: none;
}
/* 画廊项由自定义委托绘制（圆角卡片 + 悬停/选中态），
   这里一律透明，避免方形高亮从圆角卡片底下露出来。 */
QListWidget::item { background: transparent; border: none; padding: 2px; }
QListWidget::item:hover { background: transparent; }
QListWidget::item:selected { background: transparent; color: {text}; }

/* ---- 滑块 ---- */
QSlider::groove:horizontal { height: 4px; background: {border}; border-radius: 2px; }
QSlider::sub-page:horizontal { background: {accent}; border-radius: 2px; }
QSlider::handle:horizontal {
    width: 14px; margin: -5px 0; border-radius: 7px;
    background: {card}; border: 2px solid {accent};
}
QSlider::handle:horizontal:hover { background: {accentSoft}; }

/* ---- 复选框 ---- */
QCheckBox { spacing: 8px; }
QCheckBox::indicator { width: 16px; height: 16px; border-radius: 4px; border: 1px solid {border}; background: {card}; }
QCheckBox::indicator:hover { border-color: {accent}; }
QCheckBox::indicator:checked { background: {accent}; border-color: {accent}; }
QCheckBox:disabled { color: {disabled}; }

/* ---- 滚动条 ---- */
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: {border}; border-radius: 5px; min-height: 28px; }
QScrollBar::handle:vertical:hover { background: {disabled}; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle:horizontal { background: {border}; border-radius: 5px; min-width: 28px; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0px; }
QScrollArea { border: none; background: transparent; }

/* ---- 其它 ---- */
QSplitter::handle { background: transparent; }
QProgressBar { border: none; background: {border}; border-radius: 3px; height: 6px; }
QProgressBar::chunk { background: {accent}; border-radius: 3px; }
QLabel#statusLabel { color: {subText}; padding: 2px 0; }
QLabel#statusLabel[warn="true"] { color: #DC2626; }
QLabel#titleLabel { font-size: 17px; font-weight: 700; }
QMenu { background: {card}; border: 1px solid {border}; border-radius: 8px; padding: 6px; }
QMenu::item { padding: 6px 20px; border-radius: 6px; }
QMenu::item:selected { background: {accentSoft}; }
QToolTip { background: {tooltipBase}; color: {tooltipText}; border: none; padding: 5px 8px; }

/* ---- 菜单栏与状态栏（V4.1 分级布局） ---- */
QMenuBar { background: {window}; border-bottom: 1px solid {border}; padding: 2px 8px; }
QMenuBar::item { padding: 5px 10px; border-radius: 6px; background: transparent; }
QMenuBar::item:selected { background: {hover}; }
QStatusBar { background: {window}; color: {subText}; border-top: 1px solid {border}; }
QStatusBar::item { border: none; }
QStatusBar QLabel { padding: 0 4px; }

/* ---- 左侧导航栏 ---- */
QListWidget#navRail {
    background: {window};
    border: none;
    border-right: 1px solid {border};
    padding: 12px 8px;
    outline: none;
    font-weight: 600;
}
QListWidget#navRail::item {
    padding: 10px 12px;
    margin: 2px 4px;
    border-radius: 8px;
    color: {subText};
}
QListWidget#navRail::item:hover { background: {hover}; color: {text}; }
QListWidget#navRail::item:selected { background: {accentSoft}; color: {text}; }

/* ---- 顶部贯穿工具条 ---- */
QWidget#mainToolbar {
    background: {card};
    border-bottom: 1px solid {border};
}
QFrame#toolbarSep {
    background: {border};
    max-width: 1px;
    min-width: 1px;
    margin-left: 8px;
    margin-right: 8px;
}

/* ---- 页头 / 空状态（V4.5） ---- */
QLabel#pageTitle { font-size: 16px; font-weight: 700; color: {text}; padding: 0 2px; }
QWidget#emptyHint { background: transparent; }
QLabel#emptyTitle { font-size: 15px; font-weight: 600; color: {text}; padding-top: 6px; }
QLabel#emptySub { color: {subText}; }
)";

/** 把 {key} 占位符替换成实际色值。用花括号而非 %1，避免 QString::arg 的参数个数限制。 */
QString applyTemplate(const QString& tpl, const QHash<QString, QString>& vars)
{
    QString out = tpl;
    for (auto it = vars.constBegin(); it != vars.constEnd(); ++it) {
        out.replace(QStringLiteral("{") + it.key() + QStringLiteral("}"), it.value());
    }
    return out;
}

QHash<QString, QString> variablesFor(const Palette& c)
{
    const auto q = [](const char* hex) { return QString::fromLatin1(hex); };
    return {
        {QStringLiteral("window"), q(c.window)},
        {QStringLiteral("base"), q(c.base)},
        {QStringLiteral("subText"), q(c.subText)},
        {QStringLiteral("border"), q(c.border)},
        {QStringLiteral("card"), q(c.card)},
        {QStringLiteral("hover"), q(c.hover)},
        {QStringLiteral("accent"), q(c.accent)},
        {QStringLiteral("accentHover"), q(c.accentHover)},
        {QStringLiteral("accentSoft"), q(c.accentSoft)},
        {QStringLiteral("disabled"), q(c.disabled)},
        {QStringLiteral("text"), q(c.text)},
        {QStringLiteral("tooltipBase"), q(c.tooltipBase)},
        {QStringLiteral("tooltipText"), q(c.tooltipText)},
    };
}

} // namespace

QStringList AppTheme::displayNames()
{
    return {QStringLiteral("浅色"), QStringLiteral("深色"), QStringLiteral("跟随系统")};
}

ThemeKind AppTheme::fromIndex(int index)
{
    switch (index) {
    case 0: return ThemeKind::Light;
    case 1: return ThemeKind::Dark;
    default: return ThemeKind::System;
    }
}

int AppTheme::toIndex(ThemeKind kind)
{
    return static_cast<int>(kind);
}

bool AppTheme::systemPrefersLight()
{
    QSettings reg(QStringLiteral(R"(HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize)"),
                  QSettings::NativeFormat);
    const QVariant value = reg.value(QStringLiteral("AppsUseLightTheme"));
    if (!value.isValid()) {
        return true; // 读不到就按浅色处理
    }
    return value.toInt() != 0;
}

ThemeKind AppTheme::resolve(ThemeKind kind)
{
    if (kind == ThemeKind::System) {
        return systemPrefersLight() ? ThemeKind::Light : ThemeKind::Dark;
    }
    return kind;
}

QString AppTheme::accentColor(ThemeKind kind)
{
    return QString::fromLatin1(paletteFor(resolve(kind)).accent);
}

QString AppTheme::styleSheet(ThemeKind kind)
{
    return applyTemplate(QString::fromLatin1(kTemplate), variablesFor(paletteFor(kind)));
}

void AppTheme::apply(ThemeKind kind)
{
    const ThemeKind resolved = resolve(kind);
    const Palette& c = paletteFor(resolved);

    // Fusion 是 Qt 内置样式里唯一能配合调色板把深色主题渲染干净的一个
    if (QStyle* fusion = QStyleFactory::create(QStringLiteral("Fusion"))) {
        QApplication::setStyle(fusion);
    }

    const auto color = [](const char* hex) { return QColor(QString::fromLatin1(hex)); };
    QPalette pal;
    pal.setColor(QPalette::Window, color(c.window));
    pal.setColor(QPalette::WindowText, color(c.text));
    pal.setColor(QPalette::Base, color(c.base));
    pal.setColor(QPalette::AlternateBase, color(c.altBase));
    pal.setColor(QPalette::Text, color(c.text));
    pal.setColor(QPalette::Button, color(c.card));
    pal.setColor(QPalette::ButtonText, color(c.text));
    pal.setColor(QPalette::Highlight, color(c.accent));
    pal.setColor(QPalette::HighlightedText, QColor(Qt::white));
    pal.setColor(QPalette::Link, color(c.accent));
    pal.setColor(QPalette::ToolTipBase, color(c.tooltipBase));
    pal.setColor(QPalette::ToolTipText, color(c.tooltipText));
    pal.setColor(QPalette::Disabled, QPalette::Text, color(c.disabled));
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, color(c.disabled));
    pal.setColor(QPalette::Disabled, QPalette::WindowText, color(c.disabled));
#if QT_VERSION >= QT_VERSION_CHECK(5, 12, 0)
    pal.setColor(QPalette::PlaceholderText, color(c.disabled));
#endif
    QApplication::setPalette(pal);

    qApp->setStyleSheet(styleSheet(resolved));
}
