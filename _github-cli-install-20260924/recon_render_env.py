# -*- coding: utf-8 -*-
"""6.2-1 渲染路径基准测量——环境摸底。
采集：显示器数量/分辨率/DPI、GPU 适配器名、VLC 安装位置与版本、
build 内 libvlc.dll、磁盘剩余空间。只读，不落配置。
"""
import ctypes, ctypes.wintypes as w, glob, os, shutil, sys

u32 = ctypes.windll.user32
k32 = ctypes.windll.kernel32

# ---------- 显示器 ----------
class RECT(ctypes.Structure):
    _fields_ = [("l", ctypes.c_long), ("t", ctypes.c_long),
                ("r", ctypes.c_long), ("b", ctypes.c_long)]

MONITORENUMPROC = ctypes.WINFUNCTYPE(
    ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p, ctypes.POINTER(RECT), ctypes.c_long)

monitors = []
def _cb(hmon, hdc, rect, data):
    monitors.append(rect[0])
    return 1
u32.EnumDisplayMonitors(None, None, MONITORENUMPROC(_cb), None)

# 每屏 DPI（Win10 1607+）
try:
    u32.GetDpiForWindow.restype = ctypes.c_uint
    shcore = ctypes.windll.shcore
except Exception:
    pass

print("== 显示器 ==")
virt_x = u32.GetSystemMetrics(76)  # SM_XVIRTUALSCREEN
virt_y = u32.GetSystemMetrics(77)
virt_w = u32.GetSystemMetrics(78)
virt_h = u32.GetSystemMetrics(79)
print(f"虚拟桌面: ({virt_x},{virt_y}) {virt_w}x{virt_h}, 显示器数: {len(monitors)}")
for i, r in enumerate(monitors):
    print(f"  屏{i}: ({r.l},{r.t})-({r.r},{r.b})  {r.r - r.l}x{r.b - r.t}")

# 主屏 DPI
dpi = u32.GetDpiForSystem() if hasattr(u32, "GetDpiForSystem") else 96
print(f"系统 DPI: {dpi} (缩放 {dpi * 100 // 96}%)")

# ---------- GPU 适配器 ----------
print("== GPU ==")
class DISPLAY_DEVICEA(ctypes.Structure):
    _fields_ = [("cb", w.DWORD), ("DeviceName", ctypes.c_char * 32),
                ("DeviceString", ctypes.c_char * 128), ("StateFlags", w.DWORD),
                ("DeviceID", ctypes.c_char * 128), ("DeviceKey", ctypes.c_char * 128)]
i = 0
while True:
    dd = DISPLAY_DEVICEA(); dd.cb = ctypes.sizeof(dd)
    if not u32.EnumDisplayDevicesA(None, i, ctypes.byref(dd), 0):
        break
    if dd.StateFlags & 0x1:  # attached
        print(f"  显卡: {dd.DeviceString.decode('mbcs')}")
    i += 1
    if i > 8:
        break

# ---------- VLC ----------
print("== VLC ==")
cands = [r"C:\Program Files\VideoLAN\VLC", r"C:\Program Files (x86)\VideoLAN\VLC"]
for c in cands:
    if os.path.isdir(c):
        print(f"  安装目录: {c}")
        for dll in glob.glob(os.path.join(c, "libvlc.dll")):
            print(f"  libvlc.dll: {dll}")

repo = r"C:\Users\Lenovo\WorkBuddy\2026-09-07-15-05-15\WallpaperDesk"
for dll in glob.glob(os.path.join(repo, "build", "libvlc.dll")) + \
          glob.glob(os.path.join(repo, "build", "plugins", "libvlc.dll")):
    print(f"  build 内: {dll}")

# VLC 版本（从安装目录的 exe 读版本信息太麻烦，直接查 libvlc 版本函数需要加载；
# 先用文件大小/日期粗判，精确版本在测量工具里用 libvlc_get_version() 打）

# ---------- 磁盘 ----------
print("== 磁盘 ==")
for p in ("C:", "D:"):
    try:
        t, u, f = shutil.disk_usage(p)
        print(f"  {p} 总 {t/2**30:.0f}G 剩 {f/2**30:.0f}G")
    except Exception as e:
        print(f"  {p} 读取失败: {e}")

# ---------- 测试素材 ----------
print("== 素材 ==")
td = os.path.join(repo, "testdata")
if os.path.isdir(td):
    for f in sorted(os.listdir(td)):
        fp = os.path.join(td, f)
        print(f"  {f}  {os.path.getsize(fp)/2**20:.1f} MB")
else:
    print("  (无 testdata 目录)")
