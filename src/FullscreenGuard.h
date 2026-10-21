#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class QTimer;

/**
 * 全屏应用检测器：前台窗口占满整个显示器时发出通知。
 *
 * 用途：用户在打游戏 / 看全屏视频时，桌面壁纸根本看不见，此时继续解码纯属浪费
 * 电量和 CPU。检测到全屏后暂停视频，退出全屏再恢复——这是壁纸类软件最有效的一项
 * 省电优化（VLC 常驻解码通常占用 5%-15% CPU）。
 *
 * 判定方式：取前台窗口矩形，与它所在显示器的 rcMonitor 做比较（容忍 4 像素误差），
 * 并排除桌面自身（Progman / WorkerW）与任务栏。结果需连续两次一致才切换状态，
 * 避免窗口切换过程中的抖动。
 */
class FullscreenGuard : public QObject {
    Q_OBJECT
public:
    explicit FullscreenGuard(QObject* parent = nullptr);

    void setEnabled(bool enabled);
    bool isFullscreen() const { return m_active; }

    /**
     * 前台窗口所属进程的可执行文件名（小写、不含路径）。V4.9。
     * 取不到（无前台窗口 / 进程已退出 / 拒绝访问）时返回空字符串。
     */
    static QString foregroundExeName();

    /**
     * 当前会话内拥有可见顶层窗口的进程名（小写、去重、升序）。V4.9。
     * 用于「逐应用规则」的候选列表。
     */
    static QStringList windowProcessNames();

signals:
    void fullscreenChanged(bool active);

private slots:
    void onTick();

private:
    static bool foregroundIsFullscreen();
    void setActive(bool active);

    QTimer* m_timer = nullptr;
    bool m_active = false;
    int m_stableCount = 0; // 连续同值采样次数
    bool m_lastSample = false;
};
