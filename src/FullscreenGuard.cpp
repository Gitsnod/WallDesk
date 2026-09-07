#include "FullscreenGuard.h"

#include <windows.h>

#include <QTimer>
#include <cstring>
#include <string>

namespace {

/** 检测间隔（毫秒）。2 秒一次足够及时，且开销可忽略（只查一个窗口）。 */
constexpr int kCheckIntervalMs = 2000;
/** 状态切换所需的连续一致采样次数，用于去抖。 */
constexpr int kStableSamples = 2;
/** 几何比较容差（像素）；部分窗口边框会超出屏幕 1-2 像素。 */
constexpr int kTolerance = 4;

bool isShellOrDesktopWindow(HWND hwnd)
{
    wchar_t cls[64] = {0};
    if (GetClassNameW(hwnd, cls, 64) == 0) {
        return false;
    }
    const std::wstring name(cls);
    return name == L"Progman"        // 桌面的主窗口
           || name == L"WorkerW"     // 桌面分层窗口（我们的宿主就挂在这里）
           || name == L"Shell_TrayWnd"
           || name == L"Shell_SecondaryTrayWnd"
           || name == L"Button";     // 开始按钮，任务栏可见时前台可能命中
}

} // namespace

FullscreenGuard::FullscreenGuard(QObject* parent)
    : QObject(parent)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(kCheckIntervalMs);
    connect(m_timer, &QTimer::timeout, this, &FullscreenGuard::onTick);
}

void FullscreenGuard::setEnabled(bool enabled)
{
    if (enabled) {
        m_stableCount = 0;
        m_lastSample = foregroundIsFullscreen();
        m_timer->start();
    } else {
        m_timer->stop();
        if (m_active) {
            setActive(false);
        }
    }
}

bool FullscreenGuard::foregroundIsFullscreen()
{
    const HWND hwnd = GetForegroundWindow();
    if (!hwnd || hwnd == GetDesktopWindow() || hwnd == GetShellWindow()) {
        return false;
    }
    if (IsIconic(hwnd) || !IsWindowVisible(hwnd)) {
        return false;
    }
    if (isShellOrDesktopWindow(hwnd)) {
        return false;
    }

    RECT rect = {0, 0, 0, 0};
    if (!GetWindowRect(hwnd, &rect)) {
        return false;
    }

    const HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONULL);
    if (!monitor) {
        return false;
    }
    MONITORINFO info;
    memset(&info, 0, sizeof(info));
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) {
        return false;
    }

    return rect.left <= info.rcMonitor.left + kTolerance
           && rect.top <= info.rcMonitor.top + kTolerance
           && rect.right >= info.rcMonitor.right - kTolerance
           && rect.bottom >= info.rcMonitor.bottom - kTolerance;
}

void FullscreenGuard::onTick()
{
    const bool sample = foregroundIsFullscreen();
    if (sample == m_lastSample) {
        ++m_stableCount;
    } else {
        m_lastSample = sample;
        m_stableCount = 1;
    }
    if (m_stableCount >= kStableSamples && sample != m_active) {
        setActive(sample);
    }
}

void FullscreenGuard::setActive(bool active)
{
    m_active = active;
    emit fullscreenChanged(active);
}
