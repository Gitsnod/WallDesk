#!/usr/bin/env python3
"""验证图片与视频壁纸互斥，以及循环无卡顿。"""
import ctypes
import ctypes.wintypes as wt
import os
import subprocess
import sys
import time

USER = os.environ.get('USERPROFILE', r'C:\Users\Lenovo')
BUILD_EXE = r'D:\Qt\WallpaperDesk\build\WallDesk.exe'
LOG_FILE = os.path.join(USER, r'AppData\Local\WallDesk\WallDesk\logs\WallDesk.log')

user32 = ctypes.WinDLL('user32')

def find_host():
    found = []
    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def cb(hwnd, lp):
        cls = ctypes.create_unicode_buffer(64)
        user32.GetClassNameW(hwnd, cls, 64)
        if cls.value == 'WallDeskHostWnd':
            found.append(hwnd)
        c = None
        while True:
            c = user32.FindWindowExW(hwnd, c, None, None)
            if not c:
                break
            cls2 = ctypes.create_unicode_buffer(64)
            user32.GetClassNameW(c, cls2, 64)
            if cls2.value == 'WallDeskHostWnd':
                found.append(c)
        return True
    user32.EnumWindows(cb, 0)
    return found

def tail_log(n=30):
    if not os.path.exists(LOG_FILE):
        return []
    with open(LOG_FILE, 'r', encoding='utf-8', errors='ignore') as f:
        return f.readlines()[-n:]

def send_cmd(cmd):
    subprocess.run([BUILD_EXE, f'--{cmd}'], capture_output=True)
    time.sleep(1.5)

def main():
    # 清理已有实例
    os.system(r'taskkill /F /IM WallDesk.exe 2>nul')
    time.sleep(1)

    proc = subprocess.Popen([BUILD_EXE], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(3)

    print('Step 1: apply video')
    send_cmd('next')
    time.sleep(3)
    hosts_after_video = find_host()
    print(f'  Host windows after video: {len(hosts_after_video)} ({[hex(h) for h in hosts_after_video]})')

    print('Step 2: apply image')
    send_cmd('next')
    time.sleep(2)
    hosts_after_image = find_host()
    print(f'  Host windows after image: {len(hosts_after_image)} ({[hex(h) for h in hosts_after_image]})')

    print('Step 3: check loop')
    # Let video loop a couple times if currently video is active
    time.sleep(8)

    print('\nRecent log:')
    for line in tail_log(40):
        print('  ', line.strip())

    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()

if __name__ == '__main__':
    main()
