#pragma once

#include <QImage>
#include <QList>
#include <QMainWindow>
#include <QSystemTrayIcon>

#include "AppTheme.h"
#include "WallpaperEngine.h"

class DesktopWatcher;
class FullscreenGuard;
class ThumbnailLoader;
class QAction;
class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QMenu;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QSlider;
class QSpinBox;
class QStackedWidget;
class QTimer;

struct MediaItem {
    enum class Type { Image, Video };
    Type type = Type::Image;
    QString path;
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
    void onAbout();
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
    QWidget* buildToolsPage();
    /** 依据窗口宽度自适应画廊缩略图密度（V4.1 缩放自适应）。 */
    void adaptGalleryDensity();
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
    void requestThumbnails();
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
    QLabel* m_status = nullptr;
    QProgressBar* m_busy = nullptr;
    QPushButton* m_pauseButton = nullptr;
    QPushButton* m_applyButton = nullptr; // 壁纸库页
    QPushButton* m_nextButton = nullptr;  // 壁纸库页

    QTimer* m_timer = nullptr;
    QSystemTrayIcon* m_tray = nullptr;
    QMenu* m_trayMenu = nullptr;
    DesktopWatcher* m_watcher = nullptr;
    bool m_forceQuit = false;
    bool m_videoPaused = false;
    bool m_pausedByPolicy = false;
    bool m_backendReady = false;
    ThemeKind m_theme = ThemeKind::System;
};
