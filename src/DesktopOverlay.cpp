#include "DesktopOverlay.h"

#include <windows.h>

#include <QApplication>
#include <QDateTime>
#include <QFontDatabase>
#include <QLocale>
#include <QPainter>
#include <QScreen>
#include <QTimer>

namespace {

/** 中文星期。QLocale 的 longDayName 在部分系统上带「星期」前缀，这里统一手写。 */
const char* weekdayName(int day)
{
    static const char* names[] = {"周一", "周二", "周三", "周四", "周五", "周六", "周日"};
    // Qt: 1=周一 … 7=周日
    const int i = qBound(1, day, 7);
    return names[i - 1];
}

/** 绘制带发光/阴影的文字，保证在浅色或复杂壁纸上都清晰。 */
void drawShadowedText(QPainter& painter, const QRect& rect, int flags, const QString& text,
                      const QColor& color)
{
    painter.save();

    // 外发光：多层半透明黑叠加，形成柔和描边
    for (int radius = 3; radius >= 1; --radius) {
        const int alpha = 60 - radius * 12;
        painter.setPen(QColor(0, 0, 0, alpha));
        for (int dx = -radius; dx <= radius; ++dx) {
            for (int dy = -radius; dy <= radius; ++dy) {
                if (dx == 0 && dy == 0) {
                    continue;
                }
                painter.drawText(rect.translated(dx, dy), flags, text);
            }
        }
    }

    // 正文
    painter.setPen(color);
    painter.drawText(rect, flags, text);
    painter.restore();
}

/** 尝试找一个现代等宽/无衬线字体；找不到就用系统默认。 */
QFont pickFont(int pointSize, bool bold = false)
{
    QFont font;
    const QStringList candidates = {
        QStringLiteral("Microsoft YaHei UI"),
        QStringLiteral("PingFang SC"),
        QStringLiteral("Noto Sans CJK SC"),
        QStringLiteral("Source Han Sans SC"),
        QStringLiteral("SimHei"),
    };
    for (const QString& family : candidates) {
        const int id = QFontDatabase::addApplicationFont(family);
        if (id >= 0 || QFontDatabase::hasFamily(family)) {
            font.setFamily(family);
            break;
        }
    }
    font.setPointSize(pointSize);
    font.setBold(bold);
    font.setStyleStrategy(QFont::PreferAntialias);
    return font;
}

} // namespace

DesktopOverlay::DesktopOverlay()
    : QWidget(nullptr)
{
    setWindowTitle(QStringLiteral("WallDesk 桌面挂件"));
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setWindowFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowDoesNotAcceptFocus
                   | Qt::WindowTransparentForInput);

    m_tick = new QTimer(this);
    m_tick->setInterval(1000);
    connect(m_tick, &QTimer::timeout, this, &DesktopOverlay::onTick);

    m_keeper = new QTimer(this);
    m_keeper->setInterval(2000);
    connect(m_keeper, &QTimer::timeout, this, &DesktopOverlay::onKeepBottom);
}

DesktopOverlay::~DesktopOverlay() = default;

void DesktopOverlay::applySettings(const Settings& settings)
{
    m_settings = settings;
    m_spectrum.fill(0.0f, m_settings.bands);

    if (!m_settings.enabled) {
        m_tick->stop();
        m_keeper->stop();
        hide();
        return;
    }

    updateScreenGeometry();
    show();
    applyWindowStyle();
    m_tick->start();
    m_keeper->start();
    update();
}

void DesktopOverlay::setSpectrum(const QVector<float>& bands)
{
    if (!m_settings.showSpectrum || !isVisible()) {
        return;
    }
    m_spectrum = bands;
    update();
}

void DesktopOverlay::applyWindowStyle()
{
    const HWND hwnd = reinterpret_cast<HWND>(winId());
    if (!hwnd) {
        return;
    }
    const LONG_PTR ex = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    SetWindowLongPtr(hwnd, GWL_EXSTYLE,
                     ex | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW);
    // 放到 Z 序最底：位于其它应用窗口之下，但仍高于桌面壁纸层，所以看得见
    SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOSENDCHANGING);
}

