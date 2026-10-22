# -*- coding: utf-8 -*-
"""以 10Hz 抓取指定窗口画面，统计「合成器可见帧率」。

独立进程运行：哈希开销不占用被测进程 CPU（bench_render_path.py 调用）。
每帧用 PrintWindow(PW_RENDERFULLCONTENT) 抓取；全黑时回退 BitBlt(窗口 DC)。
统计：总抓帧数、相邻不同帧数（=画面变化次数）、去重帧数、每秒变化次数。

用法：
    python bench_capture_window.py --hwnd 123456 --duration 63 --interval 0.1 --out x.json
"""
import argparse
import ctypes
import ctypes.wintypes as w
import hashlib
import json
import time

u32 = ctypes.windll.user32
g32 = ctypes.windll.gdi32

class RECT(ctypes.Structure):
    _fields_ = [("left", ctypes.c_long), ("top", ctypes.c_long),
                ("right", ctypes.c_long), ("bottom", ctypes.c_long)]


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [("biSize", w.DWORD), ("biWidth", ctypes.c_long), ("biHeight", ctypes.c_long),
                ("biPlanes", w.WORD), ("biBitCount", w.WORD), ("biCompression", w.DWORD),
                ("biSizeImage", w.DWORD), ("biXPelsPerMeter", ctypes.c_long),
                ("biYPelsPerMeter", ctypes.c_long), ("biClrUsed", w.DWORD),
                ("biClrImportant", w.DWORD)]


class BITMAPINFO(ctypes.Structure):
    _fields_ = [("bmiHeader", BITMAPINFOHEADER), ("bmiColors", w.DWORD * 3)]


# ---- Win32 签名（不设 argtypes 时指针参数会被按 32 位截断）----
u32.GetClientRect.argtypes = [w.HWND, ctypes.POINTER(RECT)]
u32.GetWindowDC.restype = w.HDC
u32.GetWindowDC.argtypes = [w.HWND]
u32.ReleaseDC.restype = ctypes.c_int
u32.ReleaseDC.argtypes = [w.HWND, w.HDC]
u32.PrintWindow.restype = w.BOOL
u32.PrintWindow.argtypes = [w.HWND, w.HDC, ctypes.c_uint]
g32.CreateCompatibleDC.restype = w.HDC
g32.CreateCompatibleDC.argtypes = [w.HDC]
g32.CreateDIBSection.restype = w.HBITMAP
g32.CreateDIBSection.argtypes = [w.HDC, ctypes.POINTER(BITMAPINFO), ctypes.c_uint,
                                 ctypes.POINTER(ctypes.c_void_p), w.HANDLE, w.DWORD]
g32.SelectObject.restype = w.HGDIOBJ
g32.SelectObject.argtypes = [w.HDC, w.HGDIOBJ]
g32.BitBlt.restype = w.BOOL
g32.BitBlt.argtypes = [w.HDC, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                       w.HDC, ctypes.c_int, ctypes.c_int, w.DWORD]
g32.DeleteObject.restype = w.BOOL
g32.DeleteObject.argtypes = [w.HGDIOBJ]
g32.DeleteDC.restype = w.BOOL
g32.DeleteDC.argtypes = [w.HDC]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--hwnd", type=int, required=True)
    ap.add_argument("--duration", type=float, default=63)
    ap.add_argument("--interval", type=float, default=0.1)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()

    hwnd = w.HWND(a.hwnd)
    rect = RECT()
    u32.GetClientRect(hwnd, ctypes.byref(rect))
    W = rect.right - rect.left
    H = rect.bottom - rect.top
    if W <= 0 or H <= 0:
        raise SystemExit("窗口客户区为空（可能已销毁）")

    hdc_win = u32.GetWindowDC(hwnd)
    hdc_mem = g32.CreateCompatibleDC(hdc_win)
    bi = BITMAPINFO()
    bi.bmiHeader.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    bi.bmiHeader.biWidth = W
    bi.bmiHeader.biHeight = -H  # 自上而下
    bi.bmiHeader.biPlanes = 1
    bi.bmiHeader.biBitCount = 24
    bits = ctypes.c_void_p()
    hbm = g32.CreateDIBSection(hdc_mem, ctypes.byref(bi), 0,  # DIB_RGB_COLORS
                               ctypes.byref(bits), None, 0)
    g32.SelectObject(hdc_mem, hbm)
    nbytes = W * H * 3

    t0 = time.monotonic()
    prev = None
    transitions = 0
    captured = 0
    zero_frames = 0
    unique = set()
    per_second = []
    sec_trans = 0
    sec_idx = 0
    method_used = {"printwindow": 0, "bitblt": 0}

    while True:
        now = time.monotonic()
        el = now - t0
        if el >= a.duration:
            break
        # PrintWindow 优先
        ok = u32.PrintWindow(hwnd, hdc_mem, 2)  # PW_RENDERFULLCONTENT
        data = ctypes.string_at(bits.value, nbytes) if bits.value else b""
        if ok and any(data):
            method_used["printwindow"] += 1
        else:
            g32.BitBlt(hdc_mem, 0, 0, W, H, hdc_win, 0, 0, 0x00CC0020)  # SRCCOPY
            data = ctypes.string_at(bits.value, nbytes)
            method_used["bitblt"] += 1
        captured += 1
        if not any(data):
            zero_frames += 1
        digest = hashlib.blake2b(data, digest_size=16).digest()
        unique.add(digest)
        if prev is not None and digest != prev:
            transitions += 1
            sec_trans += 1
        prev = digest

        if int(el) != sec_idx:
            per_second.append(round(sec_trans, 2))
            sec_trans = 0
            sec_idx = int(el)

        nap = t0 + captured * a.interval - time.monotonic()
        if nap > 0:
            time.sleep(nap)

    u32.ReleaseDC(hwnd, hdc_win)
    g32.DeleteObject(hbm)
    g32.DeleteDC(hdc_mem)

    result = {
        "hwnd": a.hwnd, "w": W, "h": H,
        "interval": a.interval,
        "duration": round(time.monotonic() - t0, 2),
        "captured": captured,
        "transitions": transitions,
        "unique_frames": len(unique),
        "zero_frames": zero_frames,
        "est_fps": round(transitions / (time.monotonic() - t0), 2),
        "per_second_transitions": per_second,
        "method_used": method_used,
    }
    with open(a.out, "w", encoding="utf-8") as fp:
        json.dump(result, fp, ensure_ascii=False, indent=1)
    print(json.dumps(result, ensure_ascii=False))


if __name__ == "__main__":
    main()
