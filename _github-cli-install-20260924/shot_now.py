"""抓当前正在运行的 WallDesk 主窗口（不重启实例）。

用法：
  python shot_now.py <输出.png> [x y w h]     # 不给区域则抓整窗

说明：走屏幕 DC BitBlt（PrintWindow 对 Qt 返回旧帧，见项目记忆），
先 SetForegroundWindow 把窗口提到前台，再截。
"""
import ctypes
import struct
import sys
import time
import zlib
from ctypes import wintypes

HERE = r"C:\Users\Lenovo\WorkBuddy\2026-09-07-15-05-15\WallpaperDesk\_github-cli-install-20260924"
sys.path.insert(0, HERE)
import verify_motion as V  # noqa: E402

user32 = ctypes.windll.user32


def find_window():
    hits = []

    @ctypes.WINFUNCTYPE(ctypes.c_bool, wintypes.HWND, wintypes.LPARAM)
    def cb(hwnd, _):
        if not user32.IsWindowVisible(hwnd):
            return True
        n = user32.GetWindowTextLengthW(hwnd)
        if n == 0:
            return True
        buf = ctypes.create_unicode_buffer(n + 1)
        user32.GetWindowTextW(hwnd, buf, n + 1)
        title = buf.value
        cls = ctypes.create_unicode_buffer(256)
        user32.GetClassNameW(hwnd, cls, 256)
        if "WallDesk" in title or "WallDesk" in cls.value:
            hits.append((hwnd, title, cls.value))
        return True

    user32.EnumWindows(cb, 0)
    # 主窗口 = 面积最大的那个
    best = None
    best_area = 0
    for hwnd, title, cls in hits:
        r = wintypes.RECT()
        user32.GetWindowRect(hwnd, ctypes.byref(r))
        area = (r.right - r.left) * (r.bottom - r.top)
        if area > best_area:
            best_area = area
            best = (hwnd, title, cls, r.right - r.left, r.bottom - r.top)
    return best


def write_png(path, w, h, raw_rows):
    def chunk(tag, data):
        out = struct.pack(">I", len(data)) + tag + data
        return out + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr)
    png += chunk(b"IDAT", zlib.compress(raw_rows, 6)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def main():
    out = sys.argv[1]
    hit = find_window()
    if not hit:
        print("没找到 WallDesk 窗口")
        return 1
    hwnd, title, cls, ww, wh = hit
    print(f"窗口 0x{hwnd:X} «{title}» {ww}x{wh}")

    # 提到前台：先还原，再 SetForegroundWindow（Qt 窗口对后者通常接受）
    user32.ShowWindow(hwnd, 9)  # SW_RESTORE
    user32.SetForegroundWindow(hwnd)
    time.sleep(0.8)

    if len(sys.argv) >= 6:
        x, y, w, h = (int(v) for v in sys.argv[2:6])
    else:
        x, y, w, h = 0, 0, ww, wh
    gw, gh, rows = V.grab(hwnd, x, y, w, h)
    write_png(out, gw, gh, rows)
    print(f"已保存 {out}（{gw}x{gh}）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
