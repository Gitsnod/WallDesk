# -*- coding: utf-8 -*-
"""6.2-1 渲染路径基准测量主工具。

用法（每个模式独立进程运行，避免相互污染）：
    python bench_render_path.py --mode idle      --duration 20 --out r_idle.json
    python bench_render_path.py --mode hwnd      --duration 60 --warmup 5 --out r_hwnd.json
    python bench_render_path.py --mode callbacks --duration 60 --warmup 5 --out r_cb.json

模式：
    idle      只创建全屏宿主窗口（模拟壁纸层），不播放——静态基线
    hwnd      libvlc_media_player_set_hwnd 窗口渲染（= WallDesk 当前路径，VLC 自选 vout）
    callbacks libvlc_video_set_callbacks 裸帧回调 + SetDIBitsToDevice 自渲染（报告中的「视频回调」路径）

测量指标（每秒一次）：
    进程 CPU%（GetProcessTimes，可超 100%=多线程）、工作集（GetProcessMemoryInfo）
    GPU 3D 引擎利用率：本进程 / dwm.exe / 全系统（PDH 英文计数器，绕开本机中文区域化）
    VLC stats：显示帧数、丢帧数、读取字节（libvlc_media_get_stats，带运行时校验）
另起子进程 bench_capture_window.py 以 10Hz 抓窗口画面算合成器可见帧率。

VLC 实例参数与 WallDesk Auto 档一致（src/VlcPlayer.cpp instanceArguments），
差异仅：去 --quiet（保留日志）、去 --no-stats（需要 stats）、加 --verbose=2。
"""
import argparse
import ctypes
import ctypes.wintypes as w
import json
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

u32 = ctypes.windll.user32
k32 = ctypes.windll.kernel32
g32 = ctypes.windll.gdi32
pdh = ctypes.windll.pdh
psapi = ctypes.windll.psapi

# ---------------------------------------------------------------- win32 基座

LRESULT = ctypes.c_long
WNDPROC = ctypes.WINFUNCTYPE(LRESULT, w.HWND, ctypes.c_uint, w.WPARAM, ctypes.c_long)


class WNDCLASSEXW(ctypes.Structure):
    _fields_ = [("cbSize", w.UINT), ("style", w.UINT), ("lpfnWndProc", WNDPROC),
                ("cbClsExtra", ctypes.c_int), ("cbWndExtra", ctypes.c_int),
                ("hInstance", w.HINSTANCE), ("hIcon", w.HANDLE), ("hCursor", w.HANDLE),
                ("hbrBackground", w.HANDLE), ("lpszMenuName", w.LPWSTR),
                ("lpszClassName", w.LPWSTR), ("hIconSm", w.HANDLE)]


class MSG(ctypes.Structure):
    _fields_ = [("hwnd", w.HWND), ("message", ctypes.c_uint),
                ("wParam", w.WPARAM), ("lParam", ctypes.c_long),
                ("time", w.DWORD), ("pt", ctypes.c_long * 2)]


class RECT(ctypes.Structure):
    _fields_ = [("left", ctypes.c_long), ("top", ctypes.c_long),
                ("right", ctypes.c_long), ("bottom", ctypes.c_long)]


class PAINTSTRUCT(ctypes.Structure):
    _fields_ = [("hdc", w.HDC), ("fErase", w.BOOL), ("rcPaint", RECT),
                ("fRestore", w.BOOL), ("fIncUpdate", w.BOOL), ("rgbReserved", ctypes.c_byte * 32)]


class FILETIME(ctypes.Structure):
    _fields_ = [("dwLowDateTime", w.DWORD), ("dwHighDateTime", w.DWORD)]


class PROCESS_MEMORY_COUNTERS(ctypes.Structure):
    _fields_ = [("cb", w.DWORD), ("PageFaultCount", w.DWORD),
                ("PeakWorkingSetSize", ctypes.c_size_t), ("WorkingSetSize", ctypes.c_size_t),
                ("QuotaPeakPagedPoolUsage", ctypes.c_size_t), ("QuotaPagedPoolUsage", ctypes.c_size_t),
                ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t), ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
                ("PagefileUsage", ctypes.c_size_t), ("PeakPagefileUsage", ctypes.c_size_t)]


