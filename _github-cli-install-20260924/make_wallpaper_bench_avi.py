# -*- coding: utf-8 -*-
"""生成壁纸渲染路径基准测量用的测试 AVI（未压缩 BGR24）。

用法：
    python make_wallpaper_bench_avi.py [--w 宽] [--h 高] [--frames 帧数]
                                        [--fps 帧率] [--out 输出路径]

不传宽高时自动按「DPI 感知下的物理虚拟桌面」分辨率生成，
与壁纸宿主窗口的实际渲染像素数一致。

画面：竖向漂移渐变底 + 横向移动白色亮块 + 左上角二进制帧号，
保证每帧都与前后帧不同（用于帧率 cadence 统计）。
结构体手写 AVI，无第三方依赖；与 make_test_avi.py 同格式。
"""
import argparse
import ctypes
import ctypes.wintypes as w
import struct
import sys
import time


def physical_desktop():
    """DPI 感知进程下的物理虚拟桌面尺寸。"""
    try:
        ctypes.windll.user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))  # PER_MONITOR_AWARE_V2
    except Exception:
        ctypes.windll.user32.SetProcessDPIAware()
    u32 = ctypes.windll.user32
    return (u32.GetSystemMetrics(78), u32.GetSystemMetrics(79))


def make_frame(W, H, i):
    """行切片快速生成一帧：渐变底随帧漂移 + 移动亮块 + 帧号块。"""
    buf = bytearray(W * H * 3)

    # 渐变底：每行一个颜色，行相位随帧偏移（1080 行 × 1 次 bytes 赋值）
    row_tmpl = bytearray(W * 3)
    for y in range(H):
        v = (y + i * 4) % 256
        # 暗蓝灰底，带一点竖向明暗
        r = 10 + (v >> 5)
        g = 14 + (v >> 4)
        b = 40 + (v >> 3)
        row_tmpl[0::3] = bytes([r]) * W
        row_tmpl[1::3] = bytes([g]) * W
        row_tmpl[2::3] = bytes([b]) * W
        buf[y * W * 3:(y + 1) * W * 3] = row_tmpl

    # 横向移动的白色亮块（每行一次切片赋值）
    x0 = (i * 37) % (W - 200)
    white = b"\xff" * (200 * 3)
    for y in range(H // 4, H // 4 + H // 2):
        buf[y * W * 3 + x0 * 3: y * W * 3 + (x0 + 200) * 3] = white

    # 左上角帧号：8 个 24x24 小块表示二进制位，随帧变化
    for bit in range(8):
        if (i >> bit) & 1:
            y0 = 8 + bit * 32
            buf[y0 * W * 3: (y0 + 24) * W * 3] = b"\x00" * (W * 3)
            for y in range(y0, y0 + 24):
                buf[y * W * 3 + 8 * 3: y * W * 3 + 32 * 3] = b"\xc8\xc8\xc8" * 24
    return bytes(buf)


def chunk(tag, data):
    if len(data) % 2:
        data += b"\x00"
    return tag + struct.pack("<I", len(data)) + data


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--w", type=int, default=0)
    ap.add_argument("--h", type=int, default=0)
    ap.add_argument("--frames", type=int, default=60)
    ap.add_argument("--fps", type=int, default=10)
    ap.add_argument("--out", default="bench_test.avi")
    a = ap.parse_args()

    W, H = a.w, a.h
    if W <= 0 or H <= 0:
        W, H = physical_desktop()
    FPS, FRAMES = a.fps, a.frames

    t0 = time.time()
    with open(a.out, "wb") as fp:
        frame_size = W * H * 3
        avih = struct.pack(
            "<IIIIIIIIIIIIII",
            1000000 // FPS, frame_size * FPS, 0, 0,
            FRAMES, 0, 1, frame_size, W, H, 0, 0, 0, 0)
        strh = struct.pack(
            "<4s4sIHHIIIIIIII4s",
            b"vids", b"DIB ", 0x0000, 0, 0,
            0, 1, FPS, 0, FRAMES, frame_size, 0, 0,
            struct.pack("<HHHH", 0, 0, W, H))
        strf = struct.pack("<IiiHHIIiiII",
                           40, W, H, 1, 24, 0, frame_size, 0, 0, 0, 0)
        strl = chunk(b"LIST", chunk(b"strh", strh) + chunk(b"strf", strf))
        hdrl = chunk(b"LIST", chunk(b"avih", avih) + strl)

        movi_parts = []
        for i in range(FRAMES):
            f = make_frame(W, H, i)
            movi_parts.append(chunk(b"00dc", f))
        movi = chunk(b"LIST", b"".join(movi_parts))

        entries = []
        offset = 4
        for f in movi_parts:
            entries.append(struct.pack("<4sIII", b"00dc", 0x10, offset, len(f) - 8))
            offset += len(f)

        body = chunk(b"LIST", b"AVI " + hdrl + movi) + chunk(b"idx1", b"".join(entries))
        fp.write(b"RIFF" + struct.pack("<I", len(body)) + body)

    print(f"已生成 {a.out}：{W}x{H} @ {FPS}fps，{FRAMES} 帧，"
          f"{frame_size / 2**20:.1f} MB/帧，总 {frame_size * FRAMES / 2**20:.0f} MB，"
          f"耗时 {time.time() - t0:.1f}s")


if __name__ == "__main__":
    main()
