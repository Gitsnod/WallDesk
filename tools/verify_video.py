"""WallDesk 视频壁纸端到端验证。

判定方法：临时最小化所有前台窗口 -> 连拍两帧 -> 比对差异。
- 画面在动（差异占比高）=> 视频可见且在播放
- 两帧几乎相同且偏暗 => 黑屏/未渲染

用法: python verify_video.py [视频路径]
"""
import subprocess
import sys
import time

import ctypes
import ctypes.wintypes as wt

user32 = ctypes.WinDLL("user32")
gdi32 = ctypes.WinDLL("gdi32")
try:
    user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
except Exception:
    pass

EXE = r"D:\Qt\WallpaperDesk\build\WallDesk.exe"
VIDEO = sys.argv[1] if len(sys.argv) > 1 else r"C:\Users\Lenovo\Desktop\71122-537102350.mp4"
W, H = 480, 270


def clsname(h):
    b = ctypes.create_unicode_buffer(64)
    user32.GetClassNameW(h, b, 64)
    return b.value


def grab():
    """抓全屏缩略图，返回 RGB 字节串。"""
    hdc = user32.GetDC(0)
    mem = gdi32.CreateCompatibleDC(hdc)
    bmp = gdi32.CreateCompatibleBitmap(hdc, 1920, 1080)
    gdi32.SelectObject(mem, bmp)
    gdi32.BitBlt(mem, 0, 0, 1920, 1080, hdc, 0, 0, 0x00CC0020)
    small = gdi32.CreateCompatibleDC(hdc)
    sbmp = gdi32.CreateCompatibleBitmap(hdc, W, H)
    gdi32.SelectObject(small, sbmp)
    gdi32.SetStretchBltMode(small, 4)
    gdi32.StretchBlt(small, 0, 0, W, H, mem, 0, 0, 1920, 1080, 0x00CC0020)

    class BIH(ctypes.Structure):
        _fields_ = [("biSize", wt.DWORD), ("biWidth", ctypes.c_int), ("biHeight", ctypes.c_int),
                    ("biPlanes", wt.WORD), ("biBitCount", wt.WORD), ("biCompression", wt.DWORD),
                    ("biSizeImage", wt.DWORD), ("biXPelsPerMeter", ctypes.c_long),
                    ("biYPelsPerMeter", ctypes.c_long), ("biClrUsed", wt.DWORD),
                    ("biClrImportant", wt.DWORD)]

    bi = BIH()
    bi.biSize = ctypes.sizeof(BIH)
    bi.biWidth, bi.biHeight = W, -H
    bi.biPlanes, bi.biBitCount, bi.biCompression = 1, 32, 0
    bi.biSizeImage = W * H * 4
    buf = (ctypes.c_char * (W * H * 4))()
    gdi32.GetDIBits(small, sbmp, 0, H, buf, ctypes.byref(bi), 0)
    raw = bytes(buf)
    out = bytearray()
    for y in range(H):
        line = bytearray(raw[y * W * 4:(y + 1) * W * 4])
        del line[3::4]                              # BGRA -> BGR
        line[0::3], line[2::3] = line[2::3], line[0::3]  # BGR -> RGB
        out += line
    gdi32.DeleteObject(sbmp)
    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(small)
    gdi32.DeleteDC(mem)
    user32.ReleaseDC(0, hdc)
    return bytes(out)


def save_png(rgb, path):
    import struct
    import zlib
    rows = bytearray()
    for y in range(H):
        rows.append(0)
        rows += rgb[y * W * 3:(y + 1) * W * 3]

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data)))

    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">II", W, H) + bytes((8, 2, 0, 0, 0)))
           + chunk(b"IDAT", zlib.compress(bytes(rows), 6))
           + chunk(b"IEND", b""))
    open(path, "wb").write(png)