# ---- Win32 签名（不设 argtypes 时指针参数会被按 32 位截断）----
k32.GetModuleHandleW.restype = w.HINSTANCE
k32.GetModuleHandleW.argtypes = [w.LPCWSTR]
u32.RegisterClassExW.restype = w.ATOM
u32.RegisterClassExW.argtypes = [ctypes.POINTER(WNDCLASSEXW)]
u32.CreateWindowExW.restype = w.HWND
u32.CreateWindowExW.argtypes = [w.DWORD, w.LPCWSTR, w.LPCWSTR, w.DWORD,
                                ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                                w.HWND, w.HMENU, w.HINSTANCE, ctypes.c_void_p]
u32.ShowWindow.argtypes = [w.HWND, ctypes.c_int]
u32.UpdateWindow.argtypes = [w.HWND]
u32.DefWindowProcW.restype = LRESULT
u32.DefWindowProcW.argtypes = [w.HWND, ctypes.c_uint, w.WPARAM, ctypes.c_long]
u32.PeekMessageW.restype = w.BOOL
u32.PeekMessageW.argtypes = [ctypes.POINTER(MSG), w.HWND, ctypes.c_uint, ctypes.c_uint, ctypes.c_uint]
u32.TranslateMessage.argtypes = [ctypes.POINTER(MSG)]
u32.DispatchMessageW.argtypes = [ctypes.POINTER(MSG)]
u32.GetDC.restype = w.HDC
u32.GetDC.argtypes = [w.HWND]
u32.ReleaseDC.restype = ctypes.c_int
u32.ReleaseDC.argtypes = [w.HWND, w.HDC]
u32.SetProcessDpiAwarenessContext.restype = w.BOOL
u32.SetProcessDpiAwarenessContext.argtypes = [ctypes.c_void_p]
u32.GetSystemMetrics.restype = ctypes.c_int
u32.BeginPaint.restype = w.HDC
u32.BeginPaint.argtypes = [w.HWND, ctypes.POINTER(PAINTSTRUCT)]
u32.EndPaint.restype = w.BOOL
u32.EndPaint.argtypes = [w.HWND, ctypes.POINTER(PAINTSTRUCT)]
g32.SetDIBitsToDevice.restype = ctypes.c_int
g32.SetDIBitsToDevice.argtypes = [w.HDC] + [ctypes.c_int] * 9 + [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint]
# 进程指标签名：不设 restype 时 GetCurrentProcess 的 64 位句柄会被截断成 32 位，
# 再把截断值无签名传给 GetProcessTimes，寄存器高 32 位是垃圾 → 内核解引假句柄硬崩
k32.GetCurrentProcess.restype = w.HANDLE
k32.GetProcessTimes.restype = w.BOOL
k32.GetProcessTimes.argtypes = [w.HANDLE] + [ctypes.POINTER(FILETIME)] * 4
psapi.GetProcessMemoryInfo.restype = w.BOOL
psapi.GetProcessMemoryInfo.argtypes = [w.HANDLE, ctypes.POINTER(PROCESS_MEMORY_COUNTERS), w.DWORD]
k32.CreateToolhelp32Snapshot.restype = w.HANDLE
k32.CreateToolhelp32Snapshot.argtypes = [w.DWORD, w.DWORD]
k32.CloseHandle.restype = w.BOOL
k32.CloseHandle.argtypes = [w.HANDLE]


def pump():
    """排空并派发本线程消息。单次设上限防消息洪水把循环拖死。"""
    msg = MSG()
    n = 0
    while n < 1000 and u32.PeekMessageW(ctypes.byref(msg), None, 0, 0, 1):  # PM_REMOVE
        u32.TranslateMessage(ctypes.byref(msg))
        u32.DispatchMessageW(ctypes.byref(msg))
        n += 1


