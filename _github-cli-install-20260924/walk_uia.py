"""用 UIA 枚举各页控件几何，找高度/间距不一致。

用法：python walk_uia.py <winPage> <settingsPage>
"""
import sys
import time

sys.path.insert(0, r"C:\Users\Lenovo\WorkBuddy\2026-09-07-15-05-15\WallpaperDesk\_github-cli-install-20260924")
import uiautomation as auto
import verify_motion as V
import verify_p1 as P1

WANT = {"ButtonControl", "ComboBoxControl", "EditControl", "CheckBoxControl",
        "RadioButtonControl", "SliderControl", "SpinnerControl", "ListControl",
        "ListItemControl", "GroupControl", "TextControl", "TabItemControl"}


def collect(ctrl, depth, out):
    if depth > 14:
        return
    try:
        kids = ctrl.GetChildren()
    except Exception:
        return
    for c in kids:
        try:
            ct = c.ControlTypeName
        except Exception:
            continue
        if ct in WANT:
            try:
                r = c.BoundingRectangle
                out.append((ct, c.Name, r.left, r.top, r.width(), r.height()))
            except Exception:
                pass
        collect(c, depth + 1, out)


def main():
    wp, sp = (int(sys.argv[1]), int(sys.argv[2])) if len(sys.argv) > 2 else (0, 0)
    P1.pin_page(win_page=wp, settings_page=sp)
    p, hwnd = V.launch()
    try:
        time.sleep(1.4)
        win = auto.ControlFromHandle(hwnd)
        out = []
        collect(win, 0, out)
        left, top, w, h = V.D.win_rect(hwnd)
        print("window", left, top, w, h, "controls", len(out))
        for ct, name, x, y, cw, ch in out:
            if ct == "TextControl" and ch > 26:
                continue
            print("%-18s %4d,%4d %4dx%-3d  %s" % (ct, x - left, y - top, cw, ch, name[:40]))
    finally:
        V.stop(p)


if __name__ == "__main__":
    main()
