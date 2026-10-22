"""验证状态胶囊：嵌入状态栏位置 + 悬浮详情浮层。

本机 SetCursorPos 不产生 WM_MOUSEMOVE，沿用注入 + 真实光标同点位的做法。
"""
import sys
import time

sys.path.insert(0, r"C:\Users\Lenovo\WorkBuddy\2026-09-07-15-05-15\WallpaperDesk\_github-cli-install-20260924")
import verify_motion as V
import verify_p1 as P1

OUT = "C:/Users/Lenovo/AppData/Local/Temp/bench/"


def popups(hwnd):
    out = []
    for (h, cls, title, r) in V.D.my_windows():
        if h == hwnd or not V.user32.IsWindowVisible(h):
            continue
        if r[2] < 60 or r[3] < 30:
            continue
        out.append((h, cls, r))
    return out


def main():
    P1.pin_page(win_page=0, settings_page=0)
    p, hwnd = V.launch()
    U = V.user32
    ox, oy = V.client_origin(hwnd)
    try:
        time.sleep(2.0)
        left, top, w, h = V.D.win_rect(hwnd)
        # 胶囊定位：直接用 UIA 找状态栏里那段 "… fps … MB" 文本，比扫像素稳
        import uiautomation as auto
        win = auto.ControlFromHandle(hwnd)
        found = []

        def walk(c, d=0):
            if d > 14:
                return
            try:
                kids = c.GetChildren()
            except Exception:
                return
            for k in kids:
                try:
                    if k.ControlTypeName == "TextControl":
                        nm = k.Name or ""
                        if "MB" in nm or "fps" in nm:
                            r = k.BoundingRectangle
                            found.append((nm, r.left - left, r.top - top, r.width(), r.height()))
                except Exception:
                    pass
                walk(k, d + 1)

        walk(win)
        print("状态栏文本:", found)
        if not found:
            print("未找到胶囊")
            return
        nm, wx, wy, cw, chh = found[0]
        cx = wx + cw // 2
        cy = wy + chh // 2
        print("胶囊中心(窗口坐标)", cx, cy, "文本:", nm)
        # 离场基准
        V.leave(hwnd)
        time.sleep(0.6)
        print("悬浮前浮层:", popups(hwnd))
        # 注入鼠标移动 + 真实光标同点
        U.SetCursorPos(left + cx, top + cy)
        pmp = V.Pump(hwnd, cx - ox, cy - oy).start()
        for i in range(8):
            time.sleep(0.1)
            ps = popups(hwnd)
            if ps:
                print("%dms 浮层出现:" % ((i + 1) * 100), ps)
                break
            if i == 7:
                print("800ms 内未出现浮层")

        time.sleep(0.4)
        ps = popups(hwnd)
        if ps:
            ph, cls, r = ps[0]
            wwx, hhx, rgbx = V.grab(hwnd, 0, 0, w, h)
            V.D.write_png(OUT + "capsule_detail_full.png", wwx, hhx, rgbx)
            # 只抓浮层矩形
            fx = r[0] - left
            fy = r[1] - top
            fw, fh_, frgb = V.grab(hwnd, fx, fy, r[2], min(r[3], 400))
            V.D.write_png(OUT + "capsule_detail.png", fw, fh_, frgb)
            print("浮层几何(窗口相对)", fx, fy, r[2], r[3], "saved capsule_detail.png")

        # 移走
        pmp.close()
        U.SetCursorPos(5, 5)
        V.leave(hwnd)
        time.sleep(0.6)
        print("移走后浮层:", popups(hwnd))
        ww2, hh2, rgb2 = V.grab(hwnd, 0, 0, w, h)
        V.D.write_png(OUT + "capsule_ready.png", ww2, hh2, rgb2)
        print("saved capsule_ready.png")
    finally:
        V.stop(p)


if __name__ == "__main__":
    main()
