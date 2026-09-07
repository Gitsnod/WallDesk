#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""生成 WallDesk 应用图标 resources/app.ico（纯标准库，无需 Pillow）。

绘制内容与 MainWindow::appIcon() 保持一致：圆角蓝底 + 浅色画面 + 深色任务条，
即「一个显示器里放着一张图」的意象。

用法：
    python tools/make_icon.py                 # 生成 resources/app.ico
    python tools/make_icon.py --out path.ico  # 指定输出
    python tools/make_icon.py --preview       # 额外导出 256 尺寸 PNG 便于肉眼检查

实现要点：
  * 图形用有向距离场（SDF）求值 + 3x3 超采样，避免锯齿；
  * 每个尺寸编码为 PNG 后写入 ICO 容器（Vista 及以上支持 PNG 条目，可省去 BMP 的 AND 掩码）；
  * PNG 用 zlib 手工编码（IHDR / IDAT / IEND），无第三方依赖。
"""
import argparse
import math
import os
import struct
import sys
import zlib

SIZES = (16, 32, 48, 64, 128, 256)
BG_TOP = (0x3B, 0x82, 0xF6)     # 背景渐变起色
BG_BOTTOM = (0x1D, 0x4E, 0xD8)  # 背景渐变止色
SCREEN = (0x93, 0xC5, 0xFD)     # 画面（浅蓝）
STAND = (0x1E, 0x3A, 0x8A)      # 任务条（深蓝）

# 归一化几何：与 MainWindow::appIcon() 的 64x64 手绘比例一致
RECT_OUTER = (0.0, 0.0, 1.0, 1.0, 0.22)
RECT_SCREEN = (0.156, 0.1875, 0.844, 0.594, 0.09)
RECT_STAND = (0.25, 0.6875, 0.75, 0.8125, 0.03)


def rect_coverage(px, py, rect, size):
    """返回该点落在圆角矩形内的覆盖率（0~1），边沿用 1 像素宽过渡实现抗锯齿。"""
    x0, y0, x1, y1, rr = rect
    x0 *= size; x1 *= size; y0 *= size; y1 *= size; rr *= size
    cx = (x0 + x1) / 2.0
    cy = (y0 + y1) / 2.0
    hx = max((x1 - x0) / 2.0 - rr, 0.0)
    hy = max((y1 - y0) / 2.0 - rr, 0.0)
    dx = max(abs(px - cx) - hx, 0.0)
    dy = max(abs(py - cy) - hy, 0.0)
    dist = math.hypot(dx, dy) - rr
    return max(0.0, min(1.0, 0.5 - dist))


def blend(dst, src, alpha):
    """src over dst，dst/src 为 (r, g, b)，返回新的 (r, g, b)。"""
    return tuple(int(round(s * alpha + d * (1.0 - alpha))) for d, s in zip(dst, src))


def render(size, samples=3):
    """渲染 size×size 的 RGBA 像素，返回 bytes（每行 size*4 字节）。"""
    offset = (samples - 1) / (2.0 * samples)
    step = 1.0 / samples
    row_bytes = bytearray()
    for y in range(size):
        row_bytes.append(0)  # PNG 过滤器类型 0（None）
        for x in range(size):
            acc = [0.0, 0.0, 0.0, 0.0]
            for sy in range(samples):
                for sx in range(samples):
                    px = x + offset + sx * step
                    py = y + offset + sy * step
                    color = [0.0, 0.0, 0.0]
                    alpha = 0.0
                    # 自下而上合成：背景 -> 画面 -> 任务条
                    cov = rect_coverage(px, py, RECT_OUTER, size)
                    if cov > 0:
                        t = py / float(size)
                        grad = tuple(BG_TOP[i] + (BG_BOTTOM[i] - BG_TOP[i]) * t for i in range(3))
                        color = [grad[0], grad[1], grad[2]]
                        alpha = cov
                    cov = rect_coverage(px, py, RECT_SCREEN, size)
                    if cov > 0:
                        color = list(blend(tuple(color), SCREEN, cov))
                        alpha = max(alpha, cov)
                    cov = rect_coverage(px, py, RECT_STAND, size)
                    if cov > 0:
                        color = list(blend(tuple(color), STAND, cov))
                        alpha = max(alpha, cov)
                    weight = 1.0 / (samples * samples)
                    for i in range(3):
                        acc[i] += color[i] * weight
                    acc[3] += alpha * weight
            # 颜色按覆盖率预乘后再还原，保证边缘与透明背景混合时不出黑边
            a = acc[3]
            if a > 0:
                r, g, b = (min(255, max(0, acc[i] / a)) for i in range(3))
            else:
                r = g = b = 0
            row_bytes += bytes((int(r), int(g), int(b), int(round(a * 255))))
    return bytes(row_bytes)


def png_bytes(size):
    raw = render(size)

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    header = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)  # 8 位 RGBA
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", header)
            + chunk(b"IDAT", zlib.compress(raw, 9))
            + chunk(b"IEND", b""))


def ico_bytes(sizes=SIZES):
    images = [(size, png_bytes(size)) for size in sizes]
    out = bytearray(struct.pack("<HHH", 0, 1, len(images)))
    offset = 6 + 16 * len(images)
    for size, data in images:
        # 边长 256 在 ICO 目录里记为 0
        out += struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32,
                           len(data), offset)
        offset += len(data)
    for _, data in images:
        out += data
    return bytes(out)


def main():
    parser = argparse.ArgumentParser(description="生成 WallDesk 图标")
    parser.add_argument("--out", default=os.path.join("resources", "app.ico"),
                        help="输出 ico 路径（默认 resources/app.ico）")
    parser.add_argument("--preview", action="store_true",
                        help="额外导出 256 尺寸的 PNG 便于检查")
    args = parser.parse_args()

    data = ico_bytes()
    out_dir = os.path.dirname(os.path.abspath(args.out))
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)
    with open(args.out, "wb") as f:
        f.write(data)
    print(f"已生成 {args.out}（{len(data) / 1024:.1f} KB，尺寸 {', '.join(map(str, SIZES))}）")

    if args.preview:
        preview = os.path.join(out_dir, "app_preview.png")
        with open(preview, "wb") as f:
            f.write(png_bytes(256))
        print(f"已导出预览 {preview}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