_KEEP = []  # 持有 WNDCLASSEXW 与窗口过程回调：注册的窗口类里存的是裸函数指针，
# 局部变量被 GC 后 thunk 释放，窗口一收到消息（DWM 合成即触发）就跳进已释放内存，
# 表现为无任何 Python 异常的硬性进程死亡（实测）。


def create_window(width, height, title):
    hinst = k32.GetModuleHandleW(None)
    cls = "BenchRenderHost"

    # 窗口过程保持最小化：本机实测「回调里调 DefWindowProcW」会被安全层干扰
    # （WM_NCCREATE 返回 0 致创建中止、偶发 AV），故不调 DefWindowProc；
    # WM_PAINT 必须 BeginPaint/EndPaint 验证更新区域，否则 Windows 无限重投
    # WM_PAINT 形成洪水，高频 dispatch 会把进程拖死（实测）。
    @WNDPROC
    def proc(hwnd, m, wp, lp):
        if m == 0x81:  # WM_NCCREATE 必须返回 1，否则创建中止
            return 1
        if m == 0xf:   # WM_PAINT
            ps = PAINTSTRUCT()
            u32.BeginPaint(hwnd, ctypes.byref(ps))
            u32.EndPaint(hwnd, ctypes.byref(ps))
            return 0
        return 0

    wc = WNDCLASSEXW()
    wc.cbSize = ctypes.sizeof(WNDCLASSEXW)
    wc.lpfnWndProc = proc
    wc.hInstance = hinst
    wc.hbrBackground = g32.GetStockObject(4)  # BLACK_BRUSH
    wc.lpszClassName = cls
    if not u32.RegisterClassExW(ctypes.byref(wc)):
        raise RuntimeError("RegisterClassExW 失败: %d" % k32.GetLastError())
    hwnd = u32.CreateWindowExW(
        0x00000080 | 0x08000000 | 0x00000008,  # WS_EX_TOOLWINDOW|NOACTIVATE|TOPMOST
        cls, title, 0x80000000,  # WS_POPUP
        0, 0, width, height, None, None, hinst, None)
    if not hwnd:
        raise RuntimeError("CreateWindowExW 失败: %d" % k32.GetLastError())
    u32.ShowWindow(hwnd, 5)  # SW_SHOW
    u32.UpdateWindow(hwnd)
    _KEEP.append(wc)   # 防 GC（见 _KEEP 注释）
    _KEEP.append(proc)
    return hwnd


# ---------------------------------------------------------------- PDH GPU 计数

class PDH_ITEM_W(ctypes.Structure):
    """实测（Win11）：PdhGetFormattedCounterArrayW 返回的项步长为 24 字节，
    szName 在 +0。双精度值位置本机待定（+8 或 +16，视 PDH 内部布局而不同），
    两个偏移都读出来，由带 GPU 负载的实测判定用哪个。"""
    _fields_ = [("szName", w.LPWSTR), ("v8", ctypes.c_double), ("v16", ctypes.c_double)]


PDH_FMT_DOUBLE = 0x00000200


