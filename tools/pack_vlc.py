#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把 VLC 3.0.x 运行时裁剪、压缩，打包成可内嵌进 WallDesk.exe 的归档文件。

产物（默认写到 src/）：
  VlcBundle.bin   zlib 压缩归档（WDB1 格式），被 Windows 资源脚本引用
  VlcBundle.rc    RCDATA 资源脚本，编译后把上面的归档嵌入 exe

归档布局（全部大端，与 src/VlcBundle.cpp 的解析代码一一对应）：
  u32  magic = 0x57444231 ("WDB1")
  u32  versionLen + version 字节（UTF-8，作为缓存目录指纹）
  u32  entryCount
  逐项：u16 nameLen + name(UTF-8) + u8 flag(0=原样 1=zlib)
        + u32 storedSize + u32 rawSize + 数据

用法：
  python tools/pack_vlc.py --vlc "C:\\Program Files\\VideoLAN\\VLC"
  python tools/pack_vlc.py --vlc D:\\vlc-3.0.21 --version 3.0.21
  python tools/pack_vlc.py --vlc D:\\vlc --no-prune     # 不裁剪，全量打包
  python tools/pack_vlc.py --clean                      # 删除旧的产物
"""

from __future__ import annotations

import argparse
import struct
import sys
import zlib
from pathlib import Path

MAGIC = 0x57444231  # "WDB1"

# 与本地文件播放无关的插件目录，整体排除（体积大头）
EXCLUDE_DIRS = {
    "gui", "lua", "skins2", "visualization", "stream_out", "mux",
    "access_output", "video_splitter", "services_discovery", "control",
    "notify", "ntservice", "hrtfs",
}

# 壁纸场景用不到的视频滤镜，按文件名前缀白名单裁剪（deinterlace 等保留）
VIDEO_FILTER_KEEP = (
    "adjust", "crop", "deinterlace", "fps", "mirror", "rotate",
    "scale", "transform",
)

# 视频输出：保留 Windows 常用后端，剔除 decklink / sdi / caca 等
VIDEO_OUTPUT_KEEP = (
    "direct3d11", "direct3d9", "directdraw", "drawable", "vdummy",
    "vmem", "yuv", "wingdi", "gl", "wgl",
)

FLAG_RAW = 0
FLAG_ZLIB = 1


def plugin_stem(filename: str) -> str:
    """去掉插件文件名的 lib 前缀与 _plugin.dll 后缀。

    VLC 插件实际文件名形如 libdeinterlace_plugin.dll，而白名单写的是 deinterlace，
    不归一化的话所有白名单都会匹配失败，导致整个目录被误裁。
    """
    stem = filename
    if stem.startswith("lib"):
        stem = stem[3:]
    for suffix in ("_plugin.dll", ".dll"):
        if stem.endswith(suffix):
            return stem[: -len(suffix)]
    return stem


def collect_files(vlc_dir: Path, prune: bool) -> list[Path]:
    """收集需要打包的文件。根目录的 dll 全收（MinGW 运行时依赖也在里面），
    plugins 目录按规则裁剪。"""
    for required in ("libvlc.dll", "libvlccore.dll"):
        if not (vlc_dir / required).is_file():
            raise SystemExit(f"错误：{vlc_dir} 下未找到 {required}，请确认指向 VLC 安装目录或解压目录。")

    files: list[Path] = []
    for p in sorted(vlc_dir.glob("*.dll")):
        # 浏览器插件与壁纸无关
        if p.name.lower() in {"axvlc.dll", "npvlc.dll"}:
            continue
        files.append(p)

    plugins = vlc_dir / "plugins"
    if not plugins.is_dir():
        raise SystemExit(f"错误：{vlc_dir} 下未找到 plugins 目录。")

    for p in sorted(plugins.rglob("*")):
        if not p.is_file():
            continue
        rel = p.relative_to(plugins)
        top = rel.parts[0] if len(rel.parts) > 1 else ""
        if top in EXCLUDE_DIRS:
            continue
        # 插件缓存 .dat 让 VLC 首次启动时自行重建：我们删掉了插件，旧缓存反而可能失效
        if p.suffix.lower() == ".dat":
            continue
        if prune:
            stem = plugin_stem(p.name)
            if top == "video_filter" and not stem.startswith(VIDEO_FILTER_KEEP):
                continue
            if top == "video_output" and not stem.startswith(VIDEO_OUTPUT_KEEP):
                continue
        files.append(p)

    return files


def make_fingerprint(vlc_dir: Path, version: str | None) -> str:
    """缓存目录指纹。用户给了版本号就用版本号，否则用 libvlc.dll 的大小+修改时间。"""
    if version:
        return version
    dll = vlc_dir / "libvlc.dll"
    st = dll.stat()
    return f"{st.st_size:x}-{int(st.st_mtime)}"


def write_archive(files: list[Path], root: Path, fingerprint: str, out: Path) -> tuple[int, int]:
    """写入归档，返回 (原始总字节, 落盘总字节)。"""
    blob = bytearray()
    blob += struct.pack(">I", MAGIC)
    raw_fp = fingerprint.encode("utf-8")
    blob += struct.pack(">I", len(raw_fp)) + raw_fp
    blob += struct.pack(">I", len(files))

    raw_total = 0
    stored_total = 0
    for p in files:
        raw = p.read_bytes()
        # 解压端是 Qt 的 qUncompress，其期望 qCompress 私有格式：
        # 4 字节大端未压缩长度 + 标准 zlib 流。裸 zlib.compress 会被判为数据损坏。
        comp = struct.pack(">I", len(raw)) + zlib.compress(raw, 9)
        flag = FLAG_ZLIB if len(comp) < len(raw) else FLAG_RAW
        payload = comp if flag == FLAG_ZLIB else raw
        name = p.relative_to(root).as_posix().encode("utf-8")
        blob += struct.pack(">H", len(name)) + name
        blob += struct.pack(">B", flag)
        blob += struct.pack(">II", len(payload), len(raw))
        blob += payload
        raw_total += len(raw)
        stored_total += len(payload)

    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(bytes(blob))
    return raw_total, stored_total


def write_rc(rc_path: Path, bin_name: str) -> None:
    rc_path.write_text(
        "#define IDR_VLC_BUNDLE 1001\n"
        f"IDR_VLC_BUNDLE RCDATA \"{bin_name}\"\n",
        encoding="utf-8",
    )


def human(n: int) -> str:
    return f"{n / 1024 / 1024:.1f} MB"


def main() -> int:
    parser = argparse.ArgumentParser(description="打包 VLC 运行时为可内嵌归档")
    parser.add_argument("--vlc", help="VLC 安装目录或解压目录（含 libvlc.dll 与 plugins）")
    parser.add_argument("--out", default="src", help="产物输出目录，默认 src")
    parser.add_argument("--version", help="版本指纹，默认由 libvlc.dll 生成")
    parser.add_argument("--no-prune", action="store_true", help="不裁剪插件，全量打包")
    parser.add_argument("--clean", action="store_true", help="删除已有产物后退出")
    args = parser.parse_args()

    out_dir = Path(args.out)
    bin_path = out_dir / "VlcBundle.bin"
    rc_path = out_dir / "VlcBundle.rc"

    if args.clean:
        for p in (bin_path, rc_path):
            if p.exists():
                p.unlink()
                print(f"已删除 {p}")
        return 0

    if not args.vlc:
        parser.error("需要 --vlc 指定 VLC 目录（或用 --clean 清除旧产物）")

    vlc_dir = Path(args.vlc)
    if not vlc_dir.is_dir():
        raise SystemExit(f"错误：{vlc_dir} 不是有效目录。")

    files = collect_files(vlc_dir, not args.no_prune)
    fingerprint = make_fingerprint(vlc_dir, args.version)
    raw_total, stored_total = write_archive(files, vlc_dir, fingerprint, bin_path)
    write_rc(rc_path, bin_path.name)

    print("=" * 56)
    print(f"源目录    : {vlc_dir}")
    print(f"打包文件  : {len(files)} 个")
    print(f"原始体积  : {human(raw_total)}")
    print(f"压缩后    : {human(stored_total)}  (压缩率 {stored_total / max(raw_total, 1) * 100:.0f}%)")
    print(f"版本指纹  : {fingerprint}")
    print(f"归档      : {bin_path}")
    print(f"资源脚本  : {rc_path}")
    print("=" * 56)
    print("下一步：正常构建即可，CMake 检测到 VlcBundle.rc 会自动定义 WALLDESK_EMBED_VLC。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
