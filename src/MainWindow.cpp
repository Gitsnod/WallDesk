#include "MainWindow.h"

#include <windows.h>

#include "AppTheme.h"
#include "AppPaths.h"
#include "DesktopWatcher.h"
#include "FullscreenGuard.h"
#include "Logger.h"
#include "ThumbnailLoader.h"
#include "VlcBundle.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHash>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QListView>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygonF>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSize>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QtGlobal>

#include <thread>

namespace {
constexpr int kRoleType = Qt::UserRole + 1;
constexpr int kRolePath = Qt::UserRole + 2;
/** 电源状态：0=电池，1=交流，255=未知。 */
constexpr BYTE kAcLineOnAc = 1;

/**
 * 画廊缩略图基准宽度：小 / 中 / 大。
 * 关键点：窗口缩放只改变列数，不再改变单元格尺寸——
 * 之前每次 resize 都重算 iconSize，拖窗口时缩略图会跟着忽大忽小（V4.5 修复）。
 */
constexpr int kThumbWidths[] = {128, 160, 208};
constexpr int kThumbDefaultIndex = 1;

QSize thumbSizeForIndex(int index)
{
    const int i = qBound(0, index, 2);
    const int w = kThumbWidths[i];
    return QSize(w, w * 5 / 8);
}

/** 网格单元 = 缩略图 + 上下留白 + 一行文件名。 */
QSize gridSizeForThumb(const QSize& thumb)
{
    return QSize(thumb.width() + 16, thumb.height() + 42);
}

/** 两色混合，用于卡片底色（选中 / 悬停）与细描边。 */
QColor blend(const QColor& a, const QColor& b, qreal t)
{
    return QColor(static_cast<int>(a.red() + (b.red() - a.red()) * t),
                  static_cast<int>(a.green() + (b.green() - a.green()) * t),
                  static_cast<int>(a.blue() + (b.blue() - a.blue()) * t),
                  static_cast<int>(a.alpha() + (b.alpha() - a.alpha()) * t));
}

/** 占位图缓存：按尺寸 + 类型缓存，避免每次重绘都重画一遍渐变。 */
QPixmap placeholderPixmap(const QSize& size, bool video)
{
    static QHash<QString, QPixmap> cache;
    const QString key = QStringLiteral("%1x%2-%3")
                            .arg(size.width()).arg(size.height()).arg(video ? 1 : 0);
    const auto it = cache.constFind(key);
    if (it != cache.constEnd()) {
        return it.value();
    }
    const QPixmap pm = QPixmap::fromImage(ThumbnailLoader::placeholder(size, video));
    cache.insert(key, pm);
    return pm;
}

/**
 * 画廊项自绘：圆角缩略图 + 文件名 + 视频播放角标。
 *
 * 内存要点：缩略图只从内存 LRU 取，取不到就画占位图并发起异步请求，
 * QListWidgetItem 本身不再持有 QPixmap，几百项的库也不会把内存吃满（V4.5）。
 */
class GalleryDelegate : public QStyledItemDelegate {
public:
    GalleryDelegate(QListWidget* view, ThumbnailLoader* thumbs, QObject* parent = nullptr)
        : QStyledItemDelegate(parent)
        , m_view(view)
        , m_thumbs(thumbs)
    {
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        if (!index.isValid()) {
            return;
        }
        const bool video = index.data(kRoleType).toInt() == 1;
        const QString path = index.data(kRolePath).toString();
        const QString name = index.data(Qt::DisplayRole).toString();
        const bool selected = option.state & QStyle::State_Selected;
        const bool hovered = option.state & QStyle::State_MouseOver;

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter->setClipRect(option.rect);

        const QRect card = option.rect.adjusted(3, 3, -3, -3);
        const QColor base = option.palette.color(QPalette::Base);
        const QColor accent = option.palette.color(QPalette::Highlight);
        QColor bg = base;
        if (selected) {
            bg = blend(base, accent, 0.22);
        } else if (hovered) {
            bg = blend(base, accent, 0.08);
        }
        painter->setPen(Qt::NoPen);
        painter->setBrush(bg);
        painter->drawRoundedRect(card, 10, 10);

        // ---- 缩略图 ----
        const QSize icon = m_view ? m_view->iconSize() : QSize(160, 100);
        const QRect thumb(QPoint(card.center().x() - icon.width() / 2, card.top() + 6), icon);

        QPixmap pm;
        if (m_thumbs && m_thumbs->memoryPixmap(path, &pm)) {
            if (pm.size() != thumb.size()) {
                const QPixmap scaled = pm.scaled(thumb.size(), Qt::KeepAspectRatioByExpanding,
                                                 Qt::SmoothTransformation);
                pm = scaled.copy((scaled.width() - thumb.width()) / 2,
                                 (scaled.height() - thumb.height()) / 2,
                                 thumb.width(), thumb.height());
            }
        } else {
            pm = placeholderPixmap(icon, video);
            if (m_thumbs && !path.isEmpty() && m_view) {
                m_thumbs->request(path, video, icon);
            }
        }

        QPainterPath clip;
        clip.addRoundedRect(thumb, 8, 8);
        painter->save();
        painter->setClipPath(clip);
        painter->drawPixmap(thumb, pm);
        painter->restore();

        // 内描边：浅色主题下防止浅色图与卡片糊在一起
        painter->setPen(QPen(blend(base, option.palette.color(QPalette::Text), 0.12), 1));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(thumb.adjusted(0, 0, -1, -1), 8, 8);

        if (video) {
            drawPlayBadge(painter, thumb);
        }

        // ---- 文件名 ----
        const QRect text(card.left() + 6, thumb.bottom() + 5, card.width() - 12,
                         card.bottom() - thumb.bottom() - 5);
        painter->setPen(option.palette.color(QPalette::Text));
        painter->drawText(text, Qt::AlignHCenter | Qt::AlignVCenter,
                          painter->fontMetrics().elidedText(name, Qt::ElideMiddle, text.width()));

        if (selected) {
            painter->setPen(QPen(accent, 2));
            painter->setBrush(Qt::NoBrush);
            painter->drawRoundedRect(card.adjusted(1, 1, -1, -1), 10, 10);
        }
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override
    {
        return m_view ? m_view->gridSize() : QSize(176, 132);
    }

private:
    static void drawPlayBadge(QPainter* painter, const QRect& thumb)
    {
        const int d = qMax(20, qMin(thumb.width(), thumb.height()) * 34 / 100);
        QRectF badge(0.0, 0.0, static_cast<qreal>(d), static_cast<qreal>(d));
        badge.moveCenter(QPointF(thumb.center()));
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(0, 0, 0, 130));
        painter->drawEllipse(badge);

        QPolygonF tri;
        const qreal cx = badge.center().x() + d * 0.04;
        const qreal cy = badge.center().y();
        tri << QPointF(cx - d * 0.10, cy - d * 0.20) << QPointF(cx - d * 0.10, cy + d * 0.20)
            << QPointF(cx + d * 0.20, cy);
        painter->setBrush(QColor(255, 255, 255, 235));
        painter->drawPolygon(tri);
    }

    QListWidget* m_view = nullptr;
    ThumbnailLoader* m_thumbs = nullptr;
};

} // namespace

QIcon MainWindow::appIcon()
{
    QPixmap pm(64, 64);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x25, 0x63, 0xeb));
    p.drawRoundedRect(0, 0, 64, 64, 14, 14);
    p.setBrush(QColor(0x93, 0xc5, 0xfd));
    p.drawRoundedRect(10, 12, 44, 26, 6, 6);
    p.setBrush(QColor(0x1e, 0x3a, 0x8a));
    p.drawRoundedRect(16, 44, 32, 8, 4, 4);
    p.end();
    return QIcon(pm);
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("WallDesk — 图片 / 视频壁纸"));
    setWindowIcon(appIcon());
    // 缩放自适应：下限随系统字体走，默认尺寸在 125%/150% DPI 下也放得下
    setMinimumSize(QSize(760, 520).expandedTo(
        QSize(fontMetrics().horizontalAdvance(QStringLiteral("WallDesk — 图片 / 视频壁纸")) * 3 / 2,
              fontMetrics().height() * 30)));
    resize(980, 680);

    m_thumbs = new ThumbnailLoader(this);
    connect(m_thumbs, &ThumbnailLoader::ready, this, &MainWindow::onThumbnailReady);

    buildUi();
    refreshMonitors(); // 需在 loadSettings 之前，配置里保存的是显示器序号

    // 定时器必须先于 loadSettings 创建：加载配置时会依据开关立即启动它
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &MainWindow::onNext);

    m_fullscreen = new FullscreenGuard(this);
    connect(m_fullscreen, &FullscreenGuard::fullscreenChanged, this, &MainWindow::onFullscreenChanged);

    setupWatcher();
    loadSettings();
    applyTheme();
    setupTray();

    // 视频后端放到最后：内置运行时解包耗时较长，不能挡住界面出现
    prepareBackend();
}

