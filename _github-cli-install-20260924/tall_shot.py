"""把窗口临时拉高到 1000，看清设置页底部（主题单选按钮）。"""
import sys
import time

sys.path.insert(0, r"C:\Users\Lenovo\WorkBuddy\2026-09-07-15-05-15\WallpaperDesk\_github-cli-install-20260924")
import verify_motion as V
import verify_p1 as P1

OUT = "C:/Users/Lenovo/AppData/Local/Temp/bench/"
SWP_NOZORDER = 0x0004


def main():
    P1.pin_page(win_page=1, settings_page=3)
    p, hwnd = V.launch()
    U = V.user32
    try:
        time.sleep(2.0)
        left, top, w, h = V.D.win_rect(hwnd)
        U.SetWindowPos(hwnd, 0, left, top, w, 1000, SWP_NOZORDER)
        time.sleep(1.2)
        w2, h2, rgb = V.grab(hwnd, 0, 0, w, 1000)
        V.D.write_png(OUT + "fx_tall.png", w2, h2, rgb)
        print("saved fx_tall", w2, h2)
        import uiautomation as auto
        import pxtool
        win = auto.ControlFromHandle(hwnd)
        radios = []

        def walk(c, d=0):
            if d > 14:
                return
            try:
                kids = c.GetChildren()
            except Exception:
                return
            for k in kids:
                try:
                    if k.ControlTypeName == "RadioButtonControl":
                        r = k.BoundingRectangle
                        radios.append((k.Name, r.left - left, r.top - top, r.width(), r.height()))
                except Exception:
                    pass
                walk(k, d + 1)

        walk(win)
        print("单选:", radios)
        if radios:
            ys = [r[1] for r in radios]
            y = min(ys) - 12
            pxtool.crop(OUT + "fx_tall.png", OUT + "z_radio_final.png", 350, y, 420, 70, 3)
    finally:
        V.stop(p)


if __name__ == "__main__":
    main()
