#pragma once

#include <QAbstractNativeEventFilter>
#include <QByteArray>
#include <QObject>
#include <windows.h>

/**
 * 系统事件监听：休眠唤醒、会话锁定解锁、显示器/分辨率变化、电源状态变化。
 *
 * 以 Qt 原生事件过滤器实现，因此不需要自建隐藏窗口与消息循环。
 * 会话通知（WM_WTSSESSION_CHANGE）需要先对某个窗口句柄注册，由 setNotificationWindow 完成；
 * wtsapi32.dll 采用运行时加载，避免引入额外的导入库依赖。
 */
class DesktopWatcher : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    explicit DesktopWatcher(QObject* parent = nullptr);
    ~DesktopWatcher() override;

    /** 注册会话通知的窗口句柄，通常传主窗口的 HWND。 */
    void setNotificationWindow(HWND hwnd);

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

signals:
    void systemSuspending();   // 即将进入休眠/睡眠
    void systemResumed();      // 已从休眠恢复
    void sessionLocked(bool locked); // 锁屏 / 解锁
    void displayChanged();     // 显示器插拔或分辨率变化
    void powerStatusChanged(); // 交流 / 电池供电切换

private:
    typedef BOOL(WINAPI* PfnWtsRegister)(HWND, DWORD);
    typedef BOOL(WINAPI* PfnWtsUnregister)(HWND);

    HWND m_notifyWindow = nullptr;
    HMODULE m_wtsDll = nullptr;
    bool m_sessionRegistered = false;
};
