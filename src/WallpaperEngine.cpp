#include <windows.h>

#include "WallpaperEngine.h"

#include "Logger.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QTimer>
#include <QtGlobal>
#include <cstring>
#include <string>

namespace {

/** 上限：连续重挂失败达到该次数后放弃重试并上报，避免无意义的忙等。 */
constexpr int kMaxRecoverFails = 3;
/** 看护定时器周期（毫秒）：正常 / 异常 / 暂停三种状态取不同值，避免无谓唤醒。 */
constexpr int kWatchdogNormalMs = 5000;
constexpr int kWatchdogFastMs = 2000;
constexpr int kWatchdogIdleMs = 8000;

/** 判断窗口是否覆盖整个主屏幕（允许少量误差）。 */
bool coversScreen(HWND hwnd)
{
    RECT rc;
    if (!GetWindowRect(hwnd, &rc)) {
        return false;
    }
    const int sw = GetSystemMetrics(SM_CXSCREEN);
    const int sh = GetSystemMetrics(SM_CYSCREEN);
    return (rc.left <= 0 && rc.top <= 0
            && (rc.right - rc.left) >= sw - 4
            && (rc.bottom - rc.top) >= sh - 4);
}

/** 策略一（Win10/11 首选）：Progman 子窗口中的 WorkerW 壁纸层。
 *  Win11 23H2+ 不再生成独立的可见顶层 WorkerW，但向 Progman 发 0x052C 后
 *  会在 Progman 子窗口链里生成一个位于 SHELLDLL_DefView 之后的 WorkerW，
 *  它才是真正的桌面壁纸层（视频挂到这里会位于图标之下）。 */
HWND findWorkerWUnderProgman()
{
    HWND progman = FindWindowW(L"Progman", nullptr);
    if (!progman) {
        return nullptr;
    }
    HWND child = nullptr;
    while ((child = FindWindowExW(progman, child, L"WorkerW", nullptr)) != nullptr) {
        if (IsWindowVisible(child) && coversScreen(child)) {
            return child;
        }
    }
    return nullptr;
}

/** 策略二（Win7/8/10 回退）：顶层窗口中，在 DefView 之后的 WorkerW 兄弟窗口。 */
HWND findWorkerWAfterDefView()
{
    HWND workerw = nullptr;
    EnumWindows(
        [](HWND hwnd, LPARAM lParam) -> BOOL {
            HWND defView = FindWindowExW(hwnd, nullptr, L"SHELLDLL_DefView", nullptr);
            if (defView) {
                HWND* out = reinterpret_cast<HWND*>(lParam);
                *out = FindWindowExW(nullptr, hwnd, L"WorkerW", nullptr);
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&workerw));
    return workerw;
}

/** 策略三：直接枚举顶层窗口，找类名为 WorkerW 且可见、不含图标层的窗口。 */
HWND findWorkerWByClass()
{
    HWND found = nullptr;
    EnumWindows(
        [](HWND hwnd, LPARAM lParam) -> BOOL {
            wchar_t className[64] = {0};
            if (GetClassNameW(hwnd, className, 64) == 0) {
                return TRUE;
            }
            if (std::wstring(className) != L"WorkerW") {
                return TRUE;
            }
            if (!IsWindowVisible(hwnd)) {
                return TRUE;
            }
            if (FindWindowExW(hwnd, nullptr, L"SHELLDLL_DefView", nullptr)) {
                return TRUE;
            }
            *reinterpret_cast<HWND*>(lParam) = hwnd;
            return FALSE;
        },
        reinterpret_cast<LPARAM>(&found));
    return found;
}

/**
 * 桌面壁纸层定位。
 * 0x052C 是未公开的 Progman 消息，用于让资源管理器生成可分层的 WorkerW。
 * Win11 上 WorkerW 位于 Progman 子窗口链中、SHELLDLL_DefView 之后，因此
 * 把视频宿主挂到该 WorkerW 上即可让视频位于桌面图标之下。
 */
HWND findDesktopWorkerW()
{
    HWND progman = FindWindowW(L"Progman", nullptr);
    if (progman) {
        // 先尝试发送 0x052C，让它把壁纸层 WorkerW 创建/刷新出来
        for (int attempt = 0; attempt < 3; ++attempt) {
            DWORD_PTR unused = 0;
            SendMessageTimeoutW(progman, 0x052C, 0, 0, SMTO_NORMAL, 500, &unused);
            if (HWND worker = findWorkerWUnderProgman()) {
                return worker;
            }
        }
    }
    if (HWND worker = findWorkerWAfterDefView()) {
        if (IsWindowVisible(worker)) {
            return worker;
        }
    }
    return findWorkerWByClass();
}

/** 把系统错误码翻译成可读文本，便于界面直接给出可操作的提示。 */
QString systemErrorMessage(DWORD code)
{
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    if (length == 0 || !buffer) {
        return QStringLiteral("错误码 %1").arg(code);
    }
    const QString text = QString::fromWCharArray(buffer, static_cast<int>(length)).trimmed();
    LocalFree(buffer);
    return text;
}

constexpr wchar_t kHostClassName[] = L"WallDeskHostWnd";

LRESULT CALLBACK hostWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

QList<MonitorInfo> WallpaperEngine::listMonitors()
{
    QList<MonitorInfo> result;
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR monitor, HDC, LPRECT, LPARAM lParam) -> BOOL {
            MONITORINFOEXW info;
            memset(&info, 0, sizeof(info));
            info.cbSize = sizeof(info);
            if (!GetMonitorInfoW(monitor, &info)) {
                return TRUE;
            }
            MonitorInfo item;
            item.name = QString::fromWCharArray(info.szDevice);
            item.rect = QRect(info.rcMonitor.left, info.rcMonitor.top,
                              info.rcMonitor.right - info.rcMonitor.left,
                              info.rcMonitor.bottom - info.rcMonitor.top);
            item.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
            reinterpret_cast<QList<MonitorInfo>*>(lParam)->append(item);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&result));
    return result;
}

