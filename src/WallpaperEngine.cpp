// 逐屏壁纸需要 IDesktopWallpaper；INITGUID 让 GUID 在本编译单元内生成实体
#define INITGUID
#include <windows.h>

#include <shobjidl.h>

#include "WallpaperEngine.h"

#include "AppPaths.h"
#include "Logger.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QMetaObject>
#include <QSettings>
#include <QThread>
#include <QTimer>
#include <QtGlobal>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <string>
#include <thread>

namespace {

/** 上限：连续重挂失败达到该次数后放弃重试并上报，避免无意义的忙等。 */
constexpr int kMaxRecoverFails = 3;
/** 看护定时器周期（毫秒）：正常 / 异常 / 暂停三种状态取不同值，避免无谓唤醒。 */
constexpr int kWatchdogNormalMs = 500;
constexpr int kWatchdogFastMs = 1000;
constexpr int kWatchdogIdleMs = 4000;
/** 连续多少次看护周期内播放位置未前进，即判定为卡死并重建播放。3 秒。 */
constexpr int kMaxStallTicks = 6;
/** 连续多少次判定为「已播完」仍救不回来，就升级为完整重建。 */
constexpr int kMaxEndTicks = 2;
/**
 * 片尾提前回卷窗口（毫秒）。
 * 在真正播完之前就 seek 回 0，播放器永远不进入 Ended 状态——
 * 既不会冻在最后一帧，也没有「片尾黑一下再接片头」的间隙。
 * 取值必须大于看护周期，否则可能一整轮都没踩进这个窗口。
 */
constexpr qint64 kLoopPreRollMs = 1200;

/**
 * 画面效果处理后图片的落盘位置：按「原图 + 修改时间 + 参数指纹」命名，
 * 同一张图同一套参数只处理一次，重复应用直接命中缓存。
 */
QString effectCachePath(const QString& source, const ImageEffect& fx)
{
    const QFileInfo info(source);
    const QByteArray seed = (source + QLatin1Char('|')
                             + info.lastModified().toString(Qt::ISODateWithMs) + QLatin1Char('|')
                             + fx.key())
                                .toUtf8();
    const QString hash = QString::fromLatin1(
        QCryptographicHash::hash(seed, QCryptographicHash::Md5).toHex());
    return AppPaths::dataDir() + QStringLiteral("/effects/") + hash + QStringLiteral(".png");
}

/** 需要效果时生成（或复用）处理后的副本，输出写入 targetOut。 */
bool prepareImageTarget(const QString& path, const ImageEffect& fx, QString* targetOut,
                        QString* err)
{
    const QFileInfo info(path);
    if (!info.exists()) {
        if (err) {
            *err = QStringLiteral("文件不存在：%1").arg(path);
        }
        return false;
    }
    const QString absolute = info.absoluteFilePath();
    if (fx.isDefault()) {
        *targetOut = absolute;
        return true;
    }
    const QString cache = effectCachePath(absolute, fx);
    if (!QFileInfo::exists(cache)) {
        QImage source;
        if (!source.load(absolute)) {
            if (err) {
                *err = QStringLiteral("无法读取图片：%1").arg(absolute);
            }
            return false;
        }
        QDir().mkpath(QFileInfo(cache).absolutePath());
        if (!ImageEffects::apply(source, fx).save(cache, "PNG")) {
            if (err) {
                *err = QStringLiteral("画面效果处理失败：%1").arg(absolute);
            }
            return false;
        }
    }
    *targetOut = cache;
    return true;
}

/** IDesktopWallpaper 的填充位姿枚举与本程序 ImageFit 的映射。 */
DESKTOP_WALLPAPER_POSITION wallpaperPosition(ImageFit fit)
{
    switch (fit) {
    case ImageFit::Fill:
        return DWPOS_FILL;
    case ImageFit::Fit:
        return DWPOS_FIT;
    case ImageFit::Stretch:
        return DWPOS_STRETCH;
    case ImageFit::Tile:
        return DWPOS_TILE;
    case ImageFit::Center:
        return DWPOS_CENTER;
    case ImageFit::Span:
        return DWPOS_SPAN;
    }
    return DWPOS_FILL;
}

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
        // 先尝试发送 0x052C，让它把壁纸层 WorkerW 创建/刷新出来。
        // 只发一次且超时压到 200ms：这个函数跑在界面线程，
        // 之前 3 次 × 500ms 最坏会把界面卡住 1.5 秒（「无响应」的来源之一）。
        DWORD_PTR unused = 0;
        SendMessageTimeoutW(progman, 0x052C, 0, 0, SMTO_NORMAL, 200, &unused);
        if (HWND worker = findWorkerWUnderProgman()) {
            return worker;
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

    // 去重：克隆/镜像模式可能让同一块物理屏出现多次（设备名与分辨率均相同）。
    // 保留第一个（通常为主排列），避免下拉框里出现「两个显示器 1」。
    {
        QHash<QString, int> seen;
        QList<MonitorInfo> unique;
        unique.reserve(result.size());
        for (const MonitorInfo& item : result) {
            const QString key = QStringLiteral("%1|%2x%3|%4")
                                    .arg(item.name)
                                    .arg(item.rect.width())
                                    .arg(item.rect.height())
                                    .arg(item.primary ? 1 : 0);
            if (!seen.contains(key)) {
                seen.insert(key, unique.size());
                unique.append(item);
            }
        }
        result = unique;
    }

    // 补上逐屏壁纸需要的显示器标识：IDesktopWallpaper 的枚举顺序与
    // EnumDisplayMonitors 一致，按下标配对即可；取不到就留空并回退全局设置。
    IDesktopWallpaper* wallpaper = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_DesktopWallpaper, nullptr, CLSCTX_ALL,
                                   IID_IDesktopWallpaper,
                                   reinterpret_cast<void**>(&wallpaper)))
        && wallpaper) {
        UINT count = 0;
        if (SUCCEEDED(wallpaper->GetMonitorDevicePathCount(&count))) {
            for (UINT i = 0; i < count && i < static_cast<UINT>(result.size()); ++i) {
                LPWSTR id = nullptr;
                if (SUCCEEDED(wallpaper->GetMonitorDevicePathAt(i, &id)) && id) {
                    result[static_cast<int>(i)].id = QString::fromWCharArray(id);
                    CoTaskMemFree(id);
                }
            }
        }
        wallpaper->Release();
    }

    for (int i = 0; i < result.size(); ++i) {
        result[i].index = i;
    }
    return result;
}

