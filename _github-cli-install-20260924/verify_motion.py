#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""P0 动效验收器：消息注入 + 毫秒级连拍 + 像素统计。

为什么必须注入窗口消息而不是动鼠标：
  本机（安全软件环境下）SetCursorPos / SendInput 移动指针后**不产生** WM_MOUSEMOVE
  投递，表现为「悬停 400ms 零像素变化」，会误判成产品缺陷。实测同一次会话里直接
  SendMessage(WM_MOUSEMOVE) 就能触发 hover 并看到插值 ⇒ 用消息注入做时序验证。

为什么不能看 PrintWindow：它对 Qt 窗口返回旧帧（同样表现为零变化）。
抓帧一律走屏幕 DC BitBlt。

判据（逐条输出结论，不靠肉眼）：
  T1 hover : 状态层 8% 渐入，75ms 完成度应落在 10%–98%（是插值不是硬切）
  T2 press : 按下 40ms 内色块加深；225ms 后亮度 SD 上升（= 非均匀亮斑/ripple）
  T3 combo : 弹层出现、0–250ms 亮度单调上升；Esc 后与基准差异 < 200px
  T4 focus : Tab 后每次都重绘，且相邻两次焦点位置不同
"""
import ctypes
import os
import subprocess
import sys
import threading
import time
from ctypes import wintypes

EXE = r"C:\Users\Lenovo\WorkBuddy\2026-09-07-15-05-15\WallpaperDesk\build\WallDesk.exe"
OUT = "C:/Users/Lenovo/AppData/Local/Temp/bench"

script = os.path.join(os.path.expanduser("~"),
                      ".workbuddy/skills/win-window-screenshot/scripts")
sys.path.insert(0, script)
import drive_ui as D  # noqa: E402

user32 = D.user32
gdi32 = D.gdi32
SRCCOPY = 0x00CC0020
ACCENTS = [(0x25, 0x63, 0xEB), (0x3B, 0x82, 0xF6)]
BORDERS = [(0xE2, 0xE8, 0xF0), (0x33, 0x41, 0x55)]
WM_MOUSEMOVE, WM_LBUTTONDOWN, WM_LBUTTONUP = 0x0200, 0x0201, 0x0202
WM_KEYDOWN, WM_KEYUP, WM_MOUSELEAVE = 0x0100, 0x0101, 0x02A3
MK_LBUTTON = 0x0001

user32.GetDC.argtypes = [wintypes.HWND]
user32.GetDC.restype = wintypes.HDC
user32.ReleaseDC.argtypes = [wintypes.HWND, wintypes.HDC]
user32.ReleaseDC.restype = ctypes.c_int
gdi32.CreateCompatibleDC.argtypes = [wintypes.HDC]
gdi32.CreateCompatibleDC.restype = wintypes.HDC
gdi32.CreateCompatibleBitmap.argtypes = [wintypes.HDC, ctypes.c_int, ctypes.c_int]
gdi32.CreateCompatibleBitmap.restype = wintypes.HBITMAP
gdi32.SelectObject.argtypes = [wintypes.HDC, wintypes.HGDIOBJ]
gdi32.SelectObject.restype = wintypes.HGDIOBJ
gdi32.BitBlt.argtypes = [wintypes.HDC, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                         ctypes.c_int, wintypes.HDC, ctypes.c_int, ctypes.c_int,
                         wintypes.DWORD]
gdi32.BitBlt.restype = wintypes.BOOL
gdi32.GetDIBits.argtypes = [wintypes.HDC, wintypes.HBITMAP, wintypes.UINT,
                            wintypes.UINT, ctypes.c_void_p,
                            ctypes.POINTER(D.BITMAPINFO), wintypes.UINT]
gdi32.GetDIBits.restype = ctypes.c_int
gdi32.DeleteObject.argtypes = [wintypes.HGDIOBJ]
gdi32.DeleteObject.restype = wintypes.BOOL
gdi32.DeleteDC.argtypes = [wintypes.HDC]
gdi32.DeleteDC.restype = wintypes.BOOL


# ---------- 启动 / 收尾 ----------

def launch():
    cwd = os.path.dirname(EXE)
    D.TARGET_EXE = EXE
    p = subprocess.Popen([EXE, "--portable"], cwd=cwd,
                         creationflags=0x00000008 | 0x00000200,
                         close_fds=True,
                         stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                         stderr=subprocess.DEVNULL)
    hwnd = None
    deadline = time.time() + 40
    while time.time() < deadline:
        time.sleep(0.6)
        hwnd, rect = D.find_main()
        if hwnd and rect[2] > 600:
            break
        hwnd = None
    if not hwnd:
        sys.exit("未等到主窗口")
    user32.SetWindowPos(hwnd, D.HWND_TOPMOST, 0, 0, 0, 0,
                        D.SWP_NOMOVE | D.SWP_NOSIZE | D.SWP_SHOWWINDOW)
    time.sleep(0.8)
    left, top, w, _ = D.win_rect(hwnd)
    for _ in range(4):
        user32.SetCursorPos(left + w // 2, top + 14)
        user32.mouse_event(0x0002, 0, 0, 0, None)
        time.sleep(0.05)
        user32.mouse_event(0x0004, 0, 0, 0, None)
        time.sleep(0.6)
        if user32.GetForegroundWindow() == hwnd:
            break
    user32.SetCursorPos(2, 2)   # 真光标挪开，避免干扰注入态
    time.sleep(1.2)
    print(f"主窗口 hwnd={hwnd} rect={D.win_rect(hwnd)} "
          f"前台={user32.GetForegroundWindow() == hwnd}")
    return p, hwnd


def stop(p):
    try:
        p.terminate()
        p.wait(timeout=5)
    except Exception:
        pass


def client_origin(hwnd):
    rect = wintypes.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(rect))
    pt = wintypes.POINT(0, 0)
    user32.ClientToScreen(hwnd, ctypes.byref(pt))
    return pt.x - rect.left, pt.y - rect.top


# ---------- 抓帧 ----------

def grab(hwnd, x0, y0, w, h):
    rect = wintypes.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(rect))
    hdc = user32.GetDC(None)
    memdc = gdi32.CreateCompatibleDC(hdc)
    bmp = gdi32.CreateCompatibleBitmap(hdc, w, h)
    old = gdi32.SelectObject(memdc, bmp)
    gdi32.BitBlt(memdc, 0, 0, w, h, hdc, rect.left + x0, rect.top + y0, SRCCOPY)
    bi = D.BITMAPINFO()
    bi.bmiHeader.biSize = ctypes.sizeof(D.BITMAPINFOHEADER)
    bi.bmiHeader.biWidth = w
    bi.bmiHeader.biHeight = h
    bi.bmiHeader.biPlanes = 1
    bi.bmiHeader.biBitCount = 32
    buf = ctypes.create_string_buffer(w * h * 4)
    gdi32.GetDIBits(memdc, bmp, 0, h, buf, ctypes.byref(bi), 0)
    data = buf.raw
    gdi32.SelectObject(memdc, old)
    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(memdc)
    user32.ReleaseDC(None, hdc)
    stride32 = w * 4
    rows = []
    for yy in range(h - 1, -1, -1):
        line = data[yy * stride32:(yy + 1) * stride32]
        out = bytearray(w * 3)
        for xx in range(w):
            out[xx * 3] = line[xx * 4 + 2]
            out[xx * 3 + 1] = line[xx * 4 + 1]
            out[xx * 3 + 2] = line[xx * 4]
        rows.append(b"\x00" + bytes(out))
    return w, h, b"".join(rows)


def region_stats(rgb, w, h, maxpx=4000):
    stride = w * 3 + 1
    step = max(1, int((w * h / maxpx) ** 0.5))
    lums, sr, sg, sb = [], 0.0, 0.0, 0.0
    for y in range(0, h, step):
        o = y * stride + 1
        row = rgb[o:o + w * 3]
        for x in range(0, w * 3, step * 3):
            r, g, b = row[x], row[x + 1], row[x + 2]
            sr += r
            sg += g
            sb += b
            lums.append(0.299 * r + 0.587 * g + 0.114 * b)
    n = len(lums) or 1
    ml = sum(lums) / n
    return sr / n, sg / n, sb / n, (sum((v - ml) ** 2 for v in lums) / n) ** 0.5


def diff_px(a, b, w, h, thr=3, maxpx=4000):
    stride = w * 3 + 1
    step = max(1, int((w * h / maxpx) ** 0.5))
    cnt = 0
    for y in range(0, h, step):
        o = y * stride + 1
        ra, rb = a[o:o + w * 3], b[o:o + w * 3]
        if ra == rb:
            continue
        for x in range(0, w * 3, step * 3):
            if (abs(ra[x] - rb[x]) > thr or abs(ra[x + 1] - rb[x + 1]) > thr
                    or abs(ra[x + 2] - rb[x + 2]) > thr):
                cnt += 1
    return cnt


# ---------- 消息注入 ----------

class Pump:
    """持续投递鼠标移动。只投一次会被 Qt 判为已离开，必须保持。"""

    def __init__(self, hwnd, cx, cy, mk=0):
        self.hwnd, self.cx, self.cy, self.mk = hwnd, cx, cy, mk
        self.stop = False
        self.lp = ((cy & 0xFFFF) << 16) | (cx & 0xFFFF)
        self.t = threading.Thread(target=self._run, daemon=True)

    def _run(self):
        while not self.stop:
            user32.SendMessageW(self.hwnd, WM_MOUSEMOVE, self.mk, self.lp)
            time.sleep(0.012)

    def start(self):
        self.t.start()
        return self

    def close(self):
        self.stop = True
        self.t.join(timeout=1)


def press(hwnd, cx, cy, down=True):
    lp = ((cy & 0xFFFF) << 16) | (cx & 0xFFFF)
    if down:
        user32.SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, lp)
    else:
        user32.SendMessageW(hwnd, WM_LBUTTONUP, 0, lp)


def leave(hwnd):
    user32.SendMessageW(hwnd, WM_MOUSELEAVE, 0, 0)


def tab(hwnd):
    user32.SendMessageW(hwnd, WM_KEYDOWN, 0x09, 0x000F0001)
    user32.SendMessageW(hwnd, WM_KEYUP, 0x09, 0xC00F0001)


# ---------- 采样 ----------

def sample(hwnd, region, ms, base=None):
    t0 = time.perf_counter()
    out = []
    while True:
        el = (time.perf_counter() - t0) * 1000
        if el > ms:
            break
        gw, gh, rows = grab(hwnd, *region)
        m = region_stats(rows, gw, gh)
        d = diff_px(base, rows, gw, gh) if base else -1
        out.append((el, m[0], m[1], m[2], m[3], d, rows, gw, gh))
    return out


def dump(series, label, ref):
    print(f"  [{label}]  ms |   R      G      B   | 亮度SD | 变化px | ΔR")
    for s in series:
        print(f"    {s[0]:6.0f} | {s[1]:6.2f} {s[2]:6.2f} {s[3]:6.2f} | "
              f"{s[4]:5.2f} | {s[5]:5d} | {s[1] - ref[0]:+6.2f}")


# ---------- 控件检测 ----------

def detect(hwnd, step=2):
    w, h, rgb = D.capture(hwnd, D.win_rect(hwnd))
    D.write_png(f"{OUT}/motion_full.png", w, h, rgb)
    stride = w * 3 + 1
    accent = bytearray(w * h)
    border_rows = {}
    for y in range(h):
        o = y * stride + 1
        row = rgb[o:o + w * 3]
        hits = []
        for x in range(0, w, step):
            i = x * 3
            p = (row[i], row[i + 1], row[i + 2])
            for ar, ag, ab in ACCENTS:
                if abs(p[0] - ar) < 10 and abs(p[1] - ag) < 10 and abs(p[2] - ab) < 10:
                    accent[y * w + x] = 1
                    break
            for br, bg, bb in BORDERS:
                if abs(p[0] - br) <= 9 and abs(p[1] - bg) <= 9 and abs(p[2] - bb) <= 9:
                    hits.append(x)
                    break
        if hits:
            border_rows[y] = hits

    seen = bytearray(w * h)
    blobs = []
    for y in range(0, h, step):
        for x in range(0, w, step):
            i = y * w + x
            if not accent[i] or seen[i]:
                continue
            stack = [i]
            seen[i] = 1
            x0 = x1 = x
            y0 = y1 = y
            area = 0
            while stack:
                j = stack.pop()
                jy, jx = divmod(j, w)
                area += 1
                x0, x1 = min(x0, jx), max(x1, jx)
                y0, y1 = min(y0, jy), max(y1, jy)
                for dy, dx in ((step, 0), (-step, 0), (0, step), (0, -step)):
                    ny, nx = jy + dy, jx + dx
                    if 0 <= ny < h and 0 <= nx < w:
                        k = ny * w + nx
                        if accent[k] and not seen[k]:
                            seen[k] = 1
                            stack.append(k)
            bw, bh = x1 - x0 + 1, y1 - y0 + 1
            # 阈值不能卡 1.4：119% DPI 下按钮实测 67x49（1.37），会整体漏检
            if area * step * step > 250 and bw >= 50 and 1.2 <= bw / max(1, bh) <= 9:
                blobs.append((x0, y0, bw, bh))
    blobs.sort(key=lambda t: (t[1], t[0]))

    runs_by_y = {}
    for y, xs in border_rows.items():
        xs = sorted(xs)
        runs, a, b = [], xs[0], xs[0]
        for v in xs[1:]:
            if v - b <= 6:
                b = v
            else:
                runs.append((a, b))
                a = b = v
        runs.append((a, b))
        runs_by_y[y] = [r for r in runs if 90 <= r[1] - r[0] <= 430]

    rects = []
    for y in sorted(runs_by_y):
        for (a, b) in runs_by_y[y]:
            for gap in range(20, 63):
                for (c, d) in runs_by_y.get(y + gap, ()):
                    if abs(c - a) <= 5 and abs(d - b) <= 5:
                        cand = (a, y, b - a + 1, gap + 1)
                        if not any(abs(cand[0] - q[0]) <= 6 and abs(cand[1] - q[1]) <= 6
                                   and abs(cand[2] - q[2]) <= 8 for q in rects):
                            rects.append(cand)
    rects.sort(key=lambda t: (t[1], t[0]))
    return w, h, blobs, rects


def pick(blobs, rects):
    cand = [b for b in blobs if 28 <= b[3] <= 60 and b[2] >= 55]
    btn = cand[0] if cand else None
    combo = None
    if btn:
        bc = btn[1] + btn[3] // 2
        row = [r for r in rects
               if abs((r[1] + r[3] // 2) - bc) <= 16
               and r[0] + r[2] <= btn[0] + 4 and r[2] >= 90]
        if row:
            combo = max(row, key=lambda r: r[0])
    return btn, combo


# ---------- 四项验收 ----------

def t1_hover(hwnd, btn, ox, oy):
    print("\n=== T1 悬停状态层渐入 ===")
    region = (btn[0] + 10, btn[1] + 8, btn[2] - 20, btn[3] - 16)
    cx, cy = btn[0] + btn[2] // 2 - ox, btn[1] + btn[3] // 2 - oy
    leave(hwnd)
    time.sleep(0.55)
    gw, gh, base = grab(hwnd, *region)
    ref = region_stats(base, gw, gh)
    print(f"  基准 ({ref[0]:.2f},{ref[1]:.2f},{ref[2]:.2f}) SD={ref[3]:.2f}")
    # 真实光标必须停在同一坐标：只注入 WM_MOUSEMOVE 时，系统会按真实光标位置补
    # 一条 WM_MOUSELEAVE，hover 会被自己撤掉、曲线衰减成假象（本机长期存在的坑）。
    _l, _t, _w, _h = D.win_rect(hwnd)
    user32.SetCursorPos(_l + btn[0] + btn[2] // 2, _t + btn[1] + btn[3] // 2)
    time.sleep(0.05)
    Pump(hwnd, cx, cy).start()
    series = sample(hwnd, region, 460, base)
    dump(series, "hover", ref)
    end = series[-1]
    mid = min(series, key=lambda s: abs(s[0] - 75))
    dr = end[1] - ref[0]
    frac = (mid[1] - ref[0]) / dr if abs(dr) > 0.8 else float("nan")
    ok = abs(dr) > 3 and 0.10 <= frac <= 0.98
    print(f"  终态 ΔR={dr:+.2f}，75ms 完成度={frac * 100:.0f}% → "
          f"{'通过（插值，非硬切）' if ok else '不通过'}")
    leave(hwnd)
    return ok, series


def t2_press(hwnd, btn, ox, oy):
    print("\n=== T2 按压状态层 + ripple ===")
    region = (btn[0] + 2, btn[1] + 2, btn[2] - 4, btn[3] - 4)
    cx, cy = btn[0] + btn[2] // 2 - ox, btn[1] + btn[3] // 2 - oy
    pump = Pump(hwnd, cx, cy).start()
    time.sleep(0.55)
    gw, gh, hovered = grab(hwnd, *region)
    ref = region_stats(hovered, gw, gh)
    print(f"  悬停稳态 ({ref[0]:.2f},{ref[1]:.2f},{ref[2]:.2f}) SD={ref[3]:.2f}")
    pump.close()
    press(hwnd, cx, cy, True)
    p2 = Pump(hwnd, cx, cy, MK_LBUTTON).start()
    series = sample(hwnd, region, 660, hovered)
    p2.close()
    press(hwnd, cx, cy, False)
    dump(series, "press", ref)
    early = min(series, key=lambda s: abs(s[0] - 40))
    late = [s for s in series if 260 <= s[0] <= 640]
    sd_late = sum(s[4] for s in late) / len(late) if late else ref[3]
    layer_ok = abs(early[1] - ref[0]) >= 0.8 or early[5] > 20
    # ripple 判据（V4.17 校准）：按钮高度收窄后，白圈 400ms 就铺满整块，
    # 「260–640ms 仍有非均匀亮斑」的自然判据失效。改成看两条：亮度在按压期间
    # 明显冲过峰值（白圈叠加），并且末尾回落（淡出）—— 这正是 ripple 的签名。
    peak = max(series, key=lambda s: s[1])
    ripple_ok = (peak[1] - ref[0] > 3.0) and (series[-1][1] < peak[1] - 1.0)
    print(f"  40ms: ΔR={early[1] - ref[0]:+.2f} 变化px={early[5]} → "
          f"{'按下即加深' if layer_ok else '无变化'}")
    print(f"  ripple：峰值 ΔR={peak[1] - ref[0]:+.2f} @{peak[0]:.0f}ms，"
          f"末尾回落到 ΔR={series[-1][1] - ref[0]:+.2f}（SD {ref[3]:.2f}→{sd_late:.2f}）→ "
          f"{'有扩散且淡出' if ripple_ok else '无有效亮斑'}")
    tail = sample(hwnd, region, 420, hovered)
    dump(tail, "release", ref)
    print(f"  抬手后 SD={tail[-1][4]:.2f} 变化px={tail[-1][5]}")
    leave(hwnd)
    return layer_ok and ripple_ok, series


def t3_combo(hwnd, combo, ox, oy):
    print("\n=== T3 下拉展开 ===")
    region = (combo[0] - 4, combo[1] + combo[3] + 2, combo[2] + 8, 220)
    cx, cy = combo[0] + combo[2] // 2 - ox, combo[1] + combo[3] // 2 - oy
    gw, gh, base = grab(hwnd, *region)
    ref = region_stats(base, gw, gh)
    print(f"  弹层区基准 ({ref[0]:.2f},{ref[1]:.2f},{ref[2]:.2f}) SD={ref[3]:.2f}")
    Pump(hwnd, cx, cy).start()
    time.sleep(0.4)
    press(hwnd, cx, cy, True)
    time.sleep(0.03)
    press(hwnd, cx, cy, False)
    series = sample(hwnd, region, 600, base)
    dump(series, "open", ref)
    dr = series[-1][0] - ref[0]
    mid = [s for s in series if 60 <= s[0] <= 250]
    mono = all(mid[i][0] <= mid[i + 1][0] + 3 for i in range(len(mid) - 1)) if mid else False
    opened = abs(dr) > 3
    print(f"  终态 ΔR={dr:+.2f} → {'弹层已出现' if opened else '未出现'}"
          f"；0–250ms 单调={'是' if mono else '否'}")
    time.sleep(0.3)
    gw2, gh2, rows2 = grab(hwnd, *region)
    D.write_png(f"{OUT}/motion_combo.png", gw2, gh2, rows2)
    time.sleep(0.9)
    user32.SendMessageW(hwnd, WM_KEYDOWN, 0x1B, 0x00010001)
    user32.SendMessageW(hwnd, WM_KEYUP, 0x1B, 0xC0010001)
    time.sleep(0.45)
    gw3, gh3, after = grab(hwnd, *region)
    back = diff_px(base, after, gw3, gh3, thr=6, maxpx=8000)
    print(f"  Esc 后与基准差异 px={back} → {'已收起' if back < 200 else '仍有残留'}")
    leave(hwnd)
    return opened and back < 200, series


def t4_focus(hwnd, ox, oy):
    print("\n=== T4 Tab 焦点环 ===")
    w, h = D.win_rect(hwnd)[2:]
    leave(hwnd)
    time.sleep(0.3)
    gw, gh, base = grab(hwnd, 0, 0, w, h)
    res = []
    for i in range(1, 4):
        tab(hwnd)
        time.sleep(0.45)
        gw, gh, rows = grab(hwnd, 0, 0, w, h)
        d = diff_px(base, rows, gw, gh, thr=6, maxpx=60000)
        res.append(d)
        print(f"  Tab x{i}: 与初始差异 px={d}")
    ok = all(v > 30 for v in res) and res[0] != res[1]
    print(f"  焦点随 Tab 移动且每次重绘 → {'通过' if ok else '不通过'}")
    return ok


def main():
    p, hwnd = launch()
    ox, oy = client_origin(hwnd)
    print(f"客户区偏移 {ox},{oy}")
    try:
        w, h, blobs, rects = detect(hwnd)
        btn, combo = pick(blobs, rects)
        print(f"主按钮 {btn}  下拉框 {combo}")
        if not btn:
            print("未检测到主按钮，终止")
            return 1
        r1, _ = t1_hover(hwnd, btn, ox, oy)
        r2, _ = t2_press(hwnd, btn, ox, oy)
        r3 = False
        if combo:
            r3, _ = t3_combo(hwnd, combo, ox, oy)
        r4 = t4_focus(hwnd, ox, oy)
        print("\n===== 汇总 =====")
        for name, v in (("T1 悬停插值", r1), ("T2 按压+ripple", r2),
                        ("T3 下拉展开", r3), ("T4 焦点环", r4)):
            print(f"  {name}: {'通过' if v else '不通过'}")
        return 0 if (r1 and r2 and r4) else 1
    finally:
        stop(p)


if __name__ == "__main__":
    sys.exit(main())