def diff_ratio(a, b, threshold=18):
    """两帧差异像素占比（%）。"""
    changed = total = 0
    for i in range(0, len(a), 3):
        d = abs(a[i] - b[i]) + abs(a[i + 1] - b[i + 1]) + abs(a[i + 2] - b[i + 2])
        if d > threshold:
            changed += 1
        total += 1
    return round(100.0 * changed / max(total, 1), 2)


def avg_lum(rgb):
    total = sum(rgb[0::3]) + sum(rgb[1::3]) + sum(rgb[2::3])
    return round(total / max(len(rgb), 1), 1)


def find_desktop_host():
    """定位 WallDesk 的宿主窗口。"""
    progman = user32.FindWindowW("Progman", None)
    dv = user32.FindWindowExW(progman, 0, "SHELLDLL_DefView", None)
    found = []

    if dv:
        @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
        def cb(child, lp):
            if clsname(child) == "WallDeskHostWnd":
                r = wt.RECT()
                user32.GetWindowRect(child, ctypes.byref(r))
                found.append((hex(child), (r.left, r.top, r.right - r.left, r.bottom - r.top),
                              "可见" if user32.IsWindowVisible(child) else "隐藏"))
            return True
        user32.EnumChildWindows(dv, cb, 0)
    return found


print(f"视频源文件: {VIDEO}")

# 1) 确保干净启动
proc = subprocess.Popen([EXE, "--minimized"])
time.sleep(6)

# 2) 触发视频播放
subprocess.run([EXE, "--next"], capture_output=True)
print("已发送 --next 指令，等待起播…")
time.sleep(9)

# 3) 定位宿主窗口
hosts = find_desktop_host()
print(f"DefView 下的宿主窗口: {hosts if hosts else '未找到'}")

# 4) 临时最小化前台窗口
shell_keep = ("Progman", "WorkerW", "Shell_TrayWnd", "Shell_SecondaryTrayWnd")
minimized = []


@ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
def enum_top(h, lp):
    if not user32.IsWindowVisible(h) or clsname(h) in shell_keep:
        return True
    b = ctypes.create_unicode_buffer(256)
    if not user32.GetWindowTextW(h, b, 256):
        return True
    if user32.IsIconic(h):
        return True
    user32.ShowWindow(h, 6)  # SW_MINIMIZE
    minimized.append(h)
    return True


user32.EnumWindows(enum_top, 0)
time.sleep(1.5)
print(f"已临时最小化 {len(minimized)} 个窗口")

# 5) 连拍两帧
f1 = grab()
time.sleep(1.2)
f2 = grab()
time.sleep(1.2)
f3 = grab()

save_png(f1, r"C:\Users\Lenovo\AppData\Local\Temp\vframe1.png")
save_png(f3, r"C:\Users\Lenovo\AppData\Local\Temp\vframe2.png")

d12 = diff_ratio(f1, f2)
d13 = diff_ratio(f1, f3)
lum = avg_lum(f1)

# 进程存活核查
import subprocess as sp
tl = sp.run(["tasklist"], capture_output=True).stdout.decode("gbk", errors="ignore")
alive = "WallDesk.exe" in tl

print("\n=== 判定 ===")
print(f"进程存活        : {'是' if alive else '否（已被杀）'}")
print(f"平均亮度        : {lum}")
print(f"帧1→帧2 差异占比: {d12}%")
print(f"帧1→帧3 差异占比: {d13}%")

if max(d12, d13) > 8:
    print("✅ 视频可见且正在播放（画面在变化）")
elif max(d12, d13) > 1.5:
    print("⚠️ 画面有轻微变化，可能在播放但幅度小/卡顿")
else:
    print("❌ 画面静止 —— 未渲染或黑屏")

# 6) 恢复窗口
for h in minimized:
    user32.ShowWindow(h, 9)
print(f"\n已恢复 {len(minimized)} 个窗口")

# 7) 保留运行，便于人工观察；也可 --quit
print("WallDesk 仍在运行，可人工查看桌面效果。")