WallpaperEngine::WallpaperEngine(QObject* parent)
    : QObject(parent)
{
}

WallpaperEngine::~WallpaperEngine()
{
    stopWatchdog();
    ++m_epoch;
    if (m_asyncBusy.load()) {
        // 后台线程还在这个播放器上，不能在析构里释放，否则两个线程同时操作
        m_vlc.abandon();
    } else {
        m_vlc.release();
    }
    if (m_hostWnd) {
        DestroyWindow(m_hostWnd);
        m_hostWnd = nullptr;
    }
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

bool WallpaperEngine::applyImage(const QString& path, ImageFit fit, const ImageEffect& fx,
                                 QString* err)
{
    QString target;
    if (!prepareImageTarget(path, fx, &target, err)) {
        return false;
    }
    const QFileInfo info(target);

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
        && m_lastTile == tile && m_lastEffect == fx.key()) {
        // 参数未变也要保证视频已停：图片与视频互斥
        stopVideo();
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
    m_lastEffect = fx.key();
    m_lastImageOk = true;

    // 图片与视频互斥：先让新壁纸生效，再撤掉视频窗口，
    // 这样切换过程中桌面不会出现「先黑一下再出图」的顿挫。
    stopVideo();
    return true;
}

bool WallpaperEngine::applyImageToMonitor(const QString& path, const QString& monitorId,
                                          ImageFit fit, const ImageEffect& fx, QString* err)
{
    if (monitorId.isEmpty()) {
        return applyImage(path, fit, fx, err);
    }
    QString target;
    if (!prepareImageTarget(path, fx, &target, err)) {
        return false;
    }

    IDesktopWallpaper* wallpaper = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_DesktopWallpaper, nullptr, CLSCTX_ALL,
                                  IID_IDesktopWallpaper, reinterpret_cast<void**>(&wallpaper));
    if (FAILED(hr) || !wallpaper) {
        // 系统不支持逐屏接口（极老的 Windows）：退回全局设置，功能不丢
        return applyImage(path, fit, fx, err);
    }

    wallpaper->SetPosition(wallpaperPosition(fit));
    hr = wallpaper->SetWallpaper(reinterpret_cast<LPCWSTR>(monitorId.utf16()),
                                 reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(target).utf16()));
    wallpaper->Release();
    if (FAILED(hr)) {
        if (err) {
            *err = QStringLiteral("逐屏设置壁纸失败：%1（错误码 0x%2）")
                       .arg(QFileInfo(path).fileName())
                       .arg(static_cast<quint32>(hr), 8, 16, QLatin1Char('0'));
        }
        return false;
    }
    // 逐屏接口不走 SPI，去重状态作废，下次全局应用才会重新广播
    m_lastImageOk = false;
    return true;
}

