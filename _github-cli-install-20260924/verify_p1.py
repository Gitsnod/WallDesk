"""P1 动效验收：复选框勾绘 + 菜单淡入。

判据（docs/ui-interaction-plan-2026-10-22.md §6 P1）：
  1. 复选框切换后约 60ms，indicator 左上角纯色区应落在
     「未选中底色 card」与「accent」之间（离两端都有显著距离）＝有过渡中间态；
  2. 菜单弹出后 100ms 内应出现与终态不同的混合态（非瞬开）。

环境限制（P0 已记录，别再误判成产品缺陷）：
  - SetCursorPos / SendInput 移动指针**不产生** WM_MOUSEMOVE，动画类交互
    必须 SendMessage 注入，且真实光标停在同一坐标；
  - PrintWindow 对 Qt 窗口返回旧帧，抓帧一律屏幕 DC BitBlt。
"""

import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import verify_motion as V  # noqa: E402

INI = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                   "..", "build", "data", "WallDesk", "WallDesk.ini")


def pin_page(win_page=1, settings_page=1):
    """固定启动落点：主界面 → 设置 → 播放子页（那里有几个默认勾选的复选框）。

    不这样做的话每次启动停在哪个子页取决于上次退出位置，探针会找不到目标。
    """
    path = os.path.normpath(INI)
    with open(path, encoding="utf-8") as fh:
        text = fh.read()
    text = re.sub(r"^winPage=\d+", "winPage=%d" % win_page, text, flags=re.M)
    text = re.sub(r"^settingsPage=\d+", "settingsPage=%d" % settings_page,
                  text, flags=re.M)
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(text)


def find_checkboxes(hwnd):
    """找 accent 填充的小方块 = 已勾选的复选框 indicator。

    indicator 中间有白色勾，会把蓝色块切成两半；先做两轮 3x3 膨胀
    把勾的缝隙闭合，再取连通域，否则连通块尺寸不足、永远匹配不到。
    """
    w, h, rgb = V.grab(hwnd, 0, 0, *V.D.win_rect(hwnd)[2:4])
    stride = w * 3 + 1
    mask = bytearray(w * h)
    for y in range(h):
        o = y * stride + 1
        row = rgb[o:o + w * 3]
        for x in range(w):
            i = x * 3
            for ar, ag, ab in V.ACCENTS:
                if abs(row[i] - ar) < 14 and abs(row[i + 1] - ag) < 14 \
                        and abs(row[i + 2] - ab) < 14:
                    mask[y * w + x] = 1
                    break

    def dilate(m):
        out = bytearray(w * h)
        for y in range(h):
            base = y * w
            for x in range(w):
                if m[base + x]:
                    out[base + x] = 1
                    if x > 0:
                        out[base + x - 1] = 1
                    if x < w - 1:
                        out[base + x + 1] = 1
        m2 = bytearray(out)
        for y in range(1, h - 1):
            b = y * w
            for x in range(w):
                if out[b + x]:
                    m2[b - w + x] = 1
                    m2[b + w + x] = 1
        return m2

    mask = dilate(dilate(mask))

    seen = bytearray(w * h)
    out = []
    for y in range(h):
        for x in range(w):
            i = y * w + x
            if not mask[i] or seen[i]:
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
                x0 = min(x0, jx)
                x1 = max(x1, jx)
                y0 = min(y0, jy)
                y1 = max(y1, jy)
                for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    ny, nx = jy + dy, jx + dx
                    if 0 <= ny < h and 0 <= nx < w:
                        k = ny * w + nx
                        if mask[k] and not seen[k]:
                            seen[k] = 1
                            stack.append(k)
            bw, bh = x1 - x0 + 1, y1 - y0 + 1
            if 20 <= bw <= 32 and 20 <= bh <= 32 and abs(bw - bh) <= 4 \
                    and area > bw * bh * 0.55:
                # 膨胀让外框各多 2px，内缩回来得到 indicator 实际位置
                out.append((x0 + 2, y0 + 2, bw - 4, bh - 4))
    out.sort(key=lambda t: (t[1], t[0]))
    return out


def mean_of(hwnd, rect):
    w, h, rgb = V.grab(hwnd, *rect)
    r, g, b, _ = V.region_stats(rgb, w, h)
    return (r, g, b)


def dist(a, b):
    return sum((x - y) ** 2 for x, y in zip(a, b)) ** 0.5


def click(hwnd, ox, oy, wx, wy):
    """真实光标就位 + 消息注入按压。

    wx/wy 是窗口相对坐标（与 grab 同一坐标系）；屏幕坐标 = 窗口 left/top + 偏移，
    客户区坐标（WM_MOUSEMOVE 的 lParam）= 窗口相对坐标 - client_origin。
    """
    left, top, _w, _h = V.D.win_rect(hwnd)
    V.user32.SetCursorPos(left + wx, top + wy)
    time.sleep(0.15)
    pump = V.Pump(hwnd, wx - ox, wy - oy)
    pump.start()
    time.sleep(0.2)
    V.press(hwnd, wx - ox, wy - oy, True)
    time.sleep(0.045)
    pump.close()
    V.press(hwnd, wx - ox, wy - oy, False)


def sample_switch(hwnd, probe, wx, wy, ox, oy, label):
    """点一下并连续采样 probe 区域，返回 [(ms, rgb)]。"""
    click(hwnd, ox, oy, wx, wy)
    t0 = time.perf_counter()
    series = []
    for _ in range(18):
        series.append(((time.perf_counter() - t0) * 1000, mean_of(hwnd, probe)))
    print("  %s 采样：" % label)
    for ms, col in series:
        print("    %6.1fms  %s" % (ms, tuple(round(v, 1) for v in col)))
    return series


