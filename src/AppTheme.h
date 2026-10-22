#pragma once

#include <QColor>
#include <QString>
#include <QStringList>

/** 界面主题。System 表示跟随 Windows 的「应用使用浅色模式」设置。 */
enum class ThemeKind {
    Light = 0,
    Dark = 1,
    System = 2
};

class AppTheme {
public:
    static QStringList displayNames();
    static ThemeKind fromIndex(int index);
    static int toIndex(ThemeKind kind);

    /** System 会被解析成具体的 Light / Dark。 */
    static ThemeKind resolve(ThemeKind kind);
    /** 读取注册表 AppsUseLightTheme 判断系统偏好。 */
    static bool systemPrefersLight();

    /** 应用主题：Fusion 基础样式 + 调色板 + 样式表。 */
    static void apply(ThemeKind kind);

    /** 主色调，供自绘图标等复用。 */
    static QString accentColor(ThemeKind kind);

    /** 当前生效主题（apply() 时记录的 resolve 后值），供动画控件现取色值。 */
    static ThemeKind current();

    /** 次级按钮底色（card 色），AnimatedPushButton 自绘用。 */
    static QColor buttonBaseColor(ThemeKind kind);

    /** 次级按钮边框色（border 色），AnimatedPushButton 自绘用。 */
    static QColor buttonBorderColor(ThemeKind kind);

    /** 主色浅底（accentSoft），禁用态主按钮自绘用。 */
    static QColor accentSoftColor(ThemeKind kind);

private:
    static QString styleSheet(ThemeKind kind);
};
