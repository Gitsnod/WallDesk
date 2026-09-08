#!/usr/bin/env python3
"""截取 WallDesk 主窗口，用于人工核对 UI 布局。"""
import ctypes
import ctypes.wintypes as wt
import struct
import sys
import zlib

user32 = ctypes.WinDLL('user32')
gdi32 = ctypes.WinDLL('gdi32')

try:
    user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
except Exception:
    user32.SetProcessDPIAware()


def find_main_window():
    found = []

    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def cb(hwnd, lp):
        if not user32.IsWindowVisible(hwnd):
            return True
        length = user32.GetWindowTextLengthW(hwnd)
        if length == 0:
            return True
        buf = ctypes.create_unicode_buffer(length + 1)
        user32.GetWindowTextW(hwnd, buf, length + 1)
        if 'WallDesk' in buf.value:
            rect = wt.RECT()
            user32.GetWindowRect(hwnd, ctypes.byref(rect))
            w = rect.right - rect.left
            h = rect.bottom - rect.top
            if w > 400 and h > 300:
                found.append((hwnd, buf.value, w, h))
        return True

    user32.EnumWindows(cb, 0)
    return found


def save_png(path, w, h, rows_rgb):
    raw = b''.join(b'\x00' + rows_rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))

    def chunk(tag, data):
        return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data))

    png = (b'\x89PNG\r\n\x1a\n'
           + chunk(b'IHDR', struct.pack('>II', w, h) + bytes((8, 2, 0, 0, 0)))
           + chunk(b'IDAT', zlib.compress(raw, 6))
           + chunk(b'IEND', b''))
    with open(path, 'wb') as f:
        f.write(png)
    return path


def main():
    wins = find_main_window()
    if not wins:
        print('未找到 WallDesk 主窗口（可能已最小化到托盘）')
        return 1
    hwnd, title, w, h = wins[0]
    print(f'窗口: {title} {w}x{h}')

    user32.ShowWindow(hwnd, 9)  # SW_RESTORE
    user32.SetForegroundWindow(hwnd)
    import time
    time.sleep(0.6)

    hdc = user32.GetWindowDC(hwnd)
    mem = gdi32.CreateCompatibleDC(hdc)
    bmp = gdi32.CreateCompatibleBitmap(hdc, w, h)
    gdi32.SelectObject(mem, bmp)
    gdi32.BitBlt(mem, 0, 0, w, h, hdc, 0, 0, 0x00CC0020)

    class BIH(ctypes.Structure):
        _fields_ = [('biSize', wt.DWORD), ('biWidth', ctypes.c_int), ('biHeight', ctypes.c_int),
                    ('biPlanes', wt.WORD), ('biBitCount', wt.WORD), ('biCompression', wt.DWORD),
                    ('biSizeImage', wt.DWORD), ('biXPelsPerMeter', ctypes.c_long),
                    ('biYPelsPerMeter', ctypes.c_long), ('biClrUsed', wt.DWORD),
                    ('biClrImportant', wt.DWORD)]

    bi = BIH()
    bi.biSize = ctypes.sizeof(BIH)
    bi.biWidth, bi.biHeight = w, -h
    bi.biPlanes, bi.biBitCount, bi.biCompression = 1, 32, 0
    bi.biSizeImage = w * h * 4
    buf = (ctypes.c_char * (w * h * 4))()
    gdi32.GetDIBits(mem, bmp, 0, h, buf, ctypes.byref(bi), 0)
    raw = bytes(buf)

    out = bytearray()
    for y in range(h):
        line = bytearray(raw[y * w * 4:(y + 1) * w * 4])
        del line[3::4]               # 去掉 alpha
        line[0::3], line[2::3] = line[2::3], line[0::3]  # BGR -> RGB
        out += line

    out_path = sys.argv[1] if len(sys.argv) > 1 else r'C:\Users\Lenovo\AppData\Local\Temp\walldesk_ui.png'
    save_png(out_path, w, h, bytes(out))
    print(f'已保存: {out_path}')

    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(mem)
    user32.ReleaseDC(hwnd, hdc)
    return 0


if __name__ == '__main__':
    sys.exit(main())