def test_checkbox(hwnd, ox, oy):
    boxes = find_checkboxes(hwnd)
    print("已勾选复选框（原始）：", boxes[:8], "共 %d 个" % len(boxes))
    # 排除标题栏图标与左侧导航高亮；只留内容区里 21px 见方的 indicator
    boxes = [b for b in boxes if b[0] > 300 and b[1] > 300
             and b[2] >= 20 and b[3] >= 20]
    print("内容区候选：", boxes)
    if not boxes:
        print("结果：未找到复选框（页面落点不对）")
        return False

    bx, by, bw, bh = boxes[0]
    # 左上角 6x6 是纯 accent（勾在中间），拿它做纯度判据
    probe = (bx + 2, by + 2, 6, 6)
    cx, cy = bx + bw // 2, by + bh // 2

    base_on = mean_of(hwnd, probe)
    print("起点（已勾选）accent 区平均色 %s" % (tuple(round(v, 1) for v in base_on),))

    series = sample_switch(hwnd, probe, cx, cy, ox, oy, "取消勾选")
    time.sleep(0.5)
    base_off = mean_of(hwnd, probe)
    total = dist(base_on, base_off)
    print("终点（未勾选）%s；两端距离 %.1f"
          % (tuple(round(v, 1) for v in base_off), total))

    ok = False
    if total < 8.0:
        print("  两端几乎同色（%.1f）⇒ 点击没落到复选框上，本次不计入" % total)
        return False
    for ms, col in series:
        if 35 <= ms <= 115:
            d1, d2 = dist(col, base_on), dist(col, base_off)
            mid = d1 > total * 0.15 and d2 > total * 0.15
            print("  判据窗口 %.0fms：距起点 %.1f、距终点 %.1f → %s"
                  % (ms, d1, d2, "中间态" if mid else "接近端点"))
            ok = ok or mid

    # 反向再点一次（勾回），确认不是单向偶然
    time.sleep(0.25)
    series2 = sample_switch(hwnd, probe, cx, cy, ox, oy, "重新勾选")
    time.sleep(0.4)
    back = mean_of(hwnd, probe)
    ok2 = any(35 <= ms <= 115 and dist(c, base_off) > total * 0.15
              and dist(c, base_on) > total * 0.15 for ms, c in series2)
    print("复原后颜色 %s（应回到起点附近）" % (tuple(round(v, 1) for v in back),))

    print("结果：取消勾选过渡=%s，勾选过渡=%s" % (ok, ok2))
    return ok and ok2


def menu_probe(hwnd, ox, oy):
    """F10 打开菜单栏 → 采样弹层区域是否经过半透明中间态。"""
    left, top, _w, _h = V.D.win_rect(hwnd)
    V.user32.SetCursorPos(left + 4, top + 4)
    V.user32.SendMessageW(hwnd, 0x0100, 0x79, 0x00010001)
    V.user32.SendMessageW(hwnd, 0x0101, 0x79, 0xC0010001)
    time.sleep(0.35)
    popups = [(h, cls, t, r) for (h, cls, t, r) in V.D.my_windows()
              if h != hwnd and V.user32.IsWindowVisible(h)
              and r[2] > 60 and r[3] > 40]
    print("  F10 后可见弹窗：", [(c, t[:16], r) for (_h, c, t, r) in popups])
    if not popups:
        return False

    # 关掉菜单重新打开，边开边采样
    V.user32.SendMessageW(hwnd, 0x0100, 0x1B, 0x00010001)
    time.sleep(0.4)
    V.user32.SendMessageW(hwnd, 0x0100, 0x79, 0x00010001)
    V.user32.SendMessageW(hwnd, 0x0101, 0x79, 0xC0010001)
    t0 = time.perf_counter()
    samples = []
    for _ in range(16):
        live = [(h, cls, t, r) for (h, cls, t, r) in V.D.my_windows()
                if h != hwnd and V.user32.IsWindowVisible(h)
                and r[2] > 60 and r[3] > 40]
        if live:
            r = live[0][3]
            samples.append(((time.perf_counter() - t0) * 1000,
                            mean_of(hwnd, (r[0], r[1], r[2], r[3]))))
    print("  菜单区域采样：")
    for ms, col in samples:
        print("    %6.1fms  %s" % (ms, tuple(round(v, 1) for v in col)))
    if len(samples) < 3:
        return False
    finals = samples[-1][1]
    early = [c for ms, c in samples if ms < 120]
    changed = any(dist(c, finals) > 6 for c in early)
    print("  早期样本与终态差异：", "有（非瞬开）" if changed else "无")
    V.user32.SendMessageW(hwnd, 0x0100, 0x1B, 0x00010001)
    return changed


def main():
    pin_page()
    p, hwnd = V.launch()
    ox, oy = V.client_origin(hwnd)
    try:
        time.sleep(1.4)
        print("=== 复选框勾绘（120ms）===")
        ok_check = test_checkbox(hwnd, ox, oy)
        print("=== 菜单淡入（200ms）===")
        ok_menu = menu_probe(hwnd, ox, oy)
        print("总结：复选框=%s 菜单=%s" % (ok_check, ok_menu))
    finally:
        V.stop(p)


if __name__ == "__main__":
    main()