MainWindow::~MainWindow()
{
    // 事件分发器不拥有过滤器，必须在析构前显式摘除
    if (m_watcher) {
        qApp->removeNativeEventFilter(m_watcher);
    }
}

// ---------------------------------------------------------------- 初始化

void MainWindow::setupWatcher()
{
    m_watcher = new DesktopWatcher(this);
    qApp->installNativeEventFilter(m_watcher);
    m_watcher->setNotificationWindow(reinterpret_cast<HWND>(winId()));

    connect(m_watcher, &DesktopWatcher::systemResumed, this, &MainWindow::onSystemResume);
    connect(m_watcher, &DesktopWatcher::sessionLocked, this, &MainWindow::onSessionLock);
    connect(m_watcher, &DesktopWatcher::displayChanged, this, &MainWindow::onDisplayChanged);
    connect(m_watcher, &DesktopWatcher::powerStatusChanged, this, &MainWindow::onPowerStatusChanged);
    connect(&m_engine, &WallpaperEngine::videoFailed, this, &MainWindow::onVideoFailed);
    connect(&m_engine, &WallpaperEngine::videoRecovered, this, &MainWindow::onVideoRecovered);
}

void MainWindow::setupTray()
{
    m_tray = new QSystemTrayIcon(appIcon(), this);
    m_tray->setToolTip(QStringLiteral("WallDesk"));
    m_trayMenu = new QMenu(this);
    m_trayMenu->addAction(QStringLiteral("显示主界面"), this, &QWidget::showNormal);
    m_trayMenu->addAction(QStringLiteral("下一张"), this, &MainWindow::onNext);
    m_trayMenu->addAction(QStringLiteral("停止壁纸"), this, &MainWindow::onStop);
    m_trayMenu->addSeparator();
    m_trayMenu->addAction(QStringLiteral("退出"), this, &MainWindow::onQuit);
    m_tray->setContextMenu(m_trayMenu);
    connect(m_tray, &QSystemTrayIcon::activated, this, &MainWindow::onTrayActivated);
    m_tray->show();
}

// ---------------------------------------------------------------- 界面构建

namespace {

/** 左侧导航图标：程序内自绘，不依赖资源文件。 */
QIcon drawNavIcon(const QColor& accent, int kind)
{
    QPixmap pm(48, 48);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(accent);
    if (kind == 0) { // 壁纸库
        p.drawRoundedRect(6, 8, 36, 32, 6, 6);
        p.setBrush(QColor(255, 255, 255, 220));
        p.drawRoundedRect(11, 13, 26, 15, 3, 3);
        p.setBrush(QColor(255, 255, 255, 180));
        p.drawRoundedRect(11, 31, 26, 4, 2, 2);
    } else if (kind == 1) { // 设置
        for (int i = 0; i < 3; ++i) {
            const int y = 14 + i * 10;
            p.setBrush(accent);
            p.drawRoundedRect(8, y - 2, 32, 4, 2, 2);
            p.setBrush(Qt::white);
            p.drawEllipse(16 + i * 8 - 4, y - 5, 9, 9);
        }
    } else { // 工具：扳手
        p.save();
        p.translate(24, 24);
        p.rotate(-45);
        p.setBrush(accent);
        p.drawRoundedRect(-4, -14, 8, 20, 3, 3);
        p.drawRoundedRect(-8, 6, 16, 8, 2, 2);
        p.setBrush(QColor(255, 255, 255, 200));
        p.drawRoundedRect(-2, 4, 4, 12, 1, 1);
        p.restore();
    }
    p.end();
    return QIcon(pm);
}

} // namespace

void MainWindow::buildUi()
{
    auto* central = new QWidget(this);
    auto* outer = new QVBoxLayout(central);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // 顶部贯穿工具条：横向铺满整个窗口，库管理与播放控制同排，
    // 所有页面共用一份，避免同类按钮在多个页面重复出现。
    outer->addWidget(buildMainToolbar());

    auto* root = new QHBoxLayout();
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ---- 左侧导航栏（分级入口：库 / 设置 / 工具）----
    m_nav = new QListWidget(central);
    m_nav->setObjectName(QStringLiteral("navRail"));
    m_nav->setViewMode(QListView::ListMode);
    m_nav->setFlow(QListView::TopToBottom);
    m_nav->setWrapping(false);
    m_nav->setSelectionMode(QAbstractItemView::SingleSelection);
    m_nav->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_nav->setIconSize(QSize(22, 22));
    const QColor accent(0x25, 0x63, 0xeb);
    new QListWidgetItem(drawNavIcon(accent, 0),
                        QStringLiteral("壁纸库"), m_nav);
    new QListWidgetItem(drawNavIcon(accent, 1),
                        QStringLiteral("设置"), m_nav);
    new QListWidgetItem(drawNavIcon(accent, 2),
                        QStringLiteral("工具"), m_nav);
    connect(m_nav, &QListWidget::currentRowChanged, this, [this](int row) {
        if (m_pages) {
            m_pages->setCurrentIndex(row);
        }
    });
    m_nav->setFixedWidth(148);
    root->addWidget(m_nav);

    // ---- 右侧页面栈 ----
    m_pages = new QStackedWidget(central);
    m_pages->addWidget(buildLibraryPage());
    m_pages->addWidget(buildSettingsPage());
    m_pages->addWidget(buildToolsPage());
    root->addWidget(m_pages, 1);
    outer->addLayout(root, 1);

    setCentralWidget(central);

    buildActions();

    // ---- 状态栏：状态文字 + 后台任务指示 ----
    m_busy = new QProgressBar(this);
    m_busy->setRange(0, 0); // 不确定进度：只在解包内置运行时出现
    m_busy->setFixedSize(120, 6);
    m_busy->setVisible(false);
    statusBar()->addWidget(m_busy);

    m_status = new QLabel(QStringLiteral("就绪"), this);
    m_status->setObjectName(QStringLiteral("statusLabel"));
    m_status->setWordWrap(true);
    m_status->setProperty("warn", false);
    statusBar()->addWidget(m_status, 1);

    statusBar()->showMessage(QString());
    m_nav->setCurrentRow(0);
}