void DesktopOverlay::updateScreenGeometry()
{
    QRect rect;
    const QList<QScreen*> screens = QApplication::screens();
    if (m_settings.screenIndex == -2 || screens.isEmpty()) {
        rect = QApplication::primaryScreen() ? QApplication::primaryScreen()->geometry()
                                             : QRect(0, 0, 1920, 1080);
        if (m_settings.screenIndex == -2) {
            // 虚拟桌面：所有屏幕的外接矩形
            int left = rect.left();
            int top = rect.top();
            int right = rect.right();
            int bottom = rect.bottom();
            for (QScreen* screen : screens) {
                const QRect g = screen->geometry();
                left = qMin(left, g.left());
                top = qMin(top, g.top());
                right = qMax(right, g.right());
                bottom = qMax(bottom, g.bottom());
            }
            rect = QRect(QPoint(left, top), QPoint(right, bottom));
        }
    } else {
        const int index = (m_settings.screenIndex < 0) ? 0 : m_settings.screenIndex;
        QScreen* screen = screens.at(qBound(0, index, screens.size() - 1));
        rect = screen->geometry();
    }
    setGeometry(rect);
}

QRect DesktopOverlay::contentRect() const
{
    const int m = m_settings.margin;
    const QRect full = rect();
    return full.adjusted(m, m, -m, -m);
}

void DesktopOverlay::onTick()
{
    if (isVisible()) {
        update();
    }
}

void DesktopOverlay::onKeepBottom()
{
    if (!isVisible()) {
        return;
    }
    const HWND hwnd = reinterpret_cast<HWND>(winId());
    if (hwnd && GetParent(hwnd) == nullptr) {
        SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOSENDCHANGING);
    }
}

