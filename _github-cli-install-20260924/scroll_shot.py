"""滚动设置页到底部，截主题单选按钮区。"""
import sys
import time
from ctypes import wintypes

sys.path.insert(0, r"C:\Users\Lenovo\WorkBuddy\2026-09-07-15-05-15\WallpaperDesk\_github-cli-install-20260924")
import verify_motion as V
import verify_p1 as P1

OUT = "C:/Users/Lenovo/AppData/Local/Temp/bench/"


def main():
    P1.pin_page(win_page=1, settings_page=3)
    p, hwnd = V.launch()
    U = V.user32
    left, top, w, h = V.D.win_rect(hwnd)
    try:
        time.sleep(2.0)
        # 注入滚轮到内容区中部
        for _ in range(12):
            lp = ((top + 400) << 16) | ((left + 700) & 0xFFFF)
            U.SendMessageW(hwnd, 0x020A, ((-120) << 16) & 0xFFFFFFFF, lp)
            time.sleep(0.03)
        time.sleep(0.6)
        ww, hh, rgb = V.grab(hwnd, 0, 0, w, h)
        V.D.write_png(OUT + "fx_scrolled.png", ww, hh, rgb)
        print("saved fx_scrolled", ww, hh)
    finally:
        V.stop(p)


if __name__ == "__main__":
    main()
