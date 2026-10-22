"""验收设置子页切换缓动（壁纸 → 播放）：确认 QGraphicsOpacityEffect 是否真的逐帧可见。"""
import sys
import time

sys.path.insert(0, r"C:\Users\Lenovo\WorkBuddy\2026-09-07-15-05-15\WallpaperDesk\_github-cli-install-20260924")
import uiautomation as auto
import verify_motion as V
import verify_p1 as P1


def find_item(hwnd, name, max_depth=10):
    win = auto.ControlFromHandle(hwnd)
    out = []

    def walk(c, d=0):
        if d > max_depth:
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
    return out


def main():
    P1.pin_page(win_page=1, settings_page=0)
    p, hwnd = V.launch()
    try:
        time.sleep(2.2)
        left, top, w, h = V.D.win_rect(hwnd)
        items = find_item(hwnd, "播放")
        print("候选「播放」项:", items)
        # 设置子导航项 x 较小（主导航在同名 x 更大？此处按 x 排序取左列）
        if not items:
            print("未找到子导航项")
            return
        r = sorted(items, key=lambda t: t[0])[0]
        sx, sy = r[0] + r[2] // 2 - left, r[1] + r[3] // 2 - top
        ox, oy = V.client_origin(hwnd)
        V.leave(hwnd)
        time.sleep(0.5)

        region = (420, 90, 240, 140)
        V.press(hwnd, sx - ox, sy - oy, True)
        time.sleep(0.03)
        V.press(hwnd, sx - ox, sy - oy, False)

        shots = []
        t0 = time.perf_counter()
        for _ in range(16):
            sw, sh, rgb = V.grab(hwnd, *region)
            shots.append(((time.perf_counter() - t0) * 1000, rgb))
            time.sleep(0.004)
        time.sleep(0.5)
        fw, fh, final = V.grab(hwnd, *region)
        print("%8s %10s" % ("ms", "vs终态"))
        for ms, rgb in shots:
            print("%8.0f %10d" % (ms, V.diff_px(rgb, final, fw, fh, thr=4, maxpx=400000)))
    finally:
        V.stop(p)


if __name__ == "__main__":
    main()