void MainWindow::buildActions()
{
    // 顶部菜单栏已取消，所有菜单功能统一到左侧「工具」页。
    // 这里只保留全局快捷键对应的 QAction，不显示菜单。

    auto* actAddImages = new QAction(QStringLiteral("添加图片"), this);
    actAddImages->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+I")));
    connect(actAddImages, &QAction::triggered, this, &MainWindow::onAddImages);
    addAction(actAddImages);

    auto* actAddVideos = new QAction(QStringLiteral("添加视频"), this);
    actAddVideos->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+V")));
    connect(actAddVideos, &QAction::triggered, this, &MainWindow::onAddVideos);
    addAction(actAddVideos);

    auto* actApply = new QAction(QStringLiteral("应用选中"), this);
    actApply->setShortcut(QKeySequence(QStringLiteral("Return")));
    connect(actApply, &QAction::triggered, this, &MainWindow::onApplySelected);
    addAction(actApply);

    auto* actNext = new QAction(QStringLiteral("下一张"), this);
    actNext->setShortcut(QKeySequence(QStringLiteral("Ctrl+N")));
    connect(actNext, &QAction::triggered, this, &MainWindow::onNext);
    addAction(actNext);

    m_actPause = new QAction(QStringLiteral("暂停视频"), this);
    m_actPause->setShortcut(QKeySequence(QStringLiteral("Ctrl+P")));
    connect(m_actPause, &QAction::triggered, this, &MainWindow::onTogglePause);
    addAction(m_actPause);

    auto* actReattach = new QAction(QStringLiteral("重新挂载桌面层"), this);
    actReattach->setShortcut(QKeySequence(QStringLiteral("F5")));
    connect(actReattach, &QAction::triggered, this, &MainWindow::onReattach);
    addAction(actReattach);

    // 页面切换快捷键
    auto* actPage0 = new QAction(QStringLiteral("壁纸库"), this);
    actPage0->setShortcut(QKeySequence(QStringLiteral("Ctrl+1")));
    connect(actPage0, &QAction::triggered, this, [this] { m_nav->setCurrentRow(0); });
    addAction(actPage0);

    auto* actPage1 = new QAction(QStringLiteral("设置"), this);
    actPage1->setShortcut(QKeySequence(QStringLiteral("Ctrl+2")));
    connect(actPage1, &QAction::triggered, this, [this] { m_nav->setCurrentRow(1); });
    addAction(actPage1);

    auto* actPage2 = new QAction(QStringLiteral("工具"), this);
    actPage2->setShortcut(QKeySequence(QStringLiteral("Ctrl+3")));
    connect(actPage2, &QAction::triggered, this, [this] { m_nav->setCurrentRow(2); });
    addAction(actPage2);

    // 主题动作（与工具页中的按钮联动）
    const QStringList themeNames = {QStringLiteral("浅色"), QStringLiteral("深色"),
                                    QStringLiteral("跟随系统")};
    for (int i = 0; i < themeNames.size(); ++i) {
        QAction* action = new QAction(themeNames.at(i), this);
        action->setCheckable(true);
        connect(action, &QAction::triggered, this, [this, i] { onThemeChanged(i); });
        m_themeActions.append(action);
        addAction(action);
    }
}

QWidget* MainWindow::buildEmptyHint()
{
    auto* hint = new QWidget(this);
    hint->setObjectName(QStringLiteral("emptyHint"));
    auto* layout = new QVBoxLayout(hint);
    layout->setSpacing(6);
    layout->addStretch(1);

    auto* icon = new QLabel(hint);
    icon->setPixmap(appIcon().pixmap(56, 56));
    icon->setAlignment(Qt::AlignHCenter);

    auto* title = new QLabel(QStringLiteral("壁纸库还是空的"), hint);
    title->setObjectName(QStringLiteral("emptyTitle"));
    title->setAlignment(Qt::AlignHCenter);

    auto* sub = new QLabel(QStringLiteral("用上方「添加图片 / 添加视频」把文件加进来，\n"
                                          "双击任意一张即可应用到桌面。"),
                           hint);
    sub->setObjectName(QStringLiteral("emptySub"));
    sub->setAlignment(Qt::AlignHCenter);
    sub->setWordWrap(true);

    layout->addWidget(icon);
    layout->addWidget(title);
    layout->addWidget(sub);
    layout->addStretch(1);
    return hint;
}

QWidget* MainWindow::buildLibraryPage()
{
    auto* pane = new QWidget(this);
    auto* layout = new QVBoxLayout(pane);
    layout->setContentsMargins(16, 14, 16, 12);
    layout->setSpacing(10);

    // 添加 / 移除 / 清空与播放控制都在顶部贯穿工具条，本页只留画廊

    // ---- 页头 ----
    m_libraryTitle = new QLabel(QStringLiteral("壁纸库"), pane);
    m_libraryTitle->setObjectName(QStringLiteral("pageTitle"));
    layout->addWidget(m_libraryTitle);

    // ---- 画廊（与空状态提示叠放，库为空时给一句人话而不是一片空白）----
    m_galleryStack = new QStackedWidget(pane);
    m_galleryStack->setObjectName(QStringLiteral("galleryStack"));

    m_list = new QListWidget(m_galleryStack);
    m_list->setViewMode(QListWidget::IconMode);
    m_list->setMovement(QListWidget::Static);
    m_list->setResizeMode(QListWidget::Adjust);
    m_list->setWrapping(true);
    m_list->setUniformItemSizes(true);
    m_list->setSpacing(2);
    m_list->setTextElideMode(Qt::ElideMiddle);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setContextMenuPolicy(Qt::NoContextMenu);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setIconSize(thumbSizeForIndex(kThumbDefaultIndex));
    m_list->setGridSize(gridSizeForThumb(thumbSizeForIndex(kThumbDefaultIndex)));
    m_list->setItemDelegate(new GalleryDelegate(m_list, m_thumbs, m_list));
    m_list->setAttribute(Qt::WA_MacShowFocusRect, false);
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this]() { onApplySelected(); });
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        const bool hasSelection = (row >= 0 && row < m_items.size());
        if (m_applyButton) {
            m_applyButton->setEnabled(hasSelection);
        }
        if (m_nextButton) {
            m_nextButton->setEnabled(!m_items.isEmpty());
        }
    });

    m_galleryStack->addWidget(m_list);
    m_galleryStack->addWidget(buildEmptyHint());
    layout->addWidget(m_galleryStack, 1);

    // 只加载看得见的那几张：滚动 / 缩放后延迟 120 ms 补请求，
    // 一次性给整个库排队会同时唤醒一堆解码线程，内存和 CPU 都白烧。
    m_thumbTimer = new QTimer(this);
    m_thumbTimer->setSingleShot(true);
    m_thumbTimer->setInterval(120);
    connect(m_thumbTimer, &QTimer::timeout, this, &MainWindow::requestVisibleThumbnails);
    connect(m_list->verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() {
        if (m_thumbTimer) {
            m_thumbTimer->start();
        }
    });

    return pane;
}