WallpaperEngine::WallpaperEngine(QObject* parent)
    : QObject(parent)
{
}

WallpaperEngine::~WallpaperEngine()
{
    stopVideo();
}

bool WallpaperEngine::ensureVideoBackend(QString* err)
{
    if (m_vlc.isLoaded()) {
        return true;
    }
    return m_vlc.load(QCoreApplication::applicationDirPath(), err);
}

bool WallpaperEngine::loadBackendFrom(const QString& directory, QString* err)
{
    return m_vlc.loadFromDirectory(directory, err);
}

bool WallpaperEngine::applyImage(const QString& path, ImageFit fit, QString* err)
{
    const QFileInfo info(path);
    if (!info.exists()) {
        if (err) {
            *err = QStringLiteral("文件不存在：%1").arg(path);
        }
        return false;
    }

    // 填充方式写入注册表，随后必须再触发一次 SPI_SETDESKWALLPAPER 才会生效
    QSettings reg(QStringLiteral(R"(HKEY_CURRENT_USER\Control Panel\Desktop)"), QSettings::NativeFormat);

    QString style = QStringLiteral("10");
    QString tile = QStringLiteral("0");
    switch (fit) {
    case ImageFit::Fill:
        style = QStringLiteral("10");
        break;
    case ImageFit::Fit:
        style = QStringLiteral("6");
        break;
    case ImageFit::Stretch:
        style = QStringLiteral("2");
        break;
    case ImageFit::Tile:
        style = QStringLiteral("0");
        tile = QStringLiteral("1");
        break;
    case ImageFit::Center:
        style = QStringLiteral("0");
        break;
    case ImageFit::Span:
        style = QStringLiteral("22");
        break;
    }
    // 参数与上次完全一致且上次已成功：直接返回，省掉一次注册表写入和一次系统广播
    const QString absolute = info.absoluteFilePath();
    if (m_lastImageOk && m_lastImagePath == absolute && m_lastStyle == style
        && m_lastTile == tile) {
        return true;
    }

    reg.setValue(QStringLiteral("WallpaperStyle"), style);
    reg.setValue(QStringLiteral("TileWallpaper"), tile);
    reg.sync();

    const std::wstring native = QDir::toNativeSeparators(info.absoluteFilePath()).toStdWString();
    if (!SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0,
                               reinterpret_cast<void*>(const_cast<wchar_t*>(native.c_str())),
                               SPIF_UPDATEINIFILE | SPIF_SENDWININICHANGE)) {
        m_lastImageOk = false;
        if (err) {
            *err = QStringLiteral("系统接口设置壁纸失败：%1（%2）")
                       .arg(systemErrorMessage(GetLastError()), info.fileName());
        }
        return false;
    }

    m_lastImagePath = absolute;
    m_lastStyle = style;
    m_lastTile = tile;
    m_lastImageOk = true;
    return true;
}

