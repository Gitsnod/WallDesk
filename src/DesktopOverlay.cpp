#include "DesktopOverlay.h"

#include <windows.h>

#include <QApplication>
#include <QDateTime>
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

/** 描边文字：先画一圈半透明黑，再画正文，保证在浅色壁纸上也看得清。 */
void drawShadowedText(QPainter& painter, const QRect& rect, int flags, const QString& text)
{
    painter.save();
    painter.setPen(QColor(0, 0, 0, 120));
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            if (dx == 0 && dy == 0) {
                continue;
            }
            painter.drawText(rect.translated(dx, dy), flags, text);
        }
    }
    painter.setPen(painter.brush().color());
    painter.drawText(rect, flags, text);
    painter.restore();
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

    const int alpha = qBound(0, m_settings.opacity, 100) * 255 / 100;
    QColor ink = m_settings.color;
    ink.setAlpha(alpha);

    // ---- 频谱：贴屏幕底边，宽度铺满 ----
    if (m_settings.showSpectrum && !m_spectrum.isEmpty()) {
        const int count = m_spectrum.size();
        const int areaH = qMax(40, height() / 6);
        const QRect area(0, height() - areaH, width(), areaH);
        const int gap = 2;
        const int bw = qMax(2, (area.width() - gap * (count - 1)) / count);
        QColor bar = m_settings.spectrumColor;
        bar.setAlpha(alpha);
        painter.setPen(Qt::NoPen);
        painter.setBrush(bar);
        for (int i = 0; i < count; ++i) {
            const int h = static_cast<int>(qBound(0.0f, m_spectrum.at(i), 1.0f) * area.height());
            if (h <= 1) {
                continue;
            }
            const int x = area.left() + i * (bw + gap);
            painter.drawRoundedRect(QRect(x, area.bottom() - h, bw, h), 2, 2);
        }
    }

    // ---- 时钟 / 日历 / 文字：按角落堆叠 ----
    const QRect area = contentRect();
    const QDateTime now = QDateTime::currentDateTime();

    struct Block {
        QStringList lines;
        int font;
    };
    QList<Block> blocks;
    if (m_settings.showClock) {
        blocks.append(
            {QStringList{now.toString(QStringLiteral("HH:mm")),
                         now.toString(QStringLiteral("M 月 d 日")) + QStringLiteral("  ")
                             + QString::fromUtf8(weekdayName(now.date().dayOfWeek()))},
             m_settings.fontSize + 16});
    }
    if (m_settings.showCalendar) {
        const QDate date = now.date();
        const QDate first(date.year(), date.month(), 1);
        QStringList lines;
        lines << QStringLiteral("%1 年 %2 月").arg(date.year()).arg(date.month());
        lines << QStringLiteral("一 二 三 四 五 六 日");
        QString week;
        // 月首前补空格（Qt 的 dayOfWeek: 1=周一）
        for (int i = 1; i < first.dayOfWeek(); ++i) {
            week += QStringLiteral("   ");
        }
        for (int day = 1; day <= date.daysInMonth(); ++day) {
            week += QString::asprintf("%2d ", day);
            if (QDate(date.year(), date.month(), day).dayOfWeek() == 7 || day == date.daysInMonth()) {
                lines << week;
                week.clear();
            }
        }
        blocks.append({lines, m_settings.fontSize - 8});
    }
    if (m_settings.showText && !m_settings.text.trimmed().isEmpty()) {
        blocks.append({m_settings.text.split(QLatin1Char('\n')), m_settings.fontSize});
    }

    if (blocks.isEmpty()) {
        return;
    }

    int totalH = 0;
    int maxW = 0;
    for (const Block& block : blocks) {
        for (const QString& line : block.lines) {
            QFont font = painter.font();
            font.setPointSize(block.font);
            const QFontMetrics fm(font);
            totalH += fm.height() + 2;
            maxW = qMax(maxW, fm.horizontalAdvance(line));
        }
        totalH += 14; // 块间距
    }

    int top = area.top();
    int left = area.left();
    switch (m_settings.position) {
    case 0:
        break; // 左上
    case 1:
        left = area.right() - maxW;
        break;
    case 2:
        top = area.bottom() - totalH;
        break;
    case 3:
        left = area.right() - maxW;
        top = area.bottom() - totalH;
        break;
    default: // 居中
        left = area.left() + (area.width() - maxW) / 2;
        top = area.top() + (area.height() - totalH) / 2;
        break;
    }

    painter.setBrush(ink);
    for (const Block& block : blocks) {
        QFont font = painter.font();
        font.setPointSize(block.font);
        font.setBold(true);
        painter.setFont(font);
        const QFontMetrics fm(font);
        for (const QString& line : block.lines) {
            const QRect lineRect(left, top, maxW, fm.height());
            const int align = (m_settings.position == 1 || m_settings.position == 3)
                                  ? Qt::AlignRight | Qt::AlignVCenter
                                  : (m_settings.position == 4 ? Qt::AlignHCenter | Qt::AlignVCenter
                                                              : Qt::AlignLeft | Qt::AlignVCenter);
            drawShadowedText(painter, lineRect, align, line);
            top += fm.height() + 2;
        }
        top += 14;
    }
}