QWidget* MainWindow::buildSettingsPage()
{
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* pane = new QWidget(scroll);
    auto* layout = new QVBoxLayout(pane);
    layout->setContentsMargins(16, 14, 16, 12);
    layout->setSpacing(10);

    auto* title = new QLabel(QStringLiteral("设置"), pane);
    title->setObjectName(QStringLiteral("pageTitle"));
    layout->addWidget(title);

    // ---- 图片 ----
    auto* imageBox = new QGroupBox(QStringLiteral("图片"), pane);
    auto* imageForm = new QFormLayout(imageBox);
    m_fitCombo = new QComboBox(imageBox);
    m_fitCombo->addItem(QStringLiteral("填充"), static_cast<int>(ImageFit::Fill));
    m_fitCombo->addItem(QStringLiteral("适应"), static_cast<int>(ImageFit::Fit));
    m_fitCombo->addItem(QStringLiteral("拉伸"), static_cast<int>(ImageFit::Stretch));
    m_fitCombo->addItem(QStringLiteral("平铺"), static_cast<int>(ImageFit::Tile));
    m_fitCombo->addItem(QStringLiteral("居中"), static_cast<int>(ImageFit::Center));
    m_fitCombo->addItem(QStringLiteral("跨区（多屏）"), static_cast<int>(ImageFit::Span));
    imageForm->addRow(QStringLiteral("填充方式"), m_fitCombo);
    layout->addWidget(imageBox);

    // ---- 视频 ----
    auto* videoBox = new QGroupBox(QStringLiteral("视频"), pane);
    auto* videoForm = new QFormLayout(videoBox);

    m_screenCombo = new QComboBox(videoBox);
    m_screenCombo->addItem(QStringLiteral("仅主显示器"), static_cast<int>(ScreenTarget::Primary));
    m_screenCombo->addItem(QStringLiteral("全部显示器"), static_cast<int>(ScreenTarget::Virtual));
    m_screenCombo->addItem(QStringLiteral("指定显示器"), static_cast<int>(ScreenTarget::Monitor));
    videoForm->addRow(QStringLiteral("覆盖范围"), m_screenCombo);

    m_monitorCombo = new QComboBox(videoBox);
    m_monitorCombo->setEnabled(false);
    videoForm->addRow(QStringLiteral("目标显示器"), m_monitorCombo);

    m_volumeSlider = new QSlider(Qt::Horizontal, videoBox);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(0);
    connect(m_volumeSlider, &QSlider::valueChanged, this,
            [this](int v) { m_engine.setVideoVolume(v); });
    videoForm->addRow(QStringLiteral("音量"), m_volumeSlider);

    m_profileCombo = new QComboBox(videoBox);
    m_profileCombo->addItem(QStringLiteral("自动（推荐）"), static_cast<int>(VlcProfile::Auto));
    m_profileCombo->addItem(QStringLiteral("硬件解码优先"), static_cast<int>(VlcProfile::Hardware));
    m_profileCombo->addItem(QStringLiteral("软件解码（兼容）"), static_cast<int>(VlcProfile::Software));
    m_profileCombo->addItem(QStringLiteral("高画质"), static_cast<int>(VlcProfile::Quality));
    m_profileCombo->setToolTip(QStringLiteral(
        "硬件解码省电但对显卡/驱动有要求；软件解码最稳但 CPU 占用高。切换会重建解码实例并续播。"));
    videoForm->addRow(QStringLiteral("解码档位"), m_profileCombo);
    connect(m_profileCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onProfileChanged);

    connect(m_screenCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this]() { applyTarget(); });
    connect(m_monitorCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this]() { applyTarget(); });
    layout->addWidget(videoBox);

    // ---- 自动化 ----
    auto* autoBox = new QGroupBox(QStringLiteral("自动化"), pane);
    auto* autoForm = new QFormLayout(autoBox);
    m_intervalSpin = new QSpinBox(autoBox);
    m_intervalSpin->setRange(1, 1440);
    m_intervalSpin->setValue(30);
    m_intervalSpin->setSuffix(QStringLiteral(" 分钟"));
    connect(m_intervalSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this]() { onAutoSwitchToggled(m_autoSwitch->isChecked()); });
    autoForm->addRow(QStringLiteral("切换间隔"), m_intervalSpin);

    m_autoSwitch = new QCheckBox(QStringLiteral("启用自动切换"), autoBox);
    m_autoStart = new QCheckBox(QStringLiteral("开机自动启动"), autoBox);
    m_restoreLast = new QCheckBox(QStringLiteral("启动时恢复上次壁纸"), autoBox);
    connect(m_autoSwitch, &QCheckBox::toggled, this, &MainWindow::onAutoSwitchToggled);
    connect(m_autoStart, &QCheckBox::toggled, this, [this](bool on) { setAutoStart(on); });
    autoForm->addRow(QString(), m_autoSwitch);
    autoForm->addRow(QString(), m_autoStart);
    autoForm->addRow(QString(), m_restoreLast);
    layout->addWidget(autoBox);

    // ---- 省电 ----
    auto* powerBox = new QGroupBox(QStringLiteral("省电"), pane);
    auto* powerForm = new QFormLayout(powerBox);
    m_pauseOnLock = new QCheckBox(QStringLiteral("锁屏时暂停"), powerBox);
    m_pauseOnBattery = new QCheckBox(QStringLiteral("使用电池时暂停"), powerBox);
    m_pauseOnFullscreen = new QCheckBox(QStringLiteral("全屏应用时暂停"), powerBox);
    m_pauseOnFullscreen->setToolTip(QStringLiteral(
        "前台窗口占满整个显示器时（游戏、全屏播放器）桌面壁纸看不见，暂停解码可省 5%-15% CPU。"));
    connect(m_pauseOnFullscreen, &QCheckBox::toggled, this,
            [this](bool) { updateFullscreenGuard(); });
    powerForm->addRow(QString(), m_pauseOnLock);
    powerForm->addRow(QString(), m_pauseOnBattery);
    powerForm->addRow(QString(), m_pauseOnFullscreen);
    layout->addWidget(powerBox);

    // ---- 其它 ----
    auto* miscBox = new QGroupBox(QStringLiteral("其它"), pane);
    auto* miscForm = new QFormLayout(miscBox);
    m_videoThumbnails = new QCheckBox(QStringLiteral("生成视频缩略图"), miscBox);
    m_videoThumbnails->setToolTip(QStringLiteral("关闭后视频项使用占位图，可避免大文件解码开销。"));
    connect(m_videoThumbnails, &QCheckBox::toggled, this, [this](bool on) {
        if (on) {
            requestVisibleThumbnails(); // 刚打开：立刻把可见的视频补抓一帧
        } else {
            m_list->viewport()->update();
        }
        saveSettings();
    });
    miscForm->addRow(QString(), m_videoThumbnails);

    m_thumbSizeCombo = new QComboBox(miscBox);
    m_thumbSizeCombo->addItem(QStringLiteral("小（128 px）"), 0);
    m_thumbSizeCombo->addItem(QStringLiteral("中（160 px）"), 1);
    m_thumbSizeCombo->addItem(QStringLiteral("大（208 px）"), 2);
    m_thumbSizeCombo->setCurrentIndex(kThumbDefaultIndex);
    m_thumbSizeCombo->setToolTip(QStringLiteral(
        "只影响列表显示尺寸。窗口缩放不再自动改变缩略图大小——之前那样拖窗口会让缩略图忽大忽小。"));
    connect(m_thumbSizeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onThumbSizeChanged);
    miscForm->addRow(QStringLiteral("缩略图尺寸"), m_thumbSizeCombo);

    m_portable = new QCheckBox(QStringLiteral("便携模式（重启后生效）"), miscBox);
    m_portable->setChecked(AppPaths::portable());
    m_portable->setToolTip(QStringLiteral(
        "启用后配置与数据全部保存在程序目录的 data 文件夹内，不写注册表，"
        "整个文件夹拷到 U 盘即可带走。本机已有配置不会自动迁移。"));
    connect(m_portable, &QCheckBox::toggled, this, &MainWindow::onPortableToggled);
    miscForm->addRow(QString(), m_portable);

    auto* toolRow = new QHBoxLayout();
    auto* locateBtn = new QPushButton(QStringLiteral("定位 libvlc"), miscBox);
    auto* clearBtn = new QPushButton(QStringLiteral("清理缓存"), miscBox);
    auto* logBtn = new QPushButton(QStringLiteral("打开日志"), miscBox);
    connect(locateBtn, &QPushButton::clicked, this, &MainWindow::onLocateBackend);
    connect(clearBtn, &QPushButton::clicked, this, &MainWindow::onClearThumbCache);
    connect(logBtn, &QPushButton::clicked, this, [this] { openFolder(Logger::logDir()); });
    toolRow->addWidget(locateBtn);
    toolRow->addWidget(clearBtn);
    toolRow->addWidget(logBtn);
    miscForm->addRow(toolRow);
    layout->addWidget(miscBox);

    layout->addStretch(1);
    scroll->setWidget(pane);
    return scroll;
}

QWidget* MainWindow::buildToolsPage()
{
    auto* pane = new QWidget(this);
    auto* layout = new QVBoxLayout(pane);
    layout->setContentsMargins(16, 14, 16, 12);
    layout->setSpacing(10);

    auto* title = new QLabel(QStringLiteral("工具"), pane);
    title->setObjectName(QStringLiteral("pageTitle"));
    layout->addWidget(title);

    // ---- 文件 ----（添加图片 / 添加视频 / 播放控制都在顶部贯穿工具条，此处不重复）
    auto* fileBox = new QGroupBox(QStringLiteral("文件"), pane);
    auto* fileLayout = new QHBoxLayout(fileBox);
    auto* dataBtn = new QPushButton(QStringLiteral("打开数据目录"), fileBox);
    auto* logBtn = new QPushButton(QStringLiteral("打开日志目录"), fileBox);
    connect(dataBtn, &QPushButton::clicked, this, [this] { openFolder(AppPaths::dataDir()); });
    connect(logBtn, &QPushButton::clicked, this, [this] { openFolder(Logger::logDir()); });
    fileLayout->addWidget(dataBtn);
    fileLayout->addWidget(logBtn);
    fileLayout->addStretch(1);
    layout->addWidget(fileBox);

    // ---- 视图 ----
    auto* viewBox = new QGroupBox(QStringLiteral("视图"), pane);
    auto* viewLayout = new QHBoxLayout(viewBox);
    const QStringList themeNames = {QStringLiteral("浅色"), QStringLiteral("深色"),
                                    QStringLiteral("跟随系统")};
    for (int i = 0; i < themeNames.size(); ++i) {
        auto* rb = new QRadioButton(themeNames.at(i), viewBox);
        connect(rb, &QRadioButton::toggled, this, [this, i](bool checked) {
            if (checked) {
                onThemeChanged(i);
            }
        });
        m_themeRadios.append(rb);
        viewLayout->addWidget(rb);
    }
    viewLayout->addStretch(1);
    layout->addWidget(viewBox);

    // ---- 帮助 ----
    auto* helpBox = new QGroupBox(QStringLiteral("帮助"), pane);
    auto* helpLayout = new QHBoxLayout(helpBox);
    auto* clearBtn = new QPushButton(QStringLiteral("清理缩略图缓存"), helpBox);
    auto* aboutBtn = new QPushButton(QStringLiteral("关于 WallDesk"), helpBox);
    auto* quitBtn = new QPushButton(QStringLiteral("退出程序"), helpBox);
    quitBtn->setObjectName(QStringLiteral("danger"));
    connect(clearBtn, &QPushButton::clicked, this, &MainWindow::onClearThumbCache);
    connect(aboutBtn, &QPushButton::clicked, this, &MainWindow::onAbout);
    connect(quitBtn, &QPushButton::clicked, this, &MainWindow::onQuit);
    helpLayout->addWidget(clearBtn);
    helpLayout->addWidget(aboutBtn);
    helpLayout->addWidget(quitBtn);
    helpLayout->addStretch(1);
    layout->addWidget(helpBox);

    layout->addStretch(1);
    return pane;
}

