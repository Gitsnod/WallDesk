"""生成测试用未压缩 AVI：320x180，10 fps，白色方块横向移动 + 帧号。

无第三方依赖（结构体手写 AVI），供壁纸冻结/播放的截图对比验证。
"""
import struct
import sys

W, H, FPS, FRAMES = 320, 180, 10, 120  # 12 秒，循环播放足够覆盖测试窗口


def make_frame(i):
    """BGRA 底、白色方块按帧移动、左上角帧号条。"""
    buf = bytearray(W * H * 3)
    x0 = (i * 6) % (W - 40)
    for y in range(H):
        row = y * W * 3
        for x in range(W):
            o = row + x * 3
            if x0 <= x < x0 + 40 and 60 <= y < 100:
                buf[o] = buf[o + 1] = buf[o + 2] = 255
            elif (x // 8 + y // 8 + i // 3) % 7 == 0:
                buf[o] = 40
                buf[o + 1] = 60
                buf[o + 2] = 120
            else:
                buf[o] = 16
                buf[o + 1] = 20
                buf[o + 2] = 48
    # 帧号：前 20 行按二进制明暗，肉眼可辨进度
    for bit in range(8):
        if (i >> bit) & 1:
            for y in range(2 + bit * 2, 4 + bit * 2):
                for x in range(W - 40, W - 20):
                    o = (y * W + x) * 3
                    buf[o] = buf[o + 1] = buf[o + 2] = 200
    return bytes(buf)


def chunk(tag, data):
    if len(data) % 2:
        data += b"\x00"
    return tag + struct.pack("<I", len(data)) + data


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "preview_test.avi"
    frames = [make_frame(i) for i in range(FRAMES)]
    frame_size = W * H * 3

    avih = struct.pack(
        "<IIIIIIIIIIIIII",
        1000000 // FPS,  # dwMicroSecPerFrame
        frame_size * FPS,  # dwMaxBytesPerSec
        0, 0,
        FRAMES,  # dwTotalFrames
        0, 1, frame_size, W, H, 0, 0, 0, 0)

    strh = struct.pack(
        "<4s4sIHHIIIIIIII4s",
        b"vids", b"DIB ", 0x0000, 0, 0,
        0, 1, FPS,
        0, FRAMES, frame_size, 0, 0,
        struct.pack("<HHHH", 0, 0, W, H))
    strf = struct.pack(
        "<IiiHHIIiiII",
        40, W, H, 1, 24, 0, frame_size, 0, 0, 0, 0)

    strl = chunk(b"LIST", chunk(b"strh", strh) + chunk(b"strf", strf))
    hdrl = chunk(b"LIST", chunk(b"avih", avih) + strl)

    movi = b"".join(chunk(b"00dc", f) for f in frames)
    movi = chunk(b"LIST", movi)

    # idx1：偏移相对 'movi' 数据起点（RIFF 后第 4 字节起算）
    entries = []
    offset = 4  # 'LIST' + size
    for f in frames:
        entries.append(struct.pack("<4sIII", b"00dc", 0x10, offset, len(f)))
        offset += 8 + len(f) + (len(f) % 2)

    body = chunk(b"LIST", b"AVI " + hdrl + movi) + chunk(b"idx1", b"".join(entries))
    with open(out, "wb") as fp:
        fp.write(b"RIFF" + struct.pack("<I", len(body)) + body)
    print(f"已生成 {out}：{W}x{H} @ {FPS}fps，{FRAMES} 帧，{frame_size} B/帧")


if __name__ == "__main__":
    main()