class GpuMeter:
    r"""\GPU Engine(*)\Utilization Percentage —— 按 pid 子串过滤。"""

    PATH = "\\GPU Engine(*)\\Utilization Percentage"

    def __init__(self):
        self.hq = w.HANDLE()
        self.hc = w.HANDLE()
        if pdh.PdhOpenQueryW(None, 0, ctypes.byref(self.hq)) != 0:
            raise RuntimeError("PdhOpenQueryW 失败")
        pdh.PdhAddEnglishCounterW.argtypes = [w.HANDLE, w.LPWSTR, ctypes.c_void_p, ctypes.POINTER(w.HANDLE)]
        pdh.PdhAddEnglishCounterW(self.hq, self.PATH, None, ctypes.byref(self.hc))
        pdh.PdhGetFormattedCounterArrayW.argtypes = [
            w.HANDLE, w.DWORD, ctypes.POINTER(w.DWORD), ctypes.POINTER(w.DWORD), ctypes.c_void_p]
        self.ok = True
        pdh.PdhCollectQueryData(self.hq)  # 预热一次

    def sample(self):
        """返回 (dict 实例名->(v8, v16), total_v8, total_v16)。"""
        if not self.ok:
            return {}, 0.0, 0.0
        if pdh.PdhCollectQueryData(self.hq) != 0:
            return {}, 0.0, 0.0
        size = w.DWORD(0)
        cnt = w.DWORD(0)
        pdh.PdhGetFormattedCounterArrayW(self.hc, PDH_FMT_DOUBLE,
                                         ctypes.byref(size), ctypes.byref(cnt), None)
        if size.value == 0 or cnt.value == 0 or cnt.value > 4096:
            return {}, 0.0, 0.0
        buf = ctypes.create_string_buffer(size.value)
        if pdh.PdhGetFormattedCounterArrayW(self.hc, PDH_FMT_DOUBLE,
                                            ctypes.byref(size), ctypes.byref(cnt), buf) != 0:
            return {}, 0.0, 0.0
        items = ctypes.cast(buf, ctypes.POINTER(PDH_ITEM_W))
        out = {}
        n = min(cnt.value, size.value // ctypes.sizeof(PDH_ITEM_W))
        for i in range(n):
            try:
                name = items[i].szName
                if name:
                    out[name] = (items[i].v8, items[i].v16)
            except Exception:
                continue
        t8 = sum(v[0] for v in out.values())
        t16 = sum(v[1] for v in out.values())
        return out, t8, t16

    def close(self):
        pdh.PdhCloseQuery(self.hq)


def find_pid(exe):
    """toolhelp 快照按可执行名找 pid（避免 tasklist 的 GBK 输出坑）。"""
    class PROCESSENTRY32W(ctypes.Structure):
        _fields_ = [("dwSize", w.DWORD), ("cntUsage", w.DWORD), ("th32ProcessID", w.DWORD),
                    ("th32DefaultHeapID", ctypes.c_size_t), ("th32ModuleID", w.DWORD),
                    ("cntThreads", w.DWORD), ("th32ParentProcessID", w.DWORD),
                    ("pcPriClassBase", ctypes.c_long), ("dwFlags", w.DWORD),
                    ("szExeFile", w.WCHAR * 260)]
    snap = k32.CreateToolhelp32Snapshot(0x00000002, 0)  # TH32CS_SNAPPROCESS
    if snap == w.HANDLE(-1).value:
        return None
    pe = PROCESSENTRY32W()
    pe.dwSize = ctypes.sizeof(PROCESSENTRY32W)
    found = []
    ok = ctypes.windll.kernel32.Process32FirstW(snap, ctypes.byref(pe))
    while ok:
        if pe.szExeFile.lower() == exe.lower():
            found.append(pe.th32ProcessID)
        ok = ctypes.windll.kernel32.Process32NextW(snap, ctypes.byref(pe))
    k32.CloseHandle(snap)
    return found[0] if found else None


# ---------------------------------------------------------------- 进程指标

def proc_cpu_ns(handle):
    """返回累计 CPU 时间（纳秒，kernel+user）。"""
    c, e, k, u = FILETIME(), FILETIME(), FILETIME(), FILETIME()
    k32.GetProcessTimes(handle, ctypes.byref(c), ctypes.byref(e),
                        ctypes.byref(k), ctypes.byref(u))
    return (((k.dwHighDateTime << 32) | k.dwLowDateTime) +
            ((u.dwHighDateTime << 32) | u.dwLowDateTime)) * 100


def proc_ws_bytes(handle):
    pmc = PROCESS_MEMORY_COUNTERS()
    pmc.cb = ctypes.sizeof(PROCESS_MEMORY_COUNTERS)
    psapi.GetProcessMemoryInfo(handle, ctypes.byref(pmc), pmc.cb)
    return pmc.WorkingSetSize


# ---------------------------------------------------------------- libvlc 绑定

class MediaStats(ctypes.Structure):
    """libvlc_media_stats_t（VLC 3.0，16 个 4 字节字段）。"""
    _fields_ = [("i_read_bytes", ctypes.c_int), ("f_input_bitrate", ctypes.c_float),
                ("i_demux_read_bytes", ctypes.c_int), ("f_demux_bitrate", ctypes.c_float),
                ("i_demux_corrupted", ctypes.c_int), ("i_demux_discontinuity", ctypes.c_int),
                ("i_decoded_video", ctypes.c_int), ("i_decoded_audio", ctypes.c_int),
                ("i_displayed_pictures", ctypes.c_int), ("i_lost_pictures", ctypes.c_int),
                ("i_played_abuffers", ctypes.c_int), ("i_lost_abuffers", ctypes.c_int),
                ("i_send_packet", ctypes.c_int), ("i_received_packet", ctypes.c_int),
                ("f_send_bitrate", ctypes.c_float), ("f_receive_bitrate", ctypes.c_float)]


VLC_DIR = r"C:\Program Files\VideoLAN\VLC"

vlc = ctypes.WinDLL(os.path.join(VLC_DIR, "libvlc.dll"))

vlc.libvlc_new.restype = ctypes.c_void_p
vlc.libvlc_new.argtypes = [ctypes.c_int, ctypes.POINTER(ctypes.c_char_p)]
vlc.libvlc_release.argtypes = [ctypes.c_void_p]
vlc.libvlc_get_version.restype = ctypes.c_char_p
vlc.libvlc_media_new_path.restype = ctypes.c_void_p
vlc.libvlc_media_new_path.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
vlc.libvlc_media_release.argtypes = [ctypes.c_void_p]
vlc.libvlc_media_player_new_from_media.restype = ctypes.c_void_p
vlc.libvlc_media_player_new_from_media.argtypes = [ctypes.c_void_p]
vlc.libvlc_media_player_release.argtypes = [ctypes.c_void_p]
vlc.libvlc_media_player_set_hwnd.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
vlc.libvlc_media_player_play.restype = ctypes.c_int
vlc.libvlc_media_player_play.argtypes = [ctypes.c_void_p]
vlc.libvlc_media_player_stop.argtypes = [ctypes.c_void_p]
vlc.libvlc_media_player_is_playing.restype = ctypes.c_int
vlc.libvlc_media_player_is_playing.argtypes = [ctypes.c_void_p]
vlc.libvlc_media_get_stats.restype = ctypes.c_int
vlc.libvlc_media_get_stats.argtypes = [ctypes.c_void_p, ctypes.POINTER(MediaStats)]

LOCK_CB = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p))
UNLOCK_CB = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p))
DISPLAY_CB = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_void_p)
SETUP_CB = ctypes.CFUNCTYPE(ctypes.c_uint, ctypes.POINTER(ctypes.c_void_p), ctypes.c_void_p,
                            ctypes.POINTER(ctypes.c_uint), ctypes.POINTER(ctypes.c_uint),
                            ctypes.POINTER(ctypes.c_uint), ctypes.POINTER(ctypes.c_uint))