QWidget* MainWindow::buildMainToolbar()
{
    auto* bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("mainToolbar"));
    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(14, 10, 14, 10);
    layout->setSpacing(8);

    // ---- 库管理 ----
    auto* addImageBtn = new QPushButton(QStringLiteral("添加图片"), bar);
    addImageBtn->setToolTip(QStringLiteral("向壁纸库添加图片（Ctrl+Shift+I）"));
    auto* addVideoBtn = new QPushButton(QStringLiteral("添加视频"), bar);
    addVideoBtn->setToolTip(QStringLiteral("向壁纸库添加视频（Ctrl+Shift+V）"));
    auto* removeBtn = new QPushButton(QStringLiteral("移除"), bar);
    removeBtn->setObjectName(QStringLiteral("danger"));
    removeBtn->setToolTip(QStringLiteral("从库中移除选中项（不删除原文件）"));
    auto* clearBtn = new QPushButton(QStringLiteral("清空"), bar);
    clearBtn->setObjectName(QStringLiteral("danger"));
    clearBtn->setToolTip(QStringLiteral("清空整个壁纸库（不删除原文件）"));
    connect(addImageBtn, &QPushButton::clicked, this, &MainWindow::onAddImages);
    connect(addVideoBtn, &QPushButton::clicked, this, &MainWindow::onAddVideos);
    connect(removeBtn, &QPushButton::clicked, this, &MainWindow::onRemoveSelected);
    connect(clearBtn, &QPushButton::clicked, this, &MainWindow::onClearAll);
    layout->addWidget(addImageBtn);
    layout->addWidget(addVideoBtn);
    layout->addWidget(removeBtn);
    layout->addWidget(clearBtn);

    auto* sep = new QFrame(bar);
    sep->setFrameShape(QFrame::VLine);
    sep->setObjectName(QStringLiteral("toolbarSep"));
    layout->addWidget(sep);

    // ---- 播放控制 ----
    m_applyButton = new QPushButton(QStringLiteral("应用选中"), bar);
    m_applyButton->setObjectName(QStringLiteral("primary"));
    m_applyButton->setToolTip(QStringLiteral("把画廊中选中的项应用到桌面（回车）"));

    m_nextButton = new QPushButton(QStringLiteral("下一张"), bar);
    m_nextButton->setToolTip(QStringLiteral("切换到清单中的下一项（Ctrl+N）"));

    m_pauseButton = new QPushButton(QStringLiteral("暂停视频"), bar);
    m_pauseButton->setToolTip(QStringLiteral("暂停 / 继续视频壁纸（Ctrl+P）"));

    auto* stopBtn = new QPushButton(QStringLiteral("停止壁纸"), bar);
    stopBtn->setToolTip(QStringLiteral("停止视频壁纸，桌面恢复为系统壁纸"));

    auto* reattachBtn = new QPushButton(QStringLiteral("重新挂载"), bar);
    reattachBtn->setToolTip(QStringLiteral("桌面层失效后重新挂接视频窗口（F5）"));

    connect(m_applyButton, &QPushButton::clicked, this, &MainWindow::onApplySelected);
    connect(m_nextButton, &QPushButton::clicked, this, &MainWindow::onNext);
    connect(m_pauseButton, &QPushButton::clicked, this, &MainWindow::onTogglePause);
    connect(stopBtn, &QPushButton::clicked, this, &MainWindow::onStop);
    connect(reattachBtn, &QPushButton::clicked, this, &MainWindow::onReattach);

    layout->addWidget(m_applyButton);
    layout->addWidget(m_nextButton);
    layout->addWidget(m_pauseButton);
    layout->addWidget(stopBtn);
    layout->addWidget(reattachBtn);
    layout->addStretch(1);

    return bar;
}

// ---------------------------------------------------------------- 配置

void MainWindow::loadSettings()
{
    QSettings s;
    m_items.clear();
    const int count = s.beginReadArray(QStringLiteral("media"));
    for (int i = 0; i < count; ++i) {
        s.setArrayIndex(i);
        MediaItem item;
        item.type = (s.value(QStringLiteral("type"), 0).toInt() == 1) ? MediaItem::Type::Video
                                                                      : MediaItem::Type::Image;
        item.path = s.value(QStringLiteral("path")).toString();
        if (QFileInfo::exists(item.path)) {
            m_items.append(item);
        }
    }
    s.endArray();

    // 这些下拉框的写入会触发值变化槽，先屏蔽信号，加载完再统一应用
    m_fitCombo->blockSignals(true);
    m_screenCombo->blockSignals(true);
    m_monitorCombo->blockSignals(true);
    m_profileCombo->blockSignals(true);
    m_thumbSizeCombo->blockSignals(true);
    for (QAction* action : m_themeActions) {
        action->blockSignals(true);
    }
    for (QRadioButton* rb : m_themeRadios) {
        rb->blockSignals(true);
    }

    m_fitCombo->setCurrentIndex(s.value(QStringLiteral("fit"), 0).toInt());
    m_screenCombo->setCurrentIndex(s.value(QStringLiteral("screen"), 1).toInt());
    m_monitorCombo->setCurrentIndex(s.value(QStringLiteral("monitor"), 0).toInt());
    m_profileCombo->setCurrentIndex(s.value(QStringLiteral("profile"), 0).toInt());
    m_thumbSizeCombo->setCurrentIndex(
        qBound(0, s.value(QStringLiteral("thumbSize"), kThumbDefaultIndex).toInt(), 2));
    const int themeIndex = s.value(QStringLiteral("theme"), 2).toInt();
    for (int i = 0; i < m_themeActions.size(); ++i) {
        m_themeActions.at(i)->setChecked(i == themeIndex);
    }
    for (int i = 0; i < m_themeRadios.size(); ++i) {
        m_themeRadios.at(i)->setChecked(i == themeIndex);
    }
    m_volumeSlider->setValue(s.value(QStringLiteral("volume"), 0).toInt());
    m_intervalSpin->setValue(s.value(QStringLiteral("interval"), 30).toInt());
    m_autoSwitch->setChecked(s.value(QStringLiteral("autoSwitch"), false).toBool());
    m_pauseOnLock->setChecked(s.value(QStringLiteral("pauseOnLock"), true).toBool());
    m_pauseOnBattery->setChecked(s.value(QStringLiteral("pauseOnBattery"), false).toBool());
    m_pauseOnFullscreen->setChecked(s.value(QStringLiteral("pauseOnFullscreen"), true).toBool());
    m_videoThumbnails->setChecked(s.value(QStringLiteral("videoThumbnails"), true).toBool());
    m_restoreLast->setChecked(s.value(QStringLiteral("restoreLast"), true).toBool());
    m_autoStart->setChecked(isAutoStartEnabled());

    m_fitCombo->blockSignals(false);
    m_screenCombo->blockSignals(false);
    m_monitorCombo->blockSignals(false);
    m_profileCombo->blockSignals(false);
    m_thumbSizeCombo->blockSignals(false);
    for (QAction* action : m_themeActions) {
        action->blockSignals(false);
    }
    for (QRadioButton* rb : m_themeRadios) {
        rb->blockSignals(false);
    }

    // 窗口几何与导航页：缩放自适应的记忆部分
    const QByteArray geo = s.value(QStringLiteral("winGeometry")).toByteArray();
    if (!geo.isEmpty()) {
        restoreGeometry(geo);
    }
    m_nav->setCurrentRow(s.value(QStringLiteral("winPage"), 0).toInt());
    updateGalleryMetrics();

    m_theme = AppTheme::fromIndex(themeIndex);
    m_engine.setPerformanceProfile(
        static_cast<VlcProfile>(m_profileCombo->currentData().toInt()));

    refreshList();
    applyTarget();
    onAutoSwitchToggled(m_autoSwitch->isChecked());
}

