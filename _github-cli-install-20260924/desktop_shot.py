"""空闲暂停实测脚本（三阶段，全程不注入键鼠输入，直到恢复阶段）。

阶段 A：最小化 WallDesk 主窗口 → 截桌面 A/B（间隔 4s）→ 期望不同（视频在动）
阶段 B：静默 65 秒（严禁动鼠标/键盘）→ 截 C/D（间隔 4s）→ 期望相同（已冻结）
阶段 C：SetCursorPos 注入一次输入 → 1 秒后截 E/F（间隔 4s）→ 期望不同（已续播）

用法：desktop_shot.py <模式>
  shot <输出.png>      被动截取主屏（BitBlt，不注入输入）
  md5  <a.png> <b.png> 比较两张图的 md5
  minimize            最小化 WallDesk 主窗口（SetForeground 都不用）
  input               注入一次合成鼠标移动（仅阶段 C 调用）
"""
import ctypes
import hashlib
import struct
import sys
import time
import zlib
from ctypes import wintypes

try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)
except Exception:
    try:
        ctypes.windll.user32.SetProcessDPIAware()
    except Exception:
        pass

user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32

SRCCOPY = 0x00CC0020
SW_MINIMIZE = 6


def grab_primary():
    """截取主屏，去掉底部 60px（任务栏时钟秒位在跳，会污染整图 md5）。"""
    w = user32.GetSystemMetrics(0)  # SM_CXSCREEN
    h = user32.GetSystemMetrics(1) - 60  # SM_CYSCREEN - 任务栏
    hdc_screen = user32.GetDC(0)
    hdc_mem = gdi32.CreateCompatibleDC(hdc_screen)
    hbm = gdi32.CreateCompatibleBitmap(hdc_screen, w, h)
    gdi32.SelectObject(hdc_mem, hbm)
    gdi32.BitBlt(hdc_mem, 0, 0, w, h, hdc_screen, 0, 0, SRCCOPY)

    class BI(ctypes.Structure):
        _fields_ = [("biSize", wintypes.DWORD), ("biWidth", ctypes.c_long),
                    ("biHeight", ctypes.c_long), ("biPlanes", wintypes.WORD),
                    ("biBitCount", wintypes.WORD), ("biCompression", wintypes.DWORD),
                    ("biSizeImage", wintypes.DWORD),
                    ("biXPelsPerMeter", ctypes.c_long),
                    ("biYPelsPerMeter", ctypes.c_long),
                    ("biClrUsed", wintypes.DWORD),
                    ("biClrImportant", wintypes.DWORD)]

    bi = BI()
    bi.biSize = ctypes.sizeof(BI)
    bi.biWidth = w
    bi.biHeight = -h
    bi.biPlanes = 1
    bi.biBitCount = 32
    buf = ctypes.create_string_buffer(w * h * 4)
    gdi32.GetDIBits(hdc_mem, hbm, 0, h, buf, ctypes.byref(bi), 0)
    gdi32.DeleteObject(hbm)
    gdi32.DeleteDC(hdc_mem)
    user32.ReleaseDC(0, hdc_screen)
    return w, h, buf.raw


def write_png(path, w, h, bgra):
    rgb = bytearray(w * h * 3)
    for i in range(w * h):
        s = i * 4
        d = i * 3
        rgb[d] = bgra[s + 2]
        rgb[d + 1] = bgra[s + 1]
        rgb[d + 2] = bgra[s]
    raw = bytearray()
    stride = w * 3
    for y in range(h):
        raw.append(0)
        raw += rgb[y * stride:(y + 1) * stride]

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 6))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)
    return hashlib.md5(png).hexdigest()


def find_walldesk():
    """按标题找主窗口，返回 hwnd（找不到返回 0）。"""
    found = []

    def cb(hwnd, _):
        if not user32.IsWindowVisible(hwnd):
            return True
        n = user32.GetWindowTextLengthW(hwnd)
        buf = ctypes.create_unicode_buffer(n + 1)
        user32.GetWindowTextW(hwnd, buf, n + 1)
        if "WallDesk" in buf.value:
            found.append(hwnd)
        return True

    WNDENUMPROC = ctypes.WINFUNCTYPE(ctypes.c_bool, wintypes.HWND, wintypes.LPARAM)
    user32.EnumWindows(WNDENUMPROC(cb), 0)
    return found[0] if found else 0