CLEANUP_CB = ctypes.CFUNCTYPE(None, ctypes.c_void_p)
vlc.libvlc_video_set_callbacks.argtypes = [ctypes.c_void_p, LOCK_CB, UNLOCK_CB, DISPLAY_CB, ctypes.c_void_p]
vlc.libvlc_video_set_format_callbacks.argtypes = [ctypes.c_void_p, SETUP_CB, CLEANUP_CB]


def vlc_args(plugin_dir):
    """与 WallDesk Auto 档一致（src/VlcPlayer.cpp），差异见模块 docstring。"""
    args = ["--no-video-title-show", "--no-video-deco", "--no-osd",
            "--aout=mmdevice", "--no-sub-autodetect-file",
            "--no-metadata-network-access", "--file-caching=300",
            "--network-caching=300", "--clock-jitter=0",
            "--drop-late-frames", "--skip-frames",
            "--verbose=2", "--plugin-path=" + plugin_dir]
    return args


# ---------------------------------------------------------------- 自渲染回调（callbacks 模式）

_keep = []  # 防回调对象被 GC


class SelfRender:
    def __init__(self, hwnd):
        self.hwnd = hwnd
        self.frames = 0
        self.w = 0
        self.h = 0
        self.pitch = 0
        self.buf = None
        self.hdc = u32.GetDC(hwnd)
        self.t_first = None
        self.t_last = None

        def setup(opaque, chroma, width, height, pitches, lines):
            ctypes.memmove(chroma, b"RV32", 4)  # 32bpp；通道序与 Windows DIB 相反，仅影响颜色不影响成本
            self.w, self.h = width[0], height[0]
            self.pitch = self.w * 4
            pitches[0] = self.pitch
            lines[0] = self.h
            self.buf = ctypes.create_string_buffer(self.pitch * self.h)
            opaque[0] = ctypes.cast(self.buf, ctypes.c_void_p)  # 引用由 self 持有
            self.t_first = time.monotonic()
            return 1

        def lock(opaque, planes):
            planes[0] = ctypes.cast(self.buf, ctypes.c_void_p)

        def unlock(opaque, picture, planes):
            pass

        def display(opaque, picture):
            self.frames += 1
            self.t_last = time.monotonic()
            # 直接把帧缓冲贴到窗口：SetDIBitsToDevice 免一次中间拷贝
            bmi = (ctypes.c_byte * 40)()
            ctypes.memset(bmi, 0, 40)
            (ctypes.c_uint * 10).from_buffer(bmi)[0] = 40  # biSize
            (ctypes.c_uint * 10).from_buffer(bmi)[1] = self.w   # biWidth
            (ctypes.c_int * 10).from_buffer(bmi)[2] = -self.h   # biHeight 负=自上而下
            (ctypes.c_ushort * 20).from_buffer(bmi)[6] = 1      # biPlanes
            (ctypes.c_ushort * 20).from_buffer(bmi)[7] = 32     # biBitCount
            g32.SetDIBitsToDevice(self.hdc, 0, 0, self.w, self.h,
                                  0, 0, 0, self.h,
                                  picture, ctypes.cast(bmi, ctypes.c_void_p), 0)  # DIB_RGB_COLORS

        def cleanup(opaque):
            pass

        _keep.extend([setup, lock, unlock, display, cleanup])
        self._setup = SETUP_CB(setup)
        self._lock = LOCK_CB(lock)
        self._unlock = UNLOCK_CB(unlock)
        self._display = DISPLAY_CB(display)
        self._cleanup = CLEANUP_CB(cleanup)
        _keep.extend([self._setup, self._lock, self._unlock, self._display, self._cleanup])

    def release(self):
        if self.hdc:
            u32.ReleaseDC(self.hwnd, self.hdc)
            self.hdc = None


