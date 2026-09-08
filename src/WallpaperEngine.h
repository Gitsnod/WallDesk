#pragma once

#include <QList>
#include <QObject>
#include <QRect>
#include <QString>

#include <atomic>
#include <functional>

#include "VlcPlayer.h"

class QTimer;

/** 图片填充方式，对应注册表 WallpaperStyle / TileWallpaper。 */
enum class ImageFit {
    Fill = 0,    // 填充（可能裁切）
    Fit = 1,     // 适应（留黑/白边）
    Stretch = 2, // 拉伸（变形铺满）
    Tile = 3,    // 平铺
    Center = 4,  // 居中
    Span = 5     // 跨区（多屏拼接）
};

/** 视频壁纸覆盖范围。 */
enum class ScreenTarget {
    Primary = 0,   // 仅主显示器
    Virtual = 1,   // 全部显示器组成的虚拟桌面
    Monitor = 2    // 指定某一台显示器
};

/** 显示器信息。坐标使用虚拟桌面坐标系，可为负值。 */
struct MonitorInfo {
    QString name;      // 设备名，形如 \\.\DISPLAY1
    QRect rect;        // 在虚拟桌面中的位置与尺寸
    bool primary = false;

    QString label() const
    {
        return QStringLiteral("%1  %2x%3%4")
            .arg(name)
            .arg(rect.width())
            .arg(rect.height())
            .arg(primary ? QStringLiteral("  (主)") : QString());
    }
};

/**
 * 壁纸引擎：负责图片壁纸与视频壁纸的实际生效。
 *
 * 图片：写注册表 + SystemParametersInfoW(SPI_SETDESKWALLPAPER)。
 * 视频：找到桌面 WorkerW 壁纸层（Win7~Win10 为顶层窗口，Win11 为 Progman 子窗口），
 *       把宿主窗口挂到它下面，因此视频位于桌面图标之下，再用 libVLC 渲染画面。
 *
 * V2：降级挂载、看护自动重挂、续播、显示器枚举、状态查询。
 * V3 增强：
 *   1. 修掉 V2 的两处编译缺陷（resolveHostParent 缺声明、applyImage 签名不一致）；
 *   2. 图片参数未变化时跳过注册表与系统接口调用，重复应用几乎零开销；
 *   3. 看护定时器自适应间隔：异常时 2 秒、正常时 5 秒、暂停时 8 秒；
 *   4. 支持运行时切换解码档位（重建 libvlc 实例并续播）。
 */
class WallpaperEngine : public QObject {
    Q_OBJECT
public:
    explicit WallpaperEngine(QObject* parent = nullptr);
    ~WallpaperEngine() override;

    /** 枚举当前所有显示器。 */
    static QList<MonitorInfo> listMonitors();

    bool ensureVideoBackend(QString* err = nullptr);
    bool loadBackendFrom(const QString& directory, QString* err = nullptr);
    bool videoBackendReady() const { return m_vlc.isLoaded(); }
    QString backendPath() const { return m_vlc.libraryPath(); }

    /** 设置视频覆盖范围；修改后若正在播放会自动重新铺满。 */
    void setTarget(ScreenTarget target, int monitorIndex);

    bool applyImage(const QString& path, ImageFit fit, QString* err = nullptr);
    bool applyVideo(const QString& path, int volume, QString* err = nullptr);

    /** 切换解码档位。已加载时重建 libvlc 实例并从原位置续播。 */
    void setPerformanceProfile(VlcProfile profile);
    VlcProfile performanceProfile() const { return m_vlc.profile(); }

    /** 离线抓帧（生成缩略图用），不干扰正在播放的画面。 */
    bool snapshotVideo(const QString& file, const QString& outPng, int width, int height);

    /** 重新挂载宿主窗口并从上次位置继续播放。返回是否成功。 */
    bool reattach();
    /** 仅重算几何（显示器变化时），不重建播放器。 */
    void refreshGeometry();