def main():
    mode = sys.argv[1]
    if mode == "shot":
        w, h, bgra = grab_primary()
        md5 = write_png(sys.argv[2], w, h, bgra)
        print(f"shot {w}x{h} md5={md5}")
    elif mode == "md5":
        a = hashlib.md5(open(sys.argv[2], "rb").read()).hexdigest()
        b = hashlib.md5(open(sys.argv[3], "rb").read()).hexdigest()
        print(f"md5 {a} vs {b} -> {'相同' if a == b else '不同'}")
    elif mode == "minimize":
        hwnd = find_walldesk()
        if not hwnd:
            print("未找到 WallDesk 窗口")
            return 1
        user32.ShowWindow(hwnd, SW_MINIMIZE)
        print(f"已最小化 hwnd={hwnd}")
        time.sleep(0.5)
    elif mode == "restore":
        hwnd = find_walldesk()
        if not hwnd:
            print("未找到 WallDesk 窗口")
            return 1
        user32.ShowWindow(hwnd, 9)  # SW_RESTORE，不抢焦点、不注入输入
        print(f"已恢复 hwnd={hwnd}")
        time.sleep(1.0)
    elif mode == "pwshot":
        # PrintWindow 抓应用窗口本身（Qt 控件是 GDI 可绘的，能抓到）；
        # 只恢复不抢焦点，避免注入输入把空闲暂停取消掉。
        hwnd = find_walldesk()
        if not hwnd:
            print("未找到 WallDesk 窗口")
            return 1
        r = wintypes.RECT()
        user32.GetWindowRect(hwnd, ctypes.byref(r))
        w, h = r.right - r.left, r.bottom - r.top
        hdc_screen = user32.GetDC(0)
        hdc_mem = gdi32.CreateCompatibleDC(hdc_screen)
        hbm = gdi32.CreateCompatibleBitmap(hdc_screen, w, h)
        gdi32.SelectObject(hdc_mem, hbm)
        user32.PrintWindow(hwnd, hdc_mem, 2)  # PW_RENDERFULLCONTENT

        class BI2(ctypes.Structure):
            _fields_ = [("biSize", wintypes.DWORD), ("biWidth", ctypes.c_long),
                        ("biHeight", ctypes.c_long), ("biPlanes", wintypes.WORD),
                        ("biBitCount", wintypes.WORD), ("biCompression", wintypes.DWORD),
                        ("biSizeImage", wintypes.DWORD),
                        ("biXPelsPerMeter", ctypes.c_long),
                        ("biYPelsPerMeter", ctypes.c_long),
                        ("biClrUsed", wintypes.DWORD),
                        ("biClrImportant", wintypes.DWORD)]

        bi = BI2()
        bi.biSize = ctypes.sizeof(BI2)
        bi.biWidth = w
        bi.biHeight = -h
        bi.biPlanes = 1
        bi.biBitCount = 32
        buf = ctypes.create_string_buffer(w * h * 4)
        gdi32.GetDIBits(hdc_mem, hbm, 0, h, buf, ctypes.byref(bi), 0)
        gdi32.DeleteObject(hbm)
        gdi32.DeleteDC(hdc_mem)
        user32.ReleaseDC(0, hdc_screen)
        md5 = write_png(sys.argv[2], w, h, buf.raw)
        print(f"pwshot {w}x{h} md5={md5}")
    elif mode == "input":
        # 合成输入：SendInput 相对移动 1 像素。SetCursorPos 不产生输入事件、
        # 不更新 GetLastInputInfo 的 dwTime（实测踩过），必须走 SendInput。
        class MOUSEINPUT(ctypes.Structure):
            _fields_ = [("dx", wintypes.LONG), ("dy", wintypes.LONG),
                        ("mouseData", wintypes.DWORD), ("dwFlags", wintypes.DWORD),
                        ("time", wintypes.DWORD),
                        ("dwExtraInfo", ctypes.POINTER(ctypes.c_ulong))]

        class INPUT(ctypes.Structure):
            _fields_ = [("type", wintypes.DWORD), ("mi", MOUSEINPUT)]

        inp = INPUT()
        inp.type = 0  # INPUT_MOUSE
        inp.mi.dx = 1
        inp.mi.dy = 0
        inp.mi.dwFlags = 0x0001  # MOUSEEVENTF_MOVE
        user32.SendInput(1, ctypes.byref(inp), ctypes.sizeof(INPUT))
        time.sleep(0.05)
        inp.mi.dx = -1
        user32.SendInput(1, ctypes.byref(inp), ctypes.sizeof(INPUT))
        print("已注入输入：SendInput 相对移动 ±1px")
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