void MainWindow::saveSettings()
{
    QSettings s;
    s.beginWriteArray(QStringLiteral("media"), m_items.size());
    for (int i = 0; i < m_items.size(); ++i) {
        s.setArrayIndex(i);
        s.setValue(QStringLiteral("type"), m_items[i].type == MediaItem::Type::Video ? 1 : 0);
        s.setValue(QStringLiteral("path"), m_items[i].path);
    }
    s.endArray();

    s.setValue(QStringLiteral("fit"), m_fitCombo->currentIndex());
    s.setValue(QStringLiteral("screen"), m_screenCombo->currentIndex());
    s.setValue(QStringLiteral("monitor"), m_monitorCombo->currentIndex());
    s.setValue(QStringLiteral("profile"), m_profileCombo->currentIndex());
    s.setValue(QStringLiteral("thumbSize"), m_thumbSizeCombo->currentIndex());
    for (int i = 0; i < m_themeActions.size(); ++i) {
        if (m_themeActions.at(i)->isChecked()) {
            s.setValue(QStringLiteral("theme"), i);
            break;
        }
    }
    s.setValue(QStringLiteral("winGeometry"), saveGeometry());
    s.setValue(QStringLiteral("winPage"), m_nav->currentIndex().row());
    s.setValue(QStringLiteral("volume"), m_volumeSlider->value());
    s.setValue(QStringLiteral("interval"), m_intervalSpin->value());
    s.setValue(QStringLiteral("autoSwitch"), m_autoSwitch->isChecked());
    s.setValue(QStringLiteral("pauseOnLock"), m_pauseOnLock->isChecked());
    s.setValue(QStringLiteral("pauseOnBattery"), m_pauseOnBattery->isChecked());
    s.setValue(QStringLiteral("pauseOnFullscreen"), m_pauseOnFullscreen->isChecked());
    s.setValue(QStringLiteral("videoThumbnails"), m_videoThumbnails->isChecked());
    s.setValue(QStringLiteral("restoreLast"), m_restoreLast->isChecked());
}

// ---------------------------------------------------------------- 列表与缩略图

void MainWindow::refreshList()
{
    // 旧任务先作废：库整体重建后，为已移除项排队解码纯属浪费
    if (m_thumbs) {
        m_thumbs->clearPending();
    }
    m_list->clear();
    for (const MediaItem& item : m_items) {
        const bool video = (item.type == MediaItem::Type::Video);
        // 不再给每项塞 QPixmap：缩略图由委托按需从内存 LRU 取，内存占用与库大小解耦
        auto* row = new QListWidgetItem(QFileInfo(item.path).fileName());
        row->setData(kRoleType, video ? 1 : 0);
        row->setData(kRolePath, item.path);
        row->setToolTip(item.path);
        row->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
        m_list->addItem(row);
    }

    if (m_libraryTitle) {
        m_libraryTitle->setText(m_items.isEmpty()
                                    ? QStringLiteral("壁纸库")
                                    : QStringLiteral("壁纸库 · %1 项").arg(m_items.size()));
    }
    if (m_galleryStack) {
        m_galleryStack->setCurrentIndex(m_items.isEmpty() ? 1 : 0);
    }
    requestVisibleThumbnails();

    // 同步应用/下一张按钮可用状态
    const int row = m_list->currentRow();
    const bool hasSelection = (row >= 0 && row < m_items.size());
    if (m_applyButton) {
        m_applyButton->setEnabled(hasSelection);
    }
    if (m_nextButton) {
        m_nextButton->setEnabled(!m_items.isEmpty());
    }
}

/**
 * 只为可见区域（上下各留一屏缓冲）发起缩略图请求。
 * 大库（几百项）下这一条把内存峰值从「全库解码」降到「一屏解码」。
 */
void MainWindow::requestVisibleThumbnails()
{
    if (!m_list || !m_thumbs || m_items.isEmpty()) {
        return;
    }
    const QRect visible = m_list->viewport()->rect().adjusted(0, -160, 0, 160);
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem* row = m_list->item(i);
        if (!row) {
            continue;
        }
        const QRect rect = m_list->visualItemRect(row);
        if (!rect.isValid() || !rect.intersects(visible)) {
            continue;
        }
        const bool video = row->data(kRoleType).toInt() == 1;
        if (video && !m_videoThumbnails->isChecked()) {
            continue; // 关掉开关就不去解码视频，省一次 IO
        }
        m_thumbs->request(row->data(kRolePath).toString(), video, m_list->iconSize());
    }
}

void MainWindow::onThumbSizeChanged(int)
{
    updateGalleryMetrics();
    if (m_thumbs) {
        m_thumbs->clearMemory(); // 旧尺寸的解码结果全部作废
    }
    if (m_list) {
        m_list->viewport()->update();
    }
    requestVisibleThumbnails();
    saveSettings();
}

void MainWindow::onThumbnailReady(const QString&, const QImage&)
{
    // 缩略图已进入内存 LRU，委托下次绘制时自取；这里只要求重绘可见区域
    if (m_list) {
        m_list->viewport()->update();
    }
}

// ---------------------------------------------------------------- 应用逻辑

void MainWindow::refreshMonitors()
{
    const int previous = m_monitorCombo->currentIndex();
    m_monitorCombo->blockSignals(true);
    m_monitorCombo->clear();

    const QList<MonitorInfo> monitors = WallpaperEngine::listMonitors();
    for (const MonitorInfo& monitor : monitors) {
        m_monitorCombo->addItem(monitor.label());
    }
    if (monitors.isEmpty()) {
        m_monitorCombo->addItem(QStringLiteral("（未检测到显示器）"));
    }

    const int index = (previous >= 0 && previous < m_monitorCombo->count()) ? previous : 0;
    m_monitorCombo->setCurrentIndex(index);
    m_monitorCombo->blockSignals(false);
}

void MainWindow::applyTarget()
{
    const auto target = static_cast<ScreenTarget>(m_screenCombo->currentData().toInt());
    m_engine.setTarget(target, m_monitorCombo->currentIndex());
    m_monitorCombo->setEnabled(target == ScreenTarget::Monitor);
    saveSettings();
}

void MainWindow::applyTheme()
{
    AppTheme::apply(m_theme);
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    // 只补加载新露出来的项，尺寸本身不随窗口变化（V4.5：修复缩放时缩略图忽大忽小）
    if (m_thumbTimer) {
        m_thumbTimer->start();
    }
}

void MainWindow::updateGalleryMetrics()
{
    if (!m_list) {
        return;
    }
    const int index = m_thumbSizeCombo ? m_thumbSizeCombo->currentIndex() : kThumbDefaultIndex;
    const QSize thumb = thumbSizeForIndex(index);
    const QSize grid = gridSizeForThumb(thumb);
    if (m_list->iconSize() != thumb) {
        m_list->setIconSize(thumb);
        m_list->setGridSize(grid);
        m_list->updateGeometry();
        m_list->viewport()->update();
    }
}

void MainWindow::refreshBackendInfo()
{
    // 后端状态只落日志与「关于」，主界面不再显示 libvlc 路径（V4.1 起隐藏）
    if (m_engine.videoBackendReady()) {
        const QString path = m_engine.backendPath();
        const bool bundled = VlcBundle::available()
                             && path.startsWith(VlcBundle::cacheDir(), Qt::CaseInsensitive);
        Logger::info(QStringLiteral("视频后端已加载：%1%2")
                         .arg(path, bundled ? QStringLiteral("（内置）") : QString()));
    } else {
        Logger::warning(QStringLiteral("视频后端未加载，仅图片壁纸可用"));
    }
}

