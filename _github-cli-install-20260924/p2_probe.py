"""P2 验收：主页面切换淡入（壁纸库 → 设置）。

判据：切换后 200ms 内内容区从「接近背景」渐变为终态；不存在瞬跳
（即首帧与终态差异应显著大于末帧，且中段有中间值）。
"""
import sys
import time

sys.path.insert(0, r"C:\Users\Lenovo\WorkBuddy\2026-09-07-15-05-15\WallpaperDesk\_github-cli-install-20260924")
import uiautomation as auto
import verify_motion as V
import verify_p1 as P1


def nav_item(hwnd, name):
    win = auto.ControlFromHandle(hwnd)
    out = []

    def walk(c, d=0):
        if d > 8:
            return
        try:
            kids = c.GetChildren()
        except Exception:
            return
        for k in kids:
            try:
                if k.ControlTypeName == "ListItemControl" and k.Name == name:
                    r = k.BoundingRectangle
                    out.append((r.left, r.top, r.width(), r.height()))
            except Exception:
                pass
            walk(k, d + 1)

    walk(win)
    return out[0] if out else None


def main():
    P1.pin_page(win_page=0, settings_page=0)
    p, hwnd = V.launch()
    U = V.user32
    ox, oy = V.client_origin(hwnd)
    try:
        time.sleep(2.0)
        left, top, w, h = V.D.win_rect(hwnd)
        r = nav_item(hwnd, "设置")
        print("设置导航项(屏幕)", r)
        if not r:
            print("未找到导航项")
            return
        sx = r[0] + r[2] // 2 - left
        sy = r[1] + r[3] // 2 - top
        V.leave(hwnd)
        time.sleep(0.4)

        region = (420, 90, 240, 140)
        base_w, base_h, base = V.grab(hwnd, *region)

        # 注入按下/抬起
        cx, cy = sx - ox, sy - oy
        V.press(hwnd, cx, cy, True)
        time.sleep(0.03)
        V.press(hwnd, cx, cy, False)

        # 先只抓帧（BitBlt 约 5-10ms），diff 留到最后算，否则采样间隔会被 Python 循环拖到 300ms
        shots = []
        t0 = time.perf_counter()
        for _ in range(24):
            sw, sh, rgb = V.grab(hwnd, *region)
            shots.append(((time.perf_counter() - t0) * 1000, rgb))
            time.sleep(0.004)
        # 终态
        time.sleep(0.5)
        fw, fh, final = V.grab(hwnd, *region)

        print("%8s %10s %10s" % ("ms", "vs终态", "vs切换前"))
        for ms, rgb in shots:
            d_final = V.diff_px(rgb, final, fw, fh, thr=4, maxpx=400000)
            d_base = V.diff_px(rgb, base, base_w, base_h, thr=4, maxpx=400000)
            print("%8.0f %10d %10d" % (ms, d_final, d_base))
    finally:
        V.stop(p)


if __name__ == "__main__":
    main()
