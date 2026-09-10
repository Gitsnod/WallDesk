#pragma once

#include <QImage>
#include <QList>
#include <QMainWindow>
#include <QSystemTrayIcon>

#include "AppTheme.h"
#include "AudioSpectrum.h"
#include "DesktopOverlay.h"
#include "ImageEffects.h"
#include "WallpaperEngine.h"

class DesktopWatcher;
class FullscreenGuard;
class ThumbnailLoader;
class QAction;
class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QMenu;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QSlider;
class QSpinBox;
class QStackedWidget;
class QTableWidget;
class QTimer;

struct MediaItem {
    enum class Type { Image, Video };
    Type type = Type::Image;
    QString path;
    bool favorite = false; // 收藏（V4.6）
    qint64 added = 0;      // 加入库的时间戳（毫秒），用于排序
};

/**
 * 主窗口：媒体画廊 + 参数卡片 + 托盘常驻。
 *
 * V3 变更：
 *   1. 清单从纯文本列表改为缩略图画廊（异步生成 + 磁盘缓存）；
 *   2. 支持浅色 / 深色 / 跟随系统三套主题；
 *   3. 内置 VLC 运行时在后台解包，首次启动不再假死；
 *   4. 新增全屏检测暂停、解码档位、缩略图缓存清理等性能相关项。
 *
 * 所有配置通过 QSettings 持久化（注册表 HKCU\Software\WallDesk）。
 */
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    /** 其它实例请求显示主界面（单实例协作入口，V4）。 */
    void requestShow();
    /** 处理其它实例转交的指令：next / quit，其它一律视为显示主界面。 */
    void handleCommand(const QString& command);

protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onAddImages();
    void onAddVideos();
    void onRemoveSelected();
    void onClearAll();
    void onApplySelected();
    void onNext();
    void onStop();
    void onTogglePause();
    void onAutoSwitchToggled(bool enabled);
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
    void onQuit();

    // 系统事件：锁屏、休眠唤醒、显示器变化、电源切换、全屏应用
    void onSystemResume();
    void onSessionLock(bool locked);
    void onDisplayChanged();
    void onPowerStatusChanged();
    void onFullscreenChanged(bool active);

    // 维护与外观
    void onReattach();
    void onLocateBackend();
    void onThemeChanged(int index);
    void onProfileChanged(int index);
    void onClearThumbCache();
    void onThumbSizeChanged(int index);
    void onAbout();

    // V4.6：检索 / 收藏 / 策略 / 场景 / 效果 / 挂件 / 频谱
    void onSearchChanged();
    void onFilterChanged();
    void onSortChanged();
    void onToggleFavorite();
    void onGalleryContextMenu(const QPoint& pos);
    void onSwitchModeChanged();
    void onSceneToggled();
    void onAddScene();
    void onRemoveScene();
    void onSceneTick();
    void onEffectChanged();
    void onOverlayChanged();
    void onSpectrumFrame(const QVector<float>& bands);
    void onMonitorsApply();
    void onMonitorsReset();
    void onPortableToggled(bool enabled);
    void onVideoFailed(const QString& reason);
    void onVideoRecovered();
    void onThumbnailReady(const QString& path, const QImage& image);