void MainWindow::applyVideoPaused(bool paused, bool byPolicy)
{
    m_videoPaused = paused;
    m_pausedByPolicy = paused ? byPolicy : false;
    m_engine.setVideoPaused(paused);
    const QString text = paused ? QStringLiteral("继续视频") : QStringLiteral("暂停视频");
    m_pauseButton->setText(text);
    if (m_actPause) {
        m_actPause->setText(text);
    }
    updateFullscreenGuard();
}

void MainWindow::maybeRestoreLast()
{
    QSettings s;
    const bool want = m_restoreLast->isChecked();
    const QString path = s.value(QStringLiteral("lastPath")).toString();
    Logger::info(QStringLiteral("恢复检查：restoreLast=%1 path=%2 type=%3 exists=%4")
                     .arg(want ? QStringLiteral("开") : QStringLiteral("关"),
                          path.isEmpty() ? QStringLiteral("(空)") : path,
                          s.value(QStringLiteral("lastType")).toString(),
                          QFileInfo::exists(path) ? QStringLiteral("是") : QStringLiteral("否")));
    if (!want) {
        return;
    }
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        Logger::info(QStringLiteral("未恢复上次壁纸：路径为空或文件不存在（%1）").arg(path));
        return;
    }
    MediaItem item;
    item.type = (s.value(QStringLiteral("lastType"), 0).toInt() == 1) ? MediaItem::Type::Video
                                                                      : MediaItem::Type::Image;
    item.path = path;
    applyItem(item);
}

void MainWindow::applyItem(const MediaItem& item)
{
    QSettings s;
    s.setValue(QStringLiteral("lastPath"), item.path);
    s.setValue(QStringLiteral("lastType"), item.type == MediaItem::Type::Video ? 1 : 0);

    const QString name = QFileInfo(item.path).fileName();

    if (item.type == MediaItem::Type::Image) {
        const auto fit = static_cast<ImageFit>(m_fitCombo->currentData().toInt());
        QString err;
        if (m_engine.applyImage(item.path, fit, &err)) {
            Logger::info(QStringLiteral("图片壁纸已应用：%1（fit=%2）").arg(item.path).arg(static_cast<int>(fit)));
            updateStatus(QStringLiteral("已应用图片壁纸：%1").arg(name));
        } else {
            Logger::warning(QStringLiteral("图片壁纸应用失败：%1（%2）").arg(item.path, err));
            updateStatus(err, true);
        }
        return;
    }

    applyTarget(); // 保证覆盖范围已同步到引擎
    QString err;
    if (m_engine.applyVideo(item.path, m_volumeSlider->value(), &err)) {
        applyVideoPaused(false);
        updateFullscreenGuard();
        Logger::info(QStringLiteral("视频壁纸已应用：%1（volume=%2）")
                         .arg(item.path).arg(m_volumeSlider->value()));
        if (err.isEmpty()) {
            updateStatus(QStringLiteral("已应用视频壁纸：%1").arg(name));
        } else {
            updateStatus(err, true); // 降级模式：已生效但需提示
        }
    } else {
        Logger::warning(QStringLiteral("视频壁纸应用失败：%1（%2）").arg(item.path, err));
        updateStatus(err, true);
    }
}

void MainWindow::prepareBackend()
{
    if (!VlcBundle::available()) {
        finishBackendLoad(); // 没有内置运行时，直接走外部搜索
        return;
    }

    updateStatus(QStringLiteral("首次启动：正在解包内置视频引擎…"));
    m_busy->setVisible(true);
    VlcBundle::prepareAsync(this, [this](bool ok, const QString& err) {
        m_busy->setVisible(false);
        if (!ok) {
            updateStatus(QStringLiteral("内置视频引擎解包失败：%1").arg(err), true);
        }
        finishBackendLoad();
    });
}

void MainWindow::finishBackendLoad()
{
    QString err;
    m_backendReady = m_engine.ensureVideoBackend(&err);
    refreshBackendInfo();

    if (!m_backendReady) {
        updateStatus(QStringLiteral("提示：%1").arg(err), true);
    } else {
        updateStatus(QStringLiteral("就绪"));
    }

    // 视频缩略图要用到 libVLC，后端就绪后才能抓帧
    m_thumbs->setVideoGrabber([this](const QString& file, const QSize& size) -> QImage {
        if (!m_engine.videoBackendReady()) {
            return QImage();
        }
        const QString name = QStringLiteral("walldesk_%1.png")
                                 .arg(QString::fromLatin1(
                                     QCryptographicHash::hash(file.toUtf8(), QCryptographicHash::Md5).toHex()));
        const QString out = QDir::temp().filePath(name);
        if (!m_engine.snapshotVideo(file, out, size.width(), size.height())) {
            return QImage();
        }
        QImage image;
        image.load(out);
        QFile::remove(out);
        return image;
    });

    maybeRestoreLast();
    requestVisibleThumbnails();

    // 顺手清掉旧版本遗留的解包目录，失败也无副作用。
    // 用 detached 线程而非 std::async：后者返回的 future 析构时会阻塞等待任务完成。
    std::thread([] { VlcBundle::purgeStaleCaches(); }).detach();
}

void MainWindow::updateFullscreenGuard()
{
    // 只有在真的有视频在播、且用户开了这个开关时才轮询，避免无谓唤醒
    const bool wanted = m_pauseOnFullscreen->isChecked()
                        && !m_engine.currentVideo().isEmpty()
                        && m_engine.videoBackendReady();
    m_fullscreen->setEnabled(wanted);
}

// ---------------------------------------------------------------- 槽函数

void MainWindow::onAddImages()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, QStringLiteral("选择图片"), QString(),
        QStringLiteral("图片文件 (*.jpg *.jpeg *.png *.bmp *.webp *.gif);;所有文件 (*.*)"));
    if (files.isEmpty()) {
        return;
    }
    for (const QString& f : files) {
        m_items.append({MediaItem::Type::Image, QDir::toNativeSeparators(f)});
    }
    refreshList();
    saveSettings();
}

void MainWindow::onAddVideos()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, QStringLiteral("选择视频"), QString(),
        QStringLiteral("视频文件 (*.mp4 *.mkv *.webm *.avi *.mov *.wmv *.flv);;所有文件 (*.*)"));
    if (files.isEmpty()) {
        return;
    }
    for (const QString& f : files) {
        m_items.append({MediaItem::Type::Video, QDir::toNativeSeparators(f)});
    }
    refreshList();
    saveSettings();
}

void MainWindow::onRemoveSelected()
{
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_items.size()) {
        return;
    }
    m_items.removeAt(row);
    refreshList();
    saveSettings();
}

void MainWindow::onClearAll()
{
    m_items.clear();
    m_currentIndex = -1;
    refreshList();
    saveSettings();
}

void MainWindow::onApplySelected()
{
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_items.size()) {
        updateStatus(QStringLiteral("请先在画廊中选择一项"), true);
        return;
    }
    m_currentIndex = row;
    applyItem(m_items.at(row));
}

void MainWindow::onNext()
{
    if (m_items.isEmpty()) {
        return;
    }
    m_currentIndex = (m_currentIndex + 1) % m_items.size();
    m_list->setCurrentRow(m_currentIndex);
    applyItem(m_items.at(m_currentIndex));
}

void MainWindow::onStop()
{
    m_engine.stopVideo();
    updateFullscreenGuard();
    updateStatus(QStringLiteral("视频壁纸已停止，桌面恢复为当前图片壁纸"));
}

void MainWindow::onTogglePause()
{
    applyVideoPaused(!m_videoPaused);
}

void MainWindow::onAutoSwitchToggled(bool enabled)
{
    if (!m_timer) {
        return;
    }
    if (enabled) {
        m_timer->start(m_intervalSpin->value() * 60 * 1000);
    } else {
        m_timer->stop();
    }
    saveSettings();
}

void MainWindow::onSystemResume()
{
    if (m_engine.currentVideo().isEmpty()) {
        return;
    }
    // 休眠唤醒后桌面层句柄通常已失效，重新挂载并从上次位置续播
    if (m_videoPaused && m_pausedByPolicy) {
        applyVideoPaused(false);
    }
    if (m_engine.reattach()) {
        updateStatus(QStringLiteral("已从休眠恢复，视频壁纸已重新挂载"));
    }
}

