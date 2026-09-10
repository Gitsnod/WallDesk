#pragma once

#include <QObject>
#include <QVector>

#include <atomic>
#include <mutex>
#include <thread>

/**
 * 系统声音采集（WASAPI 回环）+ FFT。
 *
 * 回环采集拿的是「输出到扬声器」的混音，因此不需要声卡支持立体声混音，
 * 放歌、看视频、打游戏都能出频谱。整个采集与变换都在独立线程里跑，
 * 主线程只通过 bands() 取结果，避免解码线程被界面拖慢。
 */
class AudioSpectrum : public QObject {
    Q_OBJECT
public:
    explicit AudioSpectrum(QObject* parent = nullptr);
    ~AudioSpectrum() override;

    /** 开始采集。已在采集时直接返回 true。device 不可用时返回 false。 */
    bool start(int bandCount = 48);
    void stop();
    bool isRunning() const { return m_running.load(); }

    /** 最近一帧频谱，长度等于启动时的 bandCount，每项 0..1。 */
    QVector<float> bands() const;

signals:
    /** 每次算出新的一帧后发出（采集线程 → 界面线程，队列连接）。 */
    void frameReady(const QVector<float>& bands);

private:
    /** 采集线程主循环：初始化 COM 与 WASAPI，然后持续取缓冲。 */
    void runLoop(int bandCount);
    void publish(const QVector<float>& values);

    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stop{false};
    mutable std::mutex m_mutex;
    QVector<float> m_bands;
    int m_bandCount = 48;
};