# ---------------------------------------------------------------- 主流程

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mode", required=True, choices=["idle", "hwnd", "callbacks"])
    ap.add_argument("--duration", type=float, default=60)
    ap.add_argument("--warmup", type=float, default=5)
    ap.add_argument("--media", default=os.path.join(
        HERE, "..", "testdata", "bench_1080p30.avi"))
    ap.add_argument("--out", required=True)
    ap.add_argument("--no-capture", action="store_true")
    a = ap.parse_args()

    # 进程 DPI 感知，窗口按物理像素创建
    u32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
    W = u32.GetSystemMetrics(78)
    H = u32.GetSystemMetrics(79)

    # stderr 重定向到日志文件（VLC --verbose=2 走 fd 2）
    log_path = a.out + ".vlc.log"
    log_fd = os.open(log_path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    saved_err = os.dup(2)
    os.dup2(log_fd, 2)

    hwnd = create_window(W, H, "WallDesk 渲染路径基准（%s）" % a.mode)
    pid = os.getpid()
    dwm_pid = find_pid("dwm.exe")
    cores = os.cpu_count() or 1
    handle = k32.GetCurrentProcess()

    meter = GpuMeter()
    cap_proc = None
    cap_json = None

    media = player = inst = None
    render = None
    vlc_err = None

    if a.mode != "idle":
        try:
            argv = [s.encode() for s in vlc_args(os.path.join(VLC_DIR, "plugins"))]
            arr = (ctypes.c_char_p * (len(argv) + 1))(*argv, None)
            inst = vlc.libvlc_new(len(argv), arr)
            if not inst:
                raise RuntimeError("libvlc_new 失败")
            media = vlc.libvlc_media_new_path(inst, os.path.abspath(a.media).encode())
            if not media:
                raise RuntimeError("libvlc_media_new_path 失败")
            player = vlc.libvlc_media_player_new_from_media(media)
            if a.mode == "hwnd":
                vlc.libvlc_media_player_set_hwnd(player, ctypes.c_void_p(hwnd))
            else:
                render = SelfRender(hwnd)
                vlc.libvlc_video_set_format_callbacks(player, render._setup, render._cleanup)
                vlc.libvlc_video_set_callbacks(player, render._lock, render._unlock,
                                               render._display, None)
            if vlc.libvlc_media_player_play(player) != 0:
                raise RuntimeError("play 失败")
        except Exception as e:
            vlc_err = str(e)

    # 起抓帧子进程（独立进程，避免哈希开销污染被测进程的 CPU）
    if a.mode != "idle" and not a.no_capture:
        cap_json = a.out + ".cadence.json"
        cap_log = open(cap_json + ".log", "w")
        cap_proc = subprocess.Popen(
            [sys.executable, os.path.join(HERE, "bench_capture_window.py"),
             "--hwnd", str(hwnd), "--duration", str(int(a.duration + a.warmup + 3)),
             "--out", cap_json], stdout=cap_log, stderr=subprocess.STDOUT)
        print("[dbg] helper spawned pid=%s exe=%s script=%s" % (
            cap_proc.pid, sys.executable, os.path.join(HERE, "bench_capture_window.py")))

    # ---------------- 采样循环 ----------------
    samples = []
    last_cpu = proc_cpu_ns(handle)
    last_t = time.monotonic()
    t0 = last_t
    stats_ok = False
    stats_first = None

    while True:
        now = time.monotonic()
        el = now - t0
        if el >= a.warmup + a.duration:
            break
        pump()
        if el >= a.warmup:
            # CPU
            cpu = proc_cpu_ns(handle)
            dt = now - last_t
            cpu_pct = (cpu - last_cpu) / (dt * 1e9) * 100.0 if dt > 0 else 0.0
            last_cpu, last_t = cpu, now
            ws = proc_ws_bytes(handle)
            # GPU（两个候选偏移都记，带载实测后定型）
            gmap, gt8, gt16 = meter.sample()
            s8 = sum(v[0] for k2, v in gmap.items() if "pid_%d_" % pid in k2)
            s16 = sum(v[1] for k2, v in gmap.items() if "pid_%d_" % pid in k2)
            d8 = sum(v[0] for k2, v in gmap.items()
                     if dwm_pid and "pid_%d_" % dwm_pid in k2) if dwm_pid else 0.0
            d16 = sum(v[1] for k2, v in gmap.items()
                      if dwm_pid and "pid_%d_" % dwm_pid in k2) if dwm_pid else 0.0
            # VLC stats
            st = MediaStats()
            disp = lost = None
            if media:
                if vlc.libvlc_media_get_stats(media, ctypes.byref(st)) == 0:
                    disp, lost = st.i_displayed_pictures, st.i_lost_pictures
                    if stats_first is None:
                        stats_first = disp
                    elif disp >= stats_first:
                        stats_ok = True
            samples.append({
                "t": round(el - a.warmup, 2),
                "cpu_pct": round(cpu_pct, 1),
                "ws_mb": round(ws / 2**20, 1),
                "gpu_self8": round(s8, 2), "gpu_self16": round(s16, 2),
                "gpu_dwm8": round(d8, 2), "gpu_dwm16": round(d16, 2),
                "gpu_all8": round(gt8, 2), "gpu_all16": round(gt16, 2),
                "disp": disp, "lost": lost,
            })
        # 睡到下一个 1s 边界
        now2 = time.monotonic()
        target = t0 + a.warmup + (len(samples) + 1)
        nap = target - now2
        if nap > 0:
            time.sleep(nap)
        else:
            time.sleep(0.02)

    # ---------------- 收尾 ----------------
    if player:
        vlc.libvlc_media_player_stop(player)
        vlc.libvlc_media_player_release(player)
    if media:
        vlc.libvlc_media_release(media)
    if inst:
        vlc.libvlc_release(inst)
    if render:
        render.release()
    if cap_proc:
        try:
            cap_proc.wait(timeout=40)
        except Exception:
            cap_proc.kill()
        print("[dbg] helper rc=%s" % cap_proc.returncode)
    os.dup2(saved_err, 2)
    os.close(saved_err)
    os.close(log_fd)

    # cadence 汇总
    cadence = None
    if cap_json and os.path.exists(cap_json):
        try:
            with open(cap_json, "r", encoding="utf-8") as fp:
                cadence = json.load(fp)
        except Exception:
            cadence = None

    def avg(key):
        vals = [s[key] for s in samples if s.get(key) is not None]
        return round(sum(vals) / len(vals), 2) if vals else None

    disp_vals = [s["disp"] for s in samples if s["disp"] is not None]
    result = {
        "mode": a.mode,
        "pid": pid, "hwnd": hwnd, "cores": cores,
        "vlc_version": vlc.libvlc_get_version().decode(),
        "media": os.path.abspath(a.media),
        "media_size_mb": round(os.path.getsize(a.media) / 2**20, 1),
        "duration": a.duration, "warmup": a.warmup,
        "vlc_err": vlc_err,
        "stats_ok": stats_ok,
        "cpu_pct_avg": avg("cpu_pct"),
        "cpu_pct_max": max([s["cpu_pct"] for s in samples], default=None),
        "ws_mb_avg": avg("ws_mb"),
        "ws_mb_max": max([s["ws_mb"] for s in samples], default=None),
        "gpu_self8_avg": avg("gpu_self8"), "gpu_self16_avg": avg("gpu_self16"),
        "gpu_dwm8_avg": avg("gpu_dwm8"), "gpu_dwm16_avg": avg("gpu_dwm16"),
        "gpu_all8_avg": avg("gpu_all8"), "gpu_all16_avg": avg("gpu_all16"),
        "disp_delta": (disp_vals[-1] - disp_vals[0]) if len(disp_vals) >= 2 else None,
        "lost_delta": (samples[-1]["lost"] - samples[0]["lost"])
                      if len(samples) >= 2 and samples[0].get("lost") is not None else None,
        "presented_fps": round((disp_vals[-1] - disp_vals[0]) / a.duration, 2)
                         if len(disp_vals) >= 2 and a.duration > 0 else None,
        "cb_frames": render.frames if render else None,
        "cadence": cadence,
        "samples": samples,
        "vlc_log": log_path,
    }
    with open(a.out, "w", encoding="utf-8") as fp:
        json.dump(result, fp, ensure_ascii=False, indent=1)

    print(json.dumps({k2: v for k2, v in result.items()
                      if k2 not in ("samples", "cadence")}, ensure_ascii=False, indent=1))
    if cadence:
        print("cadence:", json.dumps(cadence, ensure_ascii=False))


if __name__ == "__main__":
    main()