void WallpaperEngine::setVideoEffect(const ImageEffect& fx)
{
    m_videoEffect = fx;
    m_vlc.setEffect(fx);
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
        // 子窗口形态：不抢焦点，且不干扰桌面图标。
        // 不带 WS_VISIBLE：等 VLC 起播（watchdog 确认 Playing）再显示，
        // 避免图片→视频切换时空白宿主先闪一下的顿挫。
        SetWindowLongPtrW(m_hostWnd, GWL_STYLE,
                          WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
        SetWindowLongPtrW(m_hostWnd, GWL_EXSTYLE, WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW);

        if (m_hostOnDefView) {
            // Win11：DefView 的图标层（SysListView32）背景不透明，宿主必须排到
            // 整个 DefView 子窗口链的最顶端，否则画面被图标层完全盖住（实测复现）。
            // 注意 SetWindowPos 的第二参数是「排在该窗口之后（更低）」，
            // 压到 listview 之下会被遮蔽，必须用 HWND_TOP。
            SetWindowPos(m_hostWnd, HWND_TOP, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    } else {
        // 降级形态：独立顶层无边框窗口，靠 Z 序置底
        if (GetParent(m_hostWnd)) {
            SetParent(m_hostWnd, nullptr);
        }
        SetWindowLongPtrW(m_hostWnd, GWL_STYLE,
                          WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
        SetWindowLongPtrW(m_hostWnd, GWL_EXSTYLE, WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW);
    }

    m_hostShown = false;
    refreshGeometry();
    return true;
}

void WallpaperEngine::refreshGeometry()
{
    if (!m_hostWnd) {
        return;
    }
    const QRect rect = targetRect();
    // 首帧渲染前不显示窗口：起播后再由看护逻辑展示，切换过程不露空白底
    const UINT showFlag = m_hostShown ? SWP_SHOWWINDOW : 0;
    const UINT flags = showFlag | SWP_NOACTIVATE
                           | (m_hostParent ? SWP_NOZORDER : 0);
    SetWindowPos(m_hostWnd, HWND_BOTTOM, rect.x(), rect.y(), rect.width(), rect.height(), flags);
}

void WallpaperEngine::destroyHostWindow()
{
    if (m_hostWnd) {
        DestroyWindow(m_hostWnd);
        m_hostWnd = nullptr;
    }
    m_hostParent = nullptr;
    m_hostShown = false;
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

    ++m_epoch;       // 让在途的后台自救任务自动失效，避免它把旧媒体重新拉起来
    m_stopRequested = false;
    waitAsync(1500); // 后台任务若仍在跑，等它收尾（同一播放器不能并发操作）

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
    m_stallCount = 0;
    m_endTicks = 0;

    // 视频与图片互斥：视频生效后清空图片去重缓存，
    // 确保下次切换回图片时不会误命中“与上次一致”而跳过。
    m_lastImagePath.clear();
    m_lastStyle.clear();
    m_lastTile.clear();
    m_lastImageOk = false;

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
    ++m_epoch; // 让在途后台任务失效：它们收尾时会补一次停止，不会把画面重新拉起
    m_stopRequested = true;

    // 先隐藏：窗口仍在，但视觉上立刻回到系统壁纸，不用等解码器收尾
    if (m_hostWnd) {
        ShowWindow(m_hostWnd, SW_HIDE);
    }

    // stop() 在解码卡死时会长时间阻塞，放到后台，界面不会跟着「无响应」
    const int epoch = m_epoch.load();
    if (!runAsync([this, epoch] {
            m_vlc.releaseMedia(); // 连同解码缓冲一起释放，切到图片壁纸后可省内存
            finishAsyncOp(epoch);
        })) {
        // 已有后台任务：它结束时发现 epoch 变了会自行收尾，这里不再插手
        Logger::info(QStringLiteral("停止壁纸：后台任务执行中，交由其收尾"));
    }

    m_currentFile.clear();
    m_recoverFails = 0;
    m_lastTime = 0;
    m_stallCount = 0;
    m_endTicks = 0;
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

bool WallpaperEngine::runAsync(std::function<void()> op)
{
    if (m_asyncBusy.exchange(true)) {
        return false; // 已有任务在跑：同一个播放器不能并发操作
    }
    std::thread([this, op] {
        op();
        m_asyncBusy.store(false);
    }).detach();
    return true;
}

void WallpaperEngine::waitAsync(int maxMs)
{
    const int step = 25;
    for (int waited = 0; waited < maxMs && m_asyncBusy.load(); waited += step) {
        QThread::msleep(step);
    }
}

void WallpaperEngine::finishAsyncOp(int epoch)
{
    if (m_epoch.load() != epoch) {
        // 已被「停止壁纸 / 换源」取代：补一次停止，别把旧画面重新拉起来
        m_vlc.releaseMedia();
    }
    // 窗口只能在创建它的线程销毁，排回界面线程。
    // 期间用户可能又应用了新壁纸（窗口被复用），此时绝不能销毁。
    QMetaObject::invokeMethod(this, [this] {
        if (m_stopRequested) {
            destroyHostWindow();
        }
    }, Qt::QueuedConnection);
}

void WallpaperEngine::recoverPlayback(bool full)
{
    if (m_currentFile.isEmpty()) {
        return;
    }
    const QString file = m_currentFile;
    const HWND hwnd = m_hostWnd;
    const int volume = m_volume;
    const int epoch = m_epoch.load();
    m_lastTime = 0;
    m_stallCount = 0;

    if (!runAsync([this, file, hwnd, volume, full, epoch] {
            bool ok = m_vlc.restart(); // 轻量：stop + play，不重建 media
            if (!ok || full) {
                ok = m_vlc.play(file, reinterpret_cast<void*>(hwnd), volume, true);
            }
            if (!ok) {
                QMetaObject::invokeMethod(this, [this] {
                    if (++m_recoverFails >= kMaxRecoverFails) {
                        stopWatchdog();
                        emit videoFailed(QStringLiteral("视频壁纸连续 %1 次自救失败，已停止自动恢复。"
                                                        "可点「重新挂载」或重启程序重试。")
                                             .arg(m_recoverFails));
                    } else {
                        reattach();
                    }
                }, Qt::QueuedConnection);
                return;
            }
            m_recoverFails = 0;
            finishAsyncOp(epoch);
        })) {
        Logger::info(QStringLiteral("已有自救任务在执行，本次不再重复触发"));
    }
}

void WallpaperEngine::onWatchdog()
{
    if (!m_hostWnd) {
        stopWatchdog();
        return;
    }
    // 后台任务正在操作同一个播放器：这一轮只做窗口存活检查，不碰 libvlc
    if (m_asyncBusy.load() && m_vlc.isLoaded()) {
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
        const qint64 now = m_vlc.time();
        const qint64 total = m_vlc.length();

        if (total > kLoopPreRollMs && now >= total - kLoopPreRollMs) {
            // 片尾提前回卷：趁播放器还活着 seek 回 0，根本不进入 Ended 状态，
            // 所以既不会冻在末帧，也没有片尾到片头的黑帧间隙。
            m_vlc.setTime(0);
            m_lastTime = 0;
            m_stallCount = 0;
            m_endTicks = 0;
        } else if (now > 0 && now != m_lastTime) {
            m_stallCount = 0; // 位置在前进，正常
            m_endTicks = 0;
            m_lastTime = now;
        } else if (++m_stallCount >= kMaxStallTicks) {
            // 状态是 Playing 但位置不前进（或时间读不出来）= 解码链路卡死。
            // 后台重建播放：主线程不碰可能阻塞的 stop()，界面不会无响应。
            m_stallCount = 0;
            Logger::warning(QStringLiteral("播放位置停滞（time=%1），后台重建播放").arg(now));
            recoverPlayback(true);
            return;
        }

        // 首帧已渲染出来，此时才把宿主窗口亮出来，避免切换时闪空白底
        if (!m_hostShown && IsWindow(hwnd)) {
            ShowWindow(hwnd, SW_SHOW);
            m_hostShown = true;
            refreshGeometry();
        }
        updateWatchdogInterval(); // 状态已稳定，回到常规间隔
        return;
    }
    if (current == PlaybackState::Ended || current == PlaybackState::Stopped) {
        // 播完（或意外停止）：stop + play 重播。只靠 set_time(0)+play 救不回来
        // ——实测会每秒都判成 Ended、画面冻在末帧，所以必须先 stop 回收 input。
        // 若连续几轮都救不回来，升级为完整重建（换新的 media 起播）。
        m_stallCount = 0;
        if (m_endTicks < kMaxEndTicks) {
            ++m_endTicks;
            Logger::info(QStringLiteral("视频播放到结尾（状态 %1），重新循环").arg(static_cast<int>(current)));
            if (!m_vlc.restart()) {
                Logger::warning(QStringLiteral("轻量重播失败，改为后台完整重播"));
                recoverPlayback(true);
            }
        } else {
            m_endTicks = 0;
            Logger::warning(QStringLiteral("连续多次重播无效，后台完整重建播放"));
            recoverPlayback(true);
        }
        return;
    }
    if (current == PlaybackState::Error) {
        Logger::warning(QStringLiteral("播放状态异常（Error），尝试重新挂载"));
        reattach();
        return;
    }
    // 起播阶段（Opening / Buffering）：不打日志刷屏，等下一轮再看
}
