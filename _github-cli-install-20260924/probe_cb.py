"""点击复选框后截图，看焦点环/选中表现；顺带滑块 hover 光晕是否被裁。

用法：python probe_cb.py cb|slider
"""
import ctypes
import os
import sys
import time

sys.path.insert(0, r"C:\Users\Lenovo\WorkBuddy\2026-09-07-15-05-15\WallpaperDesk\_github-cli-install-20260924")
import uiautomation as auto
import verify_motion as V
import verify_p1 as P1
import pxtool as PX

OUT = "C:/Users/Lenovo/AppData/Local/Temp/bench/"
user32 = ctypes.windll.user32


def shot(hwnd, name, x, y, w, h, scale=3):
    left, top, winw, winh = V.D.win_rect(hwnd)
    ww, hh, rgb = V.grab(hwnd, 0, 0, winw, winh)
    full = OUT + "_tmp_full.png"
    V.D.write_png(full, ww, hh, rgb)
    PX.crop(full, OUT + name, x, y, w, h, scale=scale)
    print("saved", name)


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "cb"
    P1.pin_page(win_page=1, settings_page=1)
    p, hwnd = V.launch()
    try:
        time.sleep(2.0)
        win = auto.ControlFromHandle(hwnd)
        if mode == "cb":
            cb = None
            for c in win.GetChildren()[0].GetChildren():
                pass
            # 直接按名字找
            def walk(c, d=0):
                if d > 12:
                    return None
                for k in c.GetChildren():
                    try:
                        if k.ControlTypeName == "CheckBoxControl" and "开机自动启动" in (k.Name or ""):
                            return k
                    except Exception:
                        pass
                    r = walk(k, d + 1)
                    if r:
                        return r
                return None
            cb = walk(win)
            if not cb:
                sys.exit("没找到复选框")
            r = cb.BoundingRectangle
            print("checkbox rect", r.left, r.top, r.width(), r.height())
            cx, cy = r.left + r.width() // 2, r.top + r.height() // 2
            for _ in range(3):
                user32.SetCursorPos(cx, cy)
                time.sleep(0.05)
                user32.mouse_event(0x0002, 0, 0, 0, None)
                time.sleep(0.05)
                user32.mouse_event(0x0004, 0, 0, 0, None)
                time.sleep(0.35)
            left, top, _, _ = V.D.win_rect(hwnd)
            shot(hwnd, "z_cb_clicked.png",
                 r.left - left - 40, r.top - top - 14, r.width() + 120, r.height() + 28, 4)
        else:
            s = None

            def walk3(c, d=0):
                if d > 12:
                    return None
                for k in c.GetChildren():
                    try:
                        if k.ControlTypeName == "ComboBoxControl" and "覆盖范围" in (k.Name or ""):
                            return k
                    except Exception:
                        pass
                    rr = walk3(k, d + 1)
                    if rr:
                        return rr
                return None
            s = walk3(win)
            r = s.BoundingRectangle
            print("combo rect", r.left, r.top, r.width(), r.height())
            cx, cy = r.left + r.width() // 2, r.top + r.height() // 2
            user32.SetCursorPos(cx, cy)
            time.sleep(0.3)
            user32.mouse_event(0x0002, 0, 0, 0, None)
            time.sleep(0.05)
            user32.mouse_event(0x0004, 0, 0, 0, None)
            time.sleep(0.6)
            left, top, winw, winh = V.D.win_rect(hwnd)
            # 弹出层是独立顶层窗，抓窗口 DC 抓不到，必须抓整屏（走子进程，argtypes 已设好）
            import subprocess
            full = OUT + "_tmp_screen.png"
            subprocess.run([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)), "desktop_shot.py"), "shot", full],
                           cwd=os.path.dirname(os.path.abspath(__file__)), check=True)
            PX.crop(full, OUT + "z_combo_open.png",
                    left + 340, top + 90, 720, 300, scale=2)
            print("saved z_combo_open.png")
    finally:
        V.stop(p)


if __name__ == "__main__":
    main()