    void stopVideo();
    void setVideoPaused(bool paused);
    void setVideoVolume(int volume);
    bool isVideoActive() const { return m_hostWnd != nullptr && m_vlc.isPlaying(); }
    bool isFallbackMode() const { return m_usingFallback; }
    PlaybackState videoState() const { return m_vlc.state(); }
    QString currentVideo() const { return m_currentFile; }

signals:
    /** 视频壁纸无法维持（重试次数耗尽）时发出，参数为可执行的修复建议。 */
    void videoFailed(const QString& reason);
    /** 自动重挂成功后发出。 */
    void videoRecovered();

private slots:
    void onWatchdog();
    /** 销毁宿主窗口。声明为槽：后台线程只能通过队列调用它。 */
    void destroyHostWindow();

private:
    HWND resolveWorkerW();
    HWND resolveHostParent();
    bool prepareHostWindow();
    QRect targetRect() const;
    void startWatchdog();
    void stopWatchdog();
    /** 按当前状态调整看护周期：异常加快、暂停放缓。 */
    void updateWatchdogInterval();

    /**
     * 把一个可能阻塞的 libvlc 操作放到后台线程执行。
     *
     * 动机：解码链路卡死时 libvlc_media_player_stop() 会一直等不到 input 线程
     * 退出，若在主线程调用就会把界面拖成「无响应」（实测复现）。放到后台后，
     * 最坏情况是这一个后台线程卡住，界面照常可用。
     * 同一时刻只允许一个后台任务（共用同一个播放器对象），已有任务时直接跳过。
     * @return 是否真正启动了后台任务。
     */
    bool runAsync(std::function<void()> op);
    /** 等待正在执行的后台任务收尾，最多 maxMs 毫秒。 */
    void waitAsync(int maxMs);
    /** 卡死 / 重播无效时的后台自救：先轻量重播，再按需完整重建。 */
    void recoverPlayback(bool full);
    /**
     * 后台任务的统一收尾：判断是否已被更新的操作取代（epoch 变化），
     * 若是则补一次停止，并把窗口销毁排回界面线程。
     */
    void finishAsyncOp(int epoch);
    /** epoch：每发起一次新的播放/停止都会自增，用于让在途后台任务自动失效。 */
    std::atomic<int> m_epoch{0};

    VlcPlayer m_vlc;
    HWND m_workerW = nullptr;      // 桌面 WorkerW 层（视频壁纸张贴位置）
    HWND m_hostParent = nullptr;   // 宿主当前挂载到的父窗口（降级模式为 nullptr）
    HWND m_hostWnd = nullptr;      // 承载 VLC 画面的纯 Win32 宿主窗口
    bool m_hostShown = false;      // 首帧渲染前保持隐藏，避免切换闪空白底
    QTimer* m_watchdog = nullptr;

    ScreenTarget m_target = ScreenTarget::Primary;
    int m_monitorIndex = 0;
    bool m_usingFallback = false;  // WorkerW 不可用，已降级为置底顶层窗口
    bool m_hostOnDefView = false;  // Win11 模式：宿主挂在 SHELLDLL_DefView 下
    bool m_pausedByUser = false;
    bool m_stopRequested = false;  // 已请求停止，宿主窗口允许销毁

    QString m_currentFile;         // 当前视频，供重挂续播使用
    int m_volume = 0;
    qint64 m_lastTime = 0;         // 最近一次正常播放的位置（毫秒）
    int m_recoverFails = 0;        // 连续重挂失败计数，超过阈值即放弃并上报
    int m_stallCount = 0;          // 播放位置连续未前进的看护周期数，用于卡死检测
    int m_endTicks = 0;            // 连续判定为「已播完」的看护周期数，用于升级自救手段
    std::atomic<bool> m_asyncBusy{false}; // 有后台 libvlc 任务在跑

    // 图片壁纸去重：三元组与上次完全一致时跳过系统调用
    QString m_lastImagePath;
    QString m_lastStyle;
    QString m_lastTile;
    bool m_lastImageOk = false;
};
