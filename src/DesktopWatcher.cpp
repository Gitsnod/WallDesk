#include "DesktopWatcher.h"

#ifndef WM_WTSSESSION_CHANGE
#define WM_WTSSESSION_CHANGE 0x02B1
#endif
#ifndef NOTIFY_FOR_THIS_SESSION
#define NOTIFY_FOR_THIS_SESSION 0
#endif
#ifndef WTS_SESSION_LOCK
#define WTS_SESSION_LOCK 0x7
#endif
#ifndef WTS_SESSION_UNLOCK
#define WTS_SESSION_UNLOCK 0x8
#endif

DesktopWatcher::DesktopWatcher(QObject* parent)
    : QObject(parent)
{
}

DesktopWatcher::~DesktopWatcher()
{
    if (m_sessionRegistered && m_wtsDll && m_notifyWindow) {
        auto unregister =
            reinterpret_cast<PfnWtsUnregister>(GetProcAddress(m_wtsDll, "WTSUnRegisterSessionNotification"));
        if (unregister) {
            unregister(m_notifyWindow);
        }
    }
    if (m_wtsDll) {
        FreeLibrary(m_wtsDll);
        m_wtsDll = nullptr;
    }
}

void DesktopWatcher::setNotificationWindow(HWND hwnd)
{
    m_notifyWindow = hwnd;
    if (!hwnd || m_sessionRegistered) {
        return;
    }

    if (!m_wtsDll) {
        m_wtsDll = LoadLibraryW(L"wtsapi32.dll");
    }
    if (!m_wtsDll) {
        return;
    }

    auto registerFn =
        reinterpret_cast<PfnWtsRegister>(GetProcAddress(m_wtsDll, "WTSRegisterSessionNotification"));
    if (registerFn && registerFn(hwnd, NOTIFY_FOR_THIS_SESSION)) {
        m_sessionRegistered = true;
    }
}

bool DesktopWatcher::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result)
{
    Q_UNUSED(result);
    if (eventType != QByteArrayLiteral("windows_generic_MSG") &&
        eventType != QByteArrayLiteral("windows_dispatcher_MSG")) {
        return false;
    }

    const MSG* msg = static_cast<const MSG*>(message);
    if (!msg) {
        return false;
    }

    switch (msg->message) {
    case WM_POWERBROADCAST:
        switch (msg->wParam) {
        case PBT_APMSUSPEND:
            emit systemSuspending();
            return true;
        case PBT_APMRESUMEAUTOMATIC:
        case PBT_APMRESUMESUSPEND:
            emit systemResumed();
            return true;
        case PBT_APMPOWERSTATUSCHANGE:
            emit powerStatusChanged();
            return true;
        default:
            break;
        }
        break;

    case WM_WTSSESSION_CHANGE:
        if (msg->wParam == WTS_SESSION_LOCK) {
            emit sessionLocked(true);
            return true;
        }
        if (msg->wParam == WTS_SESSION_UNLOCK) {
            emit sessionLocked(false);
            return true;
        }
        break;

    case WM_DISPLAYCHANGE:
    case WM_SETTINGCHANGE:
    case WM_DEVICECHANGE:
        emit displayChanged();
        return true;

    default:
        break;
    }

    return false;
}