void MainWindow::onSessionLock(bool locked)
{
    if (m_engine.currentVideo().isEmpty()) {
        return;
    }
    if (locked) {
        if (m_pauseOnLock->isChecked()) {
            applyVideoPaused(true, true);
        }
        return;
    }
    if (m_videoPaused && m_pausedByPolicy) {
        applyVideoPaused(false);
    }
    m_engine.reattach(); // 解锁后桌面层可能已重建
}

void MainWindow::onDisplayChanged()
{
    refreshMonitors();
    m_engine.refreshGeometry();
}

void MainWindow::onPowerStatusChanged()
{
    if (m_engine.currentVideo().isEmpty() || !m_pauseOnBattery->isChecked()) {
        return;
    }
    SYSTEM_POWER_STATUS status;
    if (!GetSystemPowerStatus(&status)) {
        return;
    }
    const bool onBattery = (status.ACLineStatus != kAcLineOnAc);
    if (onBattery) {
        applyVideoPaused(true, true);
    } else if (m_videoPaused && m_pausedByPolicy) {
        applyVideoPaused(false);
        m_engine.reattach();
    }
}

void MainWindow::onFullscreenChanged(bool active)
{
    if (m_engine.currentVideo().isEmpty()) {
        return;
    }
    if (active) {
        if (m_pauseOnFullscreen->isChecked()) {
            applyVideoPaused(true, true);
            updateStatus(QStringLiteral("检测到全屏应用，已暂停视频壁纸以节省资源"));
        }
        return;
    }
    if (m_videoPaused && m_pausedByPolicy) {
        applyVideoPaused(false);
        updateStatus(QStringLiteral("已退出全屏，视频壁纸继续播放"));
    }
}

void MainWindow::onReattach()
{
    if (m_engine.currentVideo().isEmpty()) {
        updateStatus(QStringLiteral("当前没有正在播放的视频壁纸，无需重新挂载"), true);
        return;
    }
    if (m_engine.reattach()) {
        updateStatus(QStringLiteral("已重新挂载视频壁纸（桌面层：%1）")
                         .arg(m_engine.isFallbackMode() ? QStringLiteral("降级模式")
                                                        : QStringLiteral("WorkerW")));
    } else {
        updateStatus(QStringLiteral("重新挂载失败。建议重启资源管理器后重试，"
                                    "或检查是否使用了第三方桌面工具。"),
                     true);
    }
}

void MainWindow::onLocateBackend()
{
    const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("选择 libvlc.dll"),
                                                      QString(), QStringLiteral("libvlc.dll"));
    if (file.isEmpty()) {
        return;
    }
    QString err;
    if (m_engine.loadBackendFrom(QFileInfo(file).absolutePath(), &err)) {
        refreshBackendInfo();
        updateStatus(QStringLiteral("已加载视频后端：%1").arg(m_engine.backendPath()));
    } else {
        updateStatus(err, true);
    }
}

void MainWindow::onThemeChanged(int index)
{
    m_theme = AppTheme::fromIndex(index);
    for (int i = 0; i < m_themeActions.size(); ++i) {
        m_themeActions.at(i)->setChecked(i == index);
    }
    for (int i = 0; i < m_themeRadios.size(); ++i) {
        m_themeRadios.at(i)->setChecked(i == index);
    }
    applyTheme();
    saveSettings();
}

void MainWindow::onProfileChanged(int)
{
    m_engine.setPerformanceProfile(static_cast<VlcProfile>(m_profileCombo->currentData().toInt()));
    saveSettings();
}

void MainWindow::onClearThumbCache()
{
    m_thumbs->clearMemory(); // 先释放已解码的，再清磁盘
    const int removed = ThumbnailLoader::clearCache();
    updateStatus(QStringLiteral("已清理 %1 个缩略图缓存文件，浏览时会重新生成。").arg(removed));
    refreshList();
}

void MainWindow::onVideoFailed(const QString& reason)
{
    Logger::warning(QStringLiteral("视频壁纸异常：%1").arg(reason));
    updateStatus(reason, true);
    refreshBackendInfo();
}

void MainWindow::onVideoRecovered()
{
    updateStatus(QStringLiteral("视频壁纸已自动恢复播放"));
}

void MainWindow::onAbout()
{
    // 不展示 libvlc 的具体路径，只给状态与修复指引
    const QString backend = m_engine.videoBackendReady()
                                ? QStringLiteral("已加载")
                                : QStringLiteral("未加载（仅图片壁纸可用，可到 设置 → 定位 libvlc 修复）");
    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("关于 WallDesk"));
    box.setIconPixmap(appIcon().pixmap(64, 64));
    box.setTextFormat(Qt::RichText);
    box.setText(QStringLiteral(
                    "<b>WallDesk %1</b><br>Windows 图片 / 视频壁纸工具<br><br>"
                    "Qt 运行库：%2（编译期 %3）<br>"
                    "运行模式：%4<br>"
                    "视频后端：%5<br>"
                    "数据目录：%6")
                    .arg(QCoreApplication::applicationVersion(),
                         QString::fromLatin1(qVersion()), QString::fromLatin1(QT_VERSION_STR),
                         AppPaths::modeText(), backend, QDir::toNativeSeparators(AppPaths::dataDir())));
    QPushButton* logBtn = box.addButton(QStringLiteral("打开日志目录"), QMessageBox::ActionRole);
    QPushButton* dataBtn = box.addButton(QStringLiteral("打开数据目录"), QMessageBox::ActionRole);
    box.addButton(QMessageBox::Close);
    box.exec();
    if (box.clickedButton() == logBtn) {
        openFolder(Logger::logDir());
    } else if (box.clickedButton() == dataBtn) {
        openFolder(AppPaths::dataDir());
    }
}

void MainWindow::onPortableToggled(bool enabled)
{
    if (enabled == AppPaths::portable()) {
        return;
    }
    QString err;
    if (AppPaths::setPortable(enabled, &err)) {
        Logger::info(QStringLiteral("便携模式已%1，重启后生效").arg(enabled ? "启用" : "停用"));
        updateStatus(QStringLiteral("已%1便携模式，重启 WallDesk 后生效。")
                         .arg(enabled ? QStringLiteral("启用") : QStringLiteral("停用")));
    } else {
        updateStatus(err, true);
        m_portable->blockSignals(true);
        m_portable->setChecked(!enabled);
        m_portable->blockSignals(false);
    }
}

void MainWindow::requestShow()
{
    showNormal();
    raise();
    activateWindow();
}

void MainWindow::handleCommand(const QString& command)
{
    Logger::info(QStringLiteral("收到实例间指令：%1").arg(command));
    if (command.compare(QStringLiteral("next"), Qt::CaseInsensitive) == 0) {
        onNext();
    } else if (command.compare(QStringLiteral("quit"), Qt::CaseInsensitive) == 0) {
        onQuit();
    } else {
        requestShow();
    }
}

void MainWindow::openFolder(const QString& path)
{
    QDir().mkpath(path);
    QDesktopServices::openUrl(QUrl::fromLocalFile(QDir::toNativeSeparators(path)));
}

void MainWindow::onTrayActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
        showNormal();
        raise();
        activateWindow();
    }
}

void MainWindow::onQuit()
{
    m_forceQuit = true;
    Logger::info(QStringLiteral("用户退出"));
    m_engine.stopVideo();
    saveSettings();
    qApp->quit();
}

// ---------------------------------------------------------------- 其它

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (m_forceQuit) {
        saveSettings();
        event->accept();
        return;
    }
    hide();
    event->ignore();
}

void MainWindow::updateStatus(const QString& text, bool warn)
{
    m_status->setText(text);
    // 用动态属性 + 重新抛光，交给样式表决定警告色，避免硬编码颜色与主题冲突
    m_status->setProperty("warn", warn);
    m_status->style()->unpolish(m_status);
    m_status->style()->polish(m_status);
}

void MainWindow::setAutoStart(bool enabled)
{
    QSettings run(QStringLiteral(R"(HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run)"),
                  QSettings::NativeFormat);
    if (enabled) {
        const QString cmd = QStringLiteral("\"%1\" --minimized")
                                .arg(QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
        run.setValue(QStringLiteral("WallDesk"), cmd);
    } else {
        run.remove(QStringLiteral("WallDesk"));
    }
}

bool MainWindow::isAutoStartEnabled() const
{
    QSettings run(QStringLiteral(R"(HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run)"),
                  QSettings::NativeFormat);
    return run.contains(QStringLiteral("WallDesk"));
}