HWND WallpaperEngine::resolveWorkerW()
{
    if (m_workerW && IsWindow(m_workerW)) {
        return m_workerW;
    }
    m_workerW = findDesktopWorkerW();
    return m_workerW;
}

/**
 * 确定宿主窗口的父窗口。
 * 按可见性优先级依次尝试：
 *   1. 桌面壁纸层 WorkerW：Win7~Win10 为顶层窗口，Win11 为 Progman 子窗口，
 *      都位于 SHELLDLL_DefView（图标层）之后，视频自然在桌面图标之下；
 *   2. 回退到 SHELLDLL_DefView（图标容器）下并置顶其 Z 序 —— 视频可见但会
 *      覆盖桌面图标，仅在前一种情况不可用时使用；
 *   3. Progman 直接子窗口（部分环境仍可见）；
 *   4. 独立置底顶层窗口。
 */
HWND WallpaperEngine::resolveHostParent()
{
    if (HWND worker = resolveWorkerW()) {
        m_usingFallback = false;
        m_hostOnDefView = false;
        return worker;
    }

    m_workerW = nullptr;

    if (HWND progman = FindWindowW(L"Progman", nullptr)) {
        if (HWND defView = FindWindowExW(progman, nullptr, L"SHELLDLL_DefView", nullptr)) {
            m_usingFallback = false;
            m_hostOnDefView = true;
            return defView;
        }
        m_usingFallback = true;
        m_hostOnDefView = false;
        return progman;
    }

    m_usingFallback = true;
    m_hostOnDefView = false;
    return nullptr; // 独立顶层窗口，靠 Z 序置底
}