void DesktopOverlay::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const int alpha = qBound(0, m_settings.opacity, 100) * 255 / 100;
    QColor ink = m_settings.color;
    ink.setAlpha(alpha);

    const QRect area = contentRect();
    const QDateTime now = QDateTime::currentDateTime();
    const bool alignRight = (m_settings.position == 1 || m_settings.position == 3);
    const bool alignCenter = (m_settings.position == 4);
    const int textFlags = (alignCenter ? Qt::AlignHCenter
                                       : (alignRight ? Qt::AlignRight : Qt::AlignLeft))
                          | Qt::AlignVCenter;

    // ---- 计算内容尺寸 ----
    int contentW = 0;
    int contentH = 0;
    QFont clockFont = pickFont(m_settings.fontSize + 18, true);
    QFont dateFont = pickFont(m_settings.fontSize, false);
    QFont calHeaderFont = pickFont(m_settings.fontSize - 2, true);
    QFont calDayFont = pickFont(m_settings.fontSize - 6, false);
    QFont textFont = pickFont(m_settings.fontSize, true);

    int clockH = 0, dateH = 0;
    if (m_settings.showClock) {
        QFontMetrics fm(clockFont);
        clockH = fm.height();
        dateH = QFontMetrics(dateFont).height();
        contentW = qMax(contentW, fm.horizontalAdvance(now.toString(QStringLiteral("HH:mm"))));
        contentW = qMax(contentW,
                        QFontMetrics(dateFont)
                            .horizontalAdvance(QStringLiteral("%1  %2")
                                                   .arg(now.toString(QStringLiteral("M 月 d 日")))
                                                   .arg(QString::fromUtf8(
                                                       weekdayName(now.date().dayOfWeek())))));
        contentH += clockH + 4 + dateH + 20;
    }

    int calW = 0, calH = 0;
    QStringList calHeaderLines;
    QStringList calWeekLines;
    if (m_settings.showCalendar) {
        const QDate date = now.date();
        const QDate first(date.year(), date.month(), 1);
        calHeaderLines << QStringLiteral("%1 年 %2 月").arg(date.year()).arg(date.month());
        calHeaderLines << QStringLiteral("一 二 三 四 五 六 日");

        QString week;
        QStringList weeks;
        for (int i = 1; i < first.dayOfWeek(); ++i) {
            week += QStringLiteral("    ");
        }
        for (int day = 1; day <= date.daysInMonth(); ++day) {
            week += QString::asprintf("%2d  ", day);
            if (QDate(date.year(), date.month(), day).dayOfWeek() == 7
                || day == date.daysInMonth()) {
                weeks.append(week);
                week.clear();
            }
        }
        calWeekLines = weeks;

        QFontMetrics hfm(calHeaderFont);
        QFontMetrics dfm(calDayFont);
        for (const QString& line : calHeaderLines) {
            calW = qMax(calW, hfm.horizontalAdvance(line));
            calH += hfm.height() + 4;
        }
        for (const QString& line : calWeekLines) {
            calW = qMax(calW, dfm.horizontalAdvance(line));
            calH += dfm.height() + 3;
        }
        contentW = qMax(contentW, calW);
        contentH += calH + 16;
    }

    int textW = 0, textH = 0;
    QStringList textLines;
    if (m_settings.showText && !m_settings.text.trimmed().isEmpty()) {
        textLines = m_settings.text.split(QLatin1Char('\n'));
        QFontMetrics fm(textFont);
        for (const QString& line : textLines) {
            textW = qMax(textW, fm.horizontalAdvance(line));
            textH += fm.height() + 4;
        }
        contentW = qMax(contentW, textW);
        contentH += textH + 16;
    }

    if (contentW == 0 || contentH == 0) {
        // 只有频谱时继续画，否则直接结束
        if (!m_settings.showSpectrum || m_spectrum.isEmpty()) {
            return;
        }
    }

    // ---- 定位卡片 ----
    const int padX = 24;
    const int padY = 20;
    QRect cardRect;
    switch (m_settings.position) {
    case 0: // 左上
        cardRect = QRect(area.left(), area.top(), contentW + padX * 2, contentH + padY * 2);
        break;
    case 1: // 右上
        cardRect = QRect(area.right() - contentW - padX * 2, area.top(), contentW + padX * 2,
                         contentH + padY * 2);
        break;
    case 2: // 左下
        cardRect = QRect(area.left(), area.bottom() - contentH - padY * 2, contentW + padX * 2,
                         contentH + padY * 2);
        break;
    case 3: // 右下
        cardRect = QRect(area.right() - contentW - padX * 2,
                         area.bottom() - contentH - padY * 2, contentW + padX * 2,
                         contentH + padY * 2);
        break;
    default: // 居中
        cardRect = QRect(area.left() + (area.width() - contentW - padX * 2) / 2,
                         area.top() + (area.height() - contentH - padY * 2) / 2,
                         contentW + padX * 2, contentH + padY * 2);
        break;
    }

    // ---- 画半透明卡片背景 ----
    QColor cardBg(0, 0, 0, qBound(0, alpha * 28 / 100, 70)); // 极淡黑底
    QColor cardBorder = ink;
    cardBorder.setAlpha(alpha / 6);
    painter.setPen(Qt::NoPen);
    painter.setBrush(cardBg);
    painter.drawRoundedRect(cardRect, 16, 16);
    QPen borderPen(cardBorder);
    borderPen.setWidth(1);
    painter.setPen(borderPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(cardRect.adjusted(1, 1, -1, -1), 16, 16);

    // ---- 绘制内容 ----
    int y = cardRect.top() + padY;
    const int left = cardRect.left() + padX;
    const int right = cardRect.right() - padX;

    if (m_settings.showClock) {
        painter.setFont(clockFont);
        const QRect timeRect(left, y, right - left, clockH);
        drawShadowedText(painter, timeRect, textFlags, now.toString(QStringLiteral("HH:mm")), ink);
        y += clockH + 4;

        painter.setFont(dateFont);
        const QString dateText = QStringLiteral("%1  %2")
                                     .arg(now.toString(QStringLiteral("M 月 d 日")))
                                     .arg(QString::fromUtf8(weekdayName(now.date().dayOfWeek())));
        const QRect dateRect(left, y, right - left, dateH);
        QColor dateInk = ink;
        dateInk.setAlpha(alpha * 75 / 100);
        drawShadowedText(painter, dateRect, textFlags, dateText, dateInk);
        y += dateH + 20;
    }

    if (m_settings.showCalendar) {
        painter.setFont(calHeaderFont);
        QFontMetrics hfm(calHeaderFont);
        const QRect headerRect(left, y, right - left, hfm.height());
        drawShadowedText(painter, headerRect, textFlags, calHeaderLines.at(0), ink);
        y += hfm.height() + 6;

        painter.setFont(calHeaderFont);
        const QRect weekHeaderRect(left, y, right - left, hfm.height());
        QColor mutedInk = ink;
        mutedInk.setAlpha(alpha * 60 / 100);
        drawShadowedText(painter, weekHeaderRect, textFlags, calHeaderLines.at(1), mutedInk);
        y += hfm.height() + 6;

        painter.setFont(calDayFont);
        QFontMetrics dfm(calDayFont);
        const int lineH = dfm.height() + 3;
        const int today = now.date().day();
        for (const QString& line : calWeekLines) {
            const QRect dayRect(left, y, right - left, lineH);
            // 高亮今天：在日期文字下画一个柔和圆角背景
            if (line.contains(QString::number(today))) {
                QColor accent = m_settings.spectrumColor;
                accent.setAlpha(alpha / 3);
                painter.setPen(Qt::NoPen);
                painter.setBrush(accent);
                const int barW = qMin(dayRect.width(), dfm.horizontalAdvance(QString::number(today)) + 12);
                const int barH = lineH - 2;
                int barX = dayRect.left();
                if (alignCenter) {
                    barX = dayRect.center().x() - barW / 2;
                } else if (alignRight) {
                    barX = dayRect.right() - barW;
                }
                painter.drawRoundedRect(
                    QRect(barX, dayRect.center().y() - barH / 2, barW, barH), 6, 6);
            }
            drawShadowedText(painter, dayRect, textFlags, line, ink);
            y += lineH;
        }
        y += 16;
    }

    if (!textLines.isEmpty()) {
        painter.setFont(textFont);
        QFontMetrics fm(textFont);
        const int lineH = fm.height() + 4;
        for (const QString& line : textLines) {
            const QRect textRect(left, y, right - left, lineH);
            drawShadowedText(painter, textRect, textFlags, line, ink);
            y += lineH;
        }
    }

    // ---- 频谱：贴屏幕底边，宽度铺满 ----
    if (m_settings.showSpectrum && !m_spectrum.isEmpty()) {
        const int count = m_spectrum.size();
        const int areaH = qMax(50, height() / 5);
        const QRect spectrumArea(0, height() - areaH, width(), areaH);
        const int gap = 3;
        const int bw = qMax(3, (spectrumArea.width() - gap * (count - 1)) / count);

        QLinearGradient grad(0, spectrumArea.bottom(), 0, spectrumArea.top());
        QColor c1 = m_settings.spectrumColor;
        c1.setAlpha(alpha);
        QColor c2 = m_settings.color;
        c2.setAlpha(alpha / 2);
        grad.setColorAt(0.0, c1);
        grad.setColorAt(1.0, c2);

        painter.setPen(Qt::NoPen);
        for (int i = 0; i < count; ++i) {
            const float v = qBound(0.0f, m_spectrum.at(i), 1.0f);
            if (v <= 0.02f) {
                continue;
            }
            const int h = static_cast<int>(v * spectrumArea.height() * 0.85f);
            const int x = spectrumArea.left() + i * (bw + gap);
            const int top = spectrumArea.bottom() - h;

            // 倒影式双层柱：主体 + 淡倒影
            QRect barRect(x, top, bw, h);
            painter.setBrush(grad);
            painter.drawRoundedRect(barRect, bw / 2, bw / 2);

            QColor reflection = c1;
            reflection.setAlpha(alpha / 5);
            painter.setBrush(reflection);
            painter.drawRoundedRect(QRect(x, spectrumArea.bottom(), bw, h / 3), bw / 2, bw / 2);
        }
    }
}
