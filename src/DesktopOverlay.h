#pragma once

#include <QColor>
#include <QString>
#include <QVector>
#include <QWidget>

class QPaintEvent;
class QPainter;
class QTimer;

/**
 * 桌面叠加层：一块铺满屏幕的透明窗口，用来画时钟、日历、自定义文字与音乐频谱。
 *
 * 设计取舍：
 *   1. 窗口本身透明且不吃鼠标（WindowTransparentForInput + WS_EX_TRANSPARENT），
 *      只在真正有内容的地方落像素，不会挡住桌面图标与操作；
 *   2. 置底策略与视频宿主不同——挂件需要被看见，因此不挂 WorkerW，
 *      而是作为最底层顶层窗口（HWND_BOTTOM）常驻，并每 2 秒复打一次，
 *      防止被新开的窗口顶到上面；
 *   3. 重绘频率 1 秒（时钟）/ 采集驱动（频谱），空闲时几乎不占 CPU。
 */
class DesktopOverlay : public QWidget {
    Q_OBJECT
public:
    /** 挂件配置，全部可序列化到 QSettings。 */
    struct Settings {
        bool enabled = false;
        bool showClock = false;
        bool showCalendar = false;
        bool showText = false;
        bool showSpectrum = false;
        /** 0 左上 / 1 右上 / 2 左下 / 3 右下 / 4 居中。 */
        int position = 1;
        int margin = 48;
        int fontSize = 30;
        /** 整体不透明度 0..100。 */
        int opacity = 80;
        int bands = 48;
        QString text;
        QColor color = QColor(255, 255, 255);
        QColor spectrumColor = QColor(0x60, 0xa5, 0xfa);
        /** -1 主屏，-2 虚拟桌面（跨屏），>=0 指定显示器序号。 */
        int screenIndex = -1;
    };

    DesktopOverlay();
    ~DesktopOverlay() override;

    void applySettings(const Settings& settings);
    const Settings& settings() const { return m_settings; }

public slots:
    /** 新的一帧频谱数据（每项 0..1），由 AudioSpectrum 驱动。 */
    void setSpectrum(const QVector<float>& bands);

protected:
    void paintEvent(QPaintEvent* event) override;

private slots:
    void onTick();
    /** 复打 HWND_BOTTOM：桌面层重建或窗口被顶起后自动归位。 */
    void onKeepBottom();

private:
    void applyWindowStyle();
    void updateScreenGeometry();
    QRect contentRect() const;

    Settings m_settings;
    QVector<float> m_spectrum;
    QTimer* m_tick = nullptr;
    QTimer* m_keeper = nullptr;
};