QRect WallpaperEngine::targetRect() const
{
    if (m_target == ScreenTarget::Monitor) {
        const QList<MonitorInfo> monitors = listMonitors();
        if (monitors.isEmpty()) {
            return QRect(0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
        }
        const int index = (m_monitorIndex >= 0 && m_monitorIndex < monitors.size()) ? m_monitorIndex : 0;
        return monitors.at(index).rect;
    }

    if (m_target == ScreenTarget::Virtual) {
        return QRect(GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
                     GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN));
    }

    return QRect(0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
}

bool WallpaperEngine::prepareHostWindow()
{
    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSEXW wc;
        memset(&wc, 0, sizeof(wc));
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = hostWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kHostClassName;
        classRegistered = RegisterClassExW(&wc) != 0;
    }

    if (!m_hostWnd) {
        // 宿主必须是纯 Win32 窗口：QWidget 挂 WA_PaintOnScreen 后其绘制管线
        // 会让 libVLC 的画面在某些驱动下整窗黑屏（实测复现），原生窗口无此问题。
        m_hostWnd = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
                                    kHostClassName, L"WallDesk Video",
                                    WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
                                    0, 0, 1, 1, nullptr, nullptr,
                                    GetModuleHandleW(nullptr), nullptr);
        if (!m_hostWnd) {
            Logger::warning(QStringLiteral("创建视频宿主窗口失败：%1").arg(GetLastError()));
            return false;
        }
    }

    m_hostParent = resolveHostParent();

    if (m_hostParent) {
        if (GetParent(m_hostWnd) != m_hostParent) {
            SetParent(m_hostWnd, m_hostParent);
        }
        // 子窗口形态：不抢焦点，且不干扰桌面图标
        SetWindowLongPtrW(m_hostWnd, GWL_STYLE,
                          WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
        SetWindowLongPtrW(m_hostWnd, GWL_EXSTYLE, WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW);

        if (m_hostOnDefView) {
            // Win11：DefView 的图标层（SysListView32）背景不透明，宿主必须排到
            // 整个 DefView 子窗口链的最顶端，否则画面被图标层完全盖住（实测复现）。
            // 注意 SetWindowPos 的第二参数是「排在该窗口之后（更低）」，
            // 压到 listview 之下会被遮蔽，必须用 HWND_TOP。
            SetWindowPos(m_hostWnd, HWND_TOP, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        }
    } else {
        // 降级形态：独立顶层无边框窗口，靠 Z 序置底
        if (GetParent(m_hostWnd)) {
            SetParent(m_hostWnd, nullptr);
        }
        SetWindowLongPtrW(m_hostWnd, GWL_STYLE,
                          WS_POPUP | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
        SetWindowLongPtrW(m_hostWnd, GWL_EXSTYLE, WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW);
    }

    refreshGeometry();
    return true;
}

void WallpaperEngine::refreshGeometry()
{
    if (!m_hostWnd) {
        return;
    }
    const QRect rect = targetRect();
    const UINT flags = m_hostParent
                           ? (SWP_SHOWWINDOW | SWP_NOACTIVATE | SWP_NOZORDER)
                           : (SWP_SHOWWINDOW | SWP_NOACTIVATE); // 置底仅对顶层窗口有意义
    SetWindowPos(m_hostWnd, HWND_BOTTOM, rect.x(), rect.y(), rect.width(), rect.height(), flags);
}

void WallpaperEngine::destroyHostWindow()
{
    if (m_hostWnd) {
        DestroyWindow(m_hostWnd);
        m_hostWnd = nullptr;
    }
    m_hostParent = nullptr;
}

bool WallpaperEngine::applyVideo(const QString& path, int volume, QString* err)
{
    if (!QFileInfo::exists(path)) {
        if (err) {
            *err = QStringLiteral("文件不存在：%1").arg(path);
        }
        return false;
    }

    if (!ensureVideoBackend(err)) {
        return false;
    }

    if (!prepareHostWindow()) {
        if (err) {
            *err = QStringLiteral("无法创建视频宿主窗口，请重试或重启程序。");
        }
        return false;
    }

    const QRect rect = targetRect();
    const QString mode = m_usingFallback ? QStringLiteral("降级")
                         : m_hostOnDefView ? QStringLiteral("DefView(Win11)")
                                           : QStringLiteral("WorkerW");
    Logger::info(QStringLiteral("视频宿主窗口：hwnd=0x%1 父窗口=0x%2 区域=%3x%4%5 模式=%6")
                     .arg(reinterpret_cast<quintptr>(m_hostWnd), 0, 16)
                     .arg(reinterpret_cast<quintptr>(m_hostParent), 0, 16)
                     .arg(rect.width())
                     .arg(rect.height())
                     .arg(QStringLiteral("(%1,%2)").arg(rect.x()).arg(rect.y()))
                     .arg(mode));

    if (!m_vlc.play(path, reinterpret_cast<void*>(m_hostWnd), volume, true)) {
        if (err) {
            *err = QStringLiteral("libVLC 无法播放该文件（编码可能不受支持）：%1").arg(path);
        }
        Logger::warning(QStringLiteral("libVLC 起播失败：%1").arg(path));
        destroyHostWindow();
        return false;
    }

    m_currentFile = path;
    m_volume = volume;
    m_recoverFails = 0;
    m_lastTime = 0;
    startWatchdog();

    if (m_usingFallback && err) {
        *err = QStringLiteral("已应用，但处于降级模式：未能定位桌面 WorkerW 图层，"
                              "视频可能覆盖桌面图标。建议重启资源管理器后点「重新挂载」。");
    }
    return true;
}

bool WallpaperEngine::reattach()
{
    if (m_currentFile.isEmpty()) {
        return false;
    }

    // 先留住播放位置，重建后从该位置继续
    if (m_vlc.isLoaded()) {
        const qint64 pos = m_vlc.time();
        if (pos > 0) {
            m_lastTime = pos;
        }
    }

    destroyHostWindow();
    m_workerW = nullptr; // 强制重新定位桌面层

    QString err;
    if (!applyVideo(m_currentFile, m_volume, &err)) {
        ++m_recoverFails;
        if (m_recoverFails >= kMaxRecoverFails) {
            stopWatchdog();
            emit videoFailed(QStringLiteral("视频壁纸连续 %1 次重挂失败，已停止自动恢复。%2")
                                 .arg(m_recoverFails)
                                 .arg(err));
        }
        return false;
    }

    if (m_lastTime > 0) {
        m_vlc.setTime(m_lastTime);
    }
    if (!m_pausedByUser) {
        m_vlc.setPaused(false);
    }

    const bool recovered = (m_recoverFails > 0);
    m_recoverFails = 0;
    if (recovered) {
        emit videoRecovered();
    }
    return true;
}

void WallpaperEngine::setTarget(ScreenTarget target, int monitorIndex)
{
    m_target = target;
    m_monitorIndex = monitorIndex;
    if (m_hostWnd) {
        refreshGeometry();
    }
}

void WallpaperEngine::stopVideo()
{
    stopWatchdog();
    m_vlc.stop();
    destroyHostWindow();
    m_currentFile.clear();
    m_recoverFails = 0;
    m_lastTime = 0;
}

void WallpaperEngine::setVideoPaused(bool paused)
{
    m_pausedByUser = paused;
    m_vlc.setPaused(paused);
    updateWatchdogInterval();
}

void WallpaperEngine::setVideoVolume(int volume)
{
    m_volume = volume;
    m_vlc.setVolume(volume);
}

bool WallpaperEngine::snapshotVideo(const QString& file, const QString& outPng, int width, int height)
{
    if (!m_vlc.isLoaded()) {
        return false;
    }
    return m_vlc.takeSnapshot(file, outPng, width, height);
}

void WallpaperEngine::setPerformanceProfile(VlcProfile profile)
{
    if (m_vlc.profile() == profile) {
        return;
    }
    m_vlc.setProfile(profile);

    // libvlc 的实例参数不能热改，必须重建实例：先记住位置，重建后续播
    if (!m_vlc.isLoaded() || m_currentFile.isEmpty()) {
        return; // 没在播放，下次应用时自然生效
    }

    const qint64 position = m_vlc.time();
    m_vlc.release();

    QString err;
    if (!ensureVideoBackend(&err)) {
        emit videoFailed(err);
        return;
    }
    if (applyVideo(m_currentFile, m_volume, &err) && position > 0) {
        m_vlc.setTime(position);
    }
}

void WallpaperEngine::startWatchdog()
{
    if (!m_watchdog) {
        m_watchdog = new QTimer(this);
        connect(m_watchdog, &QTimer::timeout, this, &WallpaperEngine::onWatchdog);
    }
    updateWatchdogInterval();
    if (!m_watchdog->isActive()) {
        m_watchdog->start();
    }
}

void WallpaperEngine::stopWatchdog()
{
    if (m_watchdog) {
        m_watchdog->stop();
    }
}

void WallpaperEngine::updateWatchdogInterval()
{
    if (!m_watchdog) {
        return;
    }
    // 刚出过问题就查得勤一点；暂停时几乎无事可做，拉长间隔省电
    const int interval = (m_recoverFails > 0) ? kWatchdogFastMs
                                              : (m_pausedByUser ? kWatchdogIdleMs : kWatchdogNormalMs);
    if (m_watchdog->interval() != interval) {
        m_watchdog->setInterval(interval);
    }
}

void WallpaperEngine::onWatchdog()
{
    if (!m_hostWnd) {
        stopWatchdog();
        return;
    }

    const HWND hwnd = m_hostWnd;
    const bool hostAlive = IsWindow(hwnd);
    const bool parentAlive = m_hostParent ? IsWindow(m_hostParent) : true;
    const bool stillAttached = m_hostParent ? (GetParent(hwnd) == m_hostParent) : true;
    // 桌面层被 explorer 隐藏（重建/切换）时同样需要重挂，否则视频跟着隐身
    const bool parentVisible = m_hostParent ? IsWindowVisible(m_hostParent) : true;

    if (!hostAlive || !parentAlive || !parentVisible || !stillAttached) {
        reattach();
        return;
    }

    if (!m_vlc.isLoaded() || m_pausedByUser) {
        return;
    }

    const PlaybackState current = m_vlc.state();
    if (current == PlaybackState::Playing) {
        m_lastTime = m_vlc.time();
        updateWatchdogInterval(); // 状态已稳定，回到常规间隔
        return;
    }
    if (current == PlaybackState::Ended) {
        // 实测 input-repeat 在部分文件上不生效，播完就地重播，不重建窗口避免闪烁
        Logger::info(QStringLiteral("视频播放到结尾，自动重新循环"));
        m_vlc.play(m_currentFile, reinterpret_cast<void*>(m_hostWnd), m_volume, true);
        return;
    }
    if (current == PlaybackState::Error) {
        Logger::warning(QStringLiteral("播放状态异常（Error），尝试重新挂载"));
        reattach();
        return;
    }
    Logger::info(QStringLiteral("播放状态：%1（等待起播）").arg(static_cast<int>(current)));
}
