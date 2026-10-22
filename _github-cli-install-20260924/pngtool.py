#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""极简 PNG 工具：解码 / 取像素 / 半尺寸导出。无第三方依赖。

用法：
  pngtool.py info  <png>
  pngtool.py px    <png> x,y [x,y ...]
  pngtool.py row   <png> y [x0] [x1]        打印一行内的游程（RLE）颜色
  pngtool.py half  <in> <out> [factor]
"""
import struct
import sys
import zlib


def decode(path):
    d = open(path, "rb").read()
    w, h = struct.unpack(">II", d[16:24])
    bit_depth, color_type = d[24], d[25]
    pos, idat = 8, b""
    while pos < len(d):
        ln = struct.unpack(">I", d[pos:pos + 4])[0]
        tag = d[pos + 4:pos + 8]
        if tag == b"IDAT":
            idat += d[pos + 8:pos + 8 + ln]
        pos += 12 + ln
    raw = zlib.decompress(idat)
    if bit_depth != 8 or color_type not in (2, 6):
        raise SystemExit(f"不支持 bit={bit_depth} color={color_type}")
    bpp = 3 if color_type == 2 else 4
    stride = w * bpp
    rows = []
    prev = bytearray(stride)
    i = 0
    for _ in range(h):
        f = raw[i]
        i += 1
        line = bytearray(raw[i:i + stride])
        i += stride
        if f == 1:
            for x in range(bpp, stride):
                line[x] = (line[x] + line[x - bpp]) & 255
        elif f == 2:
            for x in range(stride):
                line[x] = (line[x] + prev[x]) & 255
        elif f == 3:
            for x in range(stride):
                a = line[x - bpp] if x >= bpp else 0
                line[x] = (line[x] + ((a + prev[x]) >> 1)) & 255
        elif f == 4:
            for x in range(stride):
                a = line[x - bpp] if x >= bpp else 0
                b = prev[x]
                c = prev[x - bpp] if x >= bpp else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 255
        rows.append(bytes(line))
        prev = line
    return w, h, bpp, rows


def encode(path, w, h, rows):
    def chunk(tag, d):
        return (struct.pack(">I", len(d)) + tag + d
                + struct.pack(">I", zlib.crc32(tag + d) & 0xFFFFFFFF))
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(b"".join(rows), 6))
           + chunk(b"IEND", b""))
    open(path, "wb").write(png)
    return len(png)


def px(w, h, bpp, rows, x, y):
    o = x * bpp
    r = rows[y]
    return r[o], r[o + 1], r[o + 2]


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    cmd, src = sys.argv[1], sys.argv[2]
    w, h, bpp, rows = decode(src)
    if cmd == "info":
        print(f"{src} {w}x{h} bpp={bpp}")
    elif cmd == "px":
        for spec in sys.argv[3:]:
            x, y = [int(v) for v in spec.split(",")]
            print(f"({x},{y}) = {px(w, h, bpp, rows, x, y)}")
    elif cmd == "row":
        y = int(sys.argv[3])
        x0 = int(sys.argv[4]) if len(sys.argv) > 4 else 0
        x1 = int(sys.argv[5]) if len(sys.argv) > 5 else w
        cur, start = None, x0
        for x in range(x0, x1):
            c = px(w, h, bpp, rows, x, y)
            if c != cur:
                if cur is not None and x - start >= 3:
                    print(f"  x{start}..{x - 1} ({x - start}px) {cur}")
                cur, start = c, x
        print(f"  x{start}..{x1 - 1} {cur}")
    elif cmd == "half":
        dst = sys.argv[3]
        f = int(sys.argv[4]) if len(sys.argv) > 4 else 2
        nw, nh = w // f, h // f
        out = []
        for y in range(nh):
            r = rows[y * f]
            out.append(b"\x00" + bytes(
                b for x in range(nw) for b in r[x * f * bpp:x * f * bpp + 3]))
        n = encode(dst, nw, nh, out)
        print(f"{dst} {nw}x{nh} {n}B")
    return 0


if __name__ == "__main__":
    sys.exit(main())