private:
    void buildUi();
    /** 创建全局 QAction（快捷键）但不显示菜单栏；菜单功能已统一到左侧工具页。 */
    void buildActions();
    QWidget* buildLibraryPage();
    QWidget* buildSettingsPage();
    /** 多屏：为每台显示器单独指定壁纸（V4.6）。 */
    QWidget* buildMonitorsPage();
    /** 按当前显示器与库内容重建多屏页的每一行。 */
    void refreshMonitorRows();
    /** 从界面控件收集画面效果参数。 */
    ImageEffect collectEffect() const;
    /** 从界面控件收集挂件参数。 */
    DesktopOverlay::Settings collectOverlay() const;
    /** 按当前过滤 / 排序条件重建可见项下标。 */
    void rebuildVisible();
    /** 自动切换时按策略挑下一项；返回 -1 表示没有候选。 */
    int pickNextIndex() const;
    /** 定时场景：返回当前时间命中的行号，无命中返回 -1。 */
    int currentSceneRow() const;
    /** 壁纸库为空时的引导提示页。 */
    QWidget* buildEmptyHint();
    /** 按「缩略图尺寸」设置刷新单元格尺寸；窗口缩放不再改变它（V4.5）。 */
    void updateGalleryMetrics();
    /** 只为可见区域发起缩略图请求，滚动/缩放后延迟触发（V4.5 内存优化）。 */
    void requestVisibleThumbnails();
    void setupWatcher();
    void setupTray();
    void loadSettings();
    void saveSettings();
    void refreshList();
    void refreshMonitors();
    void applyTarget();
    void applyTheme();
    /** byPolicy 表示由省电/锁屏/全屏策略触发，用于区分用户的手动暂停。 */
    void applyVideoPaused(bool paused, bool byPolicy = false);
    void refreshBackendInfo();
    void maybeRestoreLast();
    void applyItem(const MediaItem& item);
    void updateStatus(const QString& text, bool warn = false);
    /** 准备视频后端：内置运行时优先，异步解包避免界面假死。 */
    void prepareBackend();
    void finishBackendLoad();
    void updateFullscreenGuard();
    void setAutoStart(bool enabled);
    bool isAutoStartEnabled() const;
    void openFolder(const QString& path);
    static QIcon appIcon();

    QList<MediaItem> m_items;
    int m_currentIndex = -1;

    WallpaperEngine m_engine;
    ThumbnailLoader* m_thumbs = nullptr;
    FullscreenGuard* m_fullscreen = nullptr;

    QListWidget* m_list = nullptr;
    QListWidget* m_nav = nullptr;
    QStackedWidget* m_pages = nullptr;
    QComboBox* m_fitCombo = nullptr;
    QComboBox* m_screenCombo = nullptr;
    QComboBox* m_monitorCombo = nullptr;
    QComboBox* m_profileCombo = nullptr;
    QComboBox* m_thumbSizeCombo = nullptr;
    QList<QAction*> m_themeActions;
    QList<QRadioButton*> m_themeRadios;
    QAction* m_actPause = nullptr;
    QSpinBox* m_intervalSpin = nullptr;
    QSlider* m_volumeSlider = nullptr;
    QCheckBox* m_autoSwitch = nullptr;
    QCheckBox* m_autoStart = nullptr;
    QCheckBox* m_pauseOnLock = nullptr;
    QCheckBox* m_pauseOnBattery = nullptr;
    QCheckBox* m_pauseOnFullscreen = nullptr;
    QCheckBox* m_videoThumbnails = nullptr;
    QCheckBox* m_restoreLast = nullptr;
    QCheckBox* m_portable = nullptr;

    // ---- V4.6 新增：检索 / 收藏 ----
    QLineEdit* m_searchEdit = nullptr;
    QComboBox* m_filterCombo = nullptr;
    QComboBox* m_sortCombo = nullptr;
    QList<int> m_visible;       // 行号 → m_items 下标（过滤 / 排序后的映射）
    QList<int> m_shuffleQueue;  // 不重复随机的播放队列
    int m_shufflePos = 0;

    // ---- V4.6 新增：切换策略 ----
    QComboBox* m_switchModeCombo = nullptr;
    QComboBox* m_switchScopeCombo = nullptr;

    // ---- V4.6 新增：定时场景 ----
    QCheckBox* m_sceneEnabled = nullptr;
    QTableWidget* m_sceneTable = nullptr;
    QTimer* m_sceneTimer = nullptr;
    int m_activeSceneRow = -1;

    // ---- V4.6 新增：画面效果 ----
    QSlider* m_fxBrightness = nullptr;
    QSlider* m_fxContrast = nullptr;
    QSlider* m_fxSaturation = nullptr;
    QSlider* m_fxBlur = nullptr;
    QSlider* m_fxVignette = nullptr;
    QCheckBox* m_fxGray = nullptr;
    QTimer* m_fxTimer = nullptr; // 滑块防抖：拖动过程中不重复重算大图

    // ---- V4.6 新增：挂件与频谱 ----
    DesktopOverlay* m_overlay = nullptr;
    QCheckBox* m_overlayEnabled = nullptr;
    QCheckBox* m_overlayClock = nullptr;
    QCheckBox* m_overlayCalendar = nullptr;
    QCheckBox* m_overlayText = nullptr;
    QLineEdit* m_overlayTextEdit = nullptr;
    QComboBox* m_overlayPosCombo = nullptr;
    QComboBox* m_overlayScreenCombo = nullptr;
    QSpinBox* m_overlaySize = nullptr;
    AudioSpectrum* m_spectrum = nullptr;
    QCheckBox* m_spectrumEnabled = nullptr;
    QSpinBox* m_spectrumBands = nullptr;

    // ---- V4.6 新增：多屏 ----
    QWidget* m_monitorRows = nullptr;
    QFormLayout* m_monitorForm = nullptr;
    QList<QComboBox*> m_monitorCombos;

    QLabel* m_status = nullptr;
    QLabel* m_libraryTitle = nullptr;
    QStackedWidget* m_galleryStack = nullptr;
    QProgressBar* m_busy = nullptr;
    QPushButton* m_pauseButton = nullptr;
    QPushButton* m_applyButton = nullptr; // 顶部贯穿工具条
    QPushButton* m_nextButton = nullptr;  // 顶部贯穿工具条

    QTimer* m_timer = nullptr;        // 自动切换
    QTimer* m_thumbTimer = nullptr;   // 可见区域缩略图补加载（防抖）
    QSystemTrayIcon* m_tray = nullptr;
    QMenu* m_trayMenu = nullptr;
    DesktopWatcher* m_watcher = nullptr;
    bool m_forceQuit = false;
    bool m_videoPaused = false;
    bool m_pausedByPolicy = false;
    bool m_backendReady = false;
    ThemeKind m_theme = ThemeKind::System;
};
