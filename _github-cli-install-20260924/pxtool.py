"""PNG 缩放 / 裁剪（最近邻），用于验收截图快速查看。

用法：
  python pxtool.py scale <src> <dst> <factor>
  python pxtool.py crop  <src> <dst> <x> <y> <w> <h> [scale]
"""
import sys

import pngtool as P


def load(path):
    w, h, bpp, rows = P.decode(path)
    # decode 返回的行是「已去 filter 的像素行」
    return w, h, bpp, [bytes(r) for r in rows]


def crop(src, dst, x0, y0, cw, ch, scale=1):
    w, h, bpp, rows = load(src)
    out = []
    for y in range(y0, min(y0 + ch, h)):
        r = rows[y]
        line = bytearray()
        for x in range(x0, min(x0 + cw, w)):
            px = r[x * bpp:x * bpp + 3]
            line += px * scale
        for _ in range(scale):
            out.append(b"\x00" + bytes(line))
    P.encode(dst, (min(x0 + cw, w) - x0) * scale, len(out), out)
    print("crop", dst, (min(x0 + cw, w) - x0) * scale, len(out))


def scale(src, dst, factor):
    w, h, bpp, rows = load(src)
    step = max(1, int(round(1 / factor)))
    out = []
    for y in range(0, h, step):
        r = rows[y]
        out.append(b"\x00" + bytes(b"".join(r[x * bpp:x * bpp + 3] for x in range(0, w, step))))
    P.encode(dst, len(out[0]) // 3 - 0, len(out), out)
    print("scale", dst, len(out[0]) - 1, len(out))


if __name__ == "__main__":
    cmd = sys.argv[1]
    if cmd == "crop":
        crop(sys.argv[2], sys.argv[3], *[int(v) for v in sys.argv[4:8]],
             scale=int(sys.argv[8]) if len(sys.argv) > 8 else 1)
    else:
        scale(sys.argv[2], sys.argv[3], float(sys.argv[4]))
