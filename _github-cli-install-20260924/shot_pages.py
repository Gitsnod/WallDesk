"""界面走查截图：按 (winPage, settingsPage) 逐屏启动、抓整窗。

用法：python shot_pages.py [标签...]
输出：C:/Users/Lenovo/AppData/Local/Temp/bench/walk_<标签>.png
"""
import os
import sys
import time

sys.path.insert(0, r"C:\Users\Lenovo\WorkBuddy\2026-09-07-15-05-15\WallpaperDesk\_github-cli-install-20260924")
import verify_motion as V
import verify_p1 as P1

OUT = "C:/Users/Lenovo/AppData/Local/Temp/bench/"

PAGES = [
    ("lib", 0, 0),
    ("set_wall", 1, 0),
    ("set_play", 1, 1),
    ("set_scene", 1, 2),
    ("set_fx", 1, 3),
    ("set_adv", 1, 4),
    ("multi", 2, 0),
]


def shoot(tag, win_page, settings_page):
    P1.pin_page(win_page=win_page, settings_page=settings_page)
    p, hwnd = V.launch()
    try:
        # 起播/解包 VLC 要几秒，状态栏里的 fps、分辨率要等播放器起来才有值；
        # 需要看稳态就用 WALK_WAIT=8 覆盖。
        time.sleep(float(os.environ.get("WALK_WAIT", "2.0")))
        left, top, w, h = V.D.win_rect(hwnd)
        ww, hh, rgb = V.grab(hwnd, 0, 0, w, h)
        name = OUT + "walk_" + tag + ".png"
        V.D.write_png(name, ww, hh, rgb)
        print("saved", tag, ww, hh)
    finally:
        V.stop(p)


if __name__ == "__main__":
    only = set(sys.argv[1:])
    for tag, wp, sp in PAGES:
        if only and tag not in only:
            continue
        shoot(tag, wp, sp)
