#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""WallDesk 发布打包工具：把构建产物整理成可分发给其他用户的发布包。

流程：
  1. 定位构建出的 WallDesk.exe（支持单配置与多配置生成器目录布局）
  2. 部署 Qt 运行时依赖：默认调 windeployqt；windeployqt 不可用时用 --deploy-from 从
     一份已部署好的目录（例如 build/）拷贝
  3. 复制 LICENSE / README / 第三方许可声明
  4. 生成 BUILDINFO.txt（版本、时间、校验）与 SHA256SUMS.txt
  5. 可选打成 zip：dist/WallDesk-<版本>-win64.zip

用法（在工程根目录）：
    python tools/make_release.py --qt-bin "C:\\Qt\\6.8.3\\mingw_64\\bin"
    python tools/make_release.py --qt-bin <路径> --no-zip          # 只要目录
    python tools/make_release.py --qt-bin <路径> --keep-opengl-sw  # 保留软件渲染库
    python tools/make_release.py --deploy-from build               # 跳过 windeployqt

windeployqt 解析顺序：--qt-bin 指定的目录 -> 环境变量 QTDIR -> PATH。

--- 关于 --deploy-from（2026-09-24 加）---
本开发机上 windeployqt 无法工作：它启动子进程 qtpaths 时失败，稳定报
    Unable to query qtpaths: Error running binary qtpaths: pipe:
POSIX / Windows 两种 PATH 形态、带不带 PATH、沙箱内与沙箱外，结果都一样，
且 qtpaths.exe 单独运行完全正常（`qtpaths --query QT_INSTALL_PREFIX` 有输出）。
环境层问题（本机装有会挂钩进程创建的安全代理），不是项目或 Qt 安装的问题。
因此加这条替代路径：从已经部署好的目录拷 Qt 运行时。用 `build/` 最方便——
CMake 构建目录里本来就有一份完整的 windeployqt 部署（历史成功运行留下的），
Qt 版本与主程序一致，插件集经实测可用。
"""
import argparse
import datetime
import hashlib
import os
import shutil
import subprocess
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_VERSION = "4.8.0"

# 发布时一并携带的文档
EXTRA_DOCS = ["LICENSE", "README.md", "THIRD-PARTY-NOTICES.txt"]

# --deploy-from 时拷贝的插件目录（与 windeployqt 实际产出的集合一致）
PLUGIN_DIRS = ["platforms", "styles", "imageformats", "iconengines", "tls",
               "networkinformation", "generic"]

# --deploy-from 时按 windeployqt 参数裁掉的文件
#   opengl32sw.dll      <- --no-opengl-sw（除非 --keep-opengl-sw）
#   D3Dcompiler_47.dll  <- --no-system-d3d-compiler（Win10+ 自带）
SKIP_DLL_ALWAYS = {"d3dcompiler_47.dll"}
SKIP_DLL_UNLESS_OPENGL_SW = {"opengl32sw.dll"}


def die(message):
    print(f"[错误] {message}", file=sys.stderr)
    sys.exit(1)


def info(message):
    print(f"[打包] {message}")


def find_executable(build_dir):
    """在构建目录中定位 WallDesk.exe，兼容 Ninja 与 VS 多配置布局。"""
    candidates = [
        os.path.join(build_dir, "WallDesk.exe"),
        os.path.join(build_dir, "Release", "WallDesk.exe"),
        os.path.join(build_dir, "Debug", "WallDesk.exe"),
    ]
    for path in candidates:
        if os.path.isfile(path):
            return path
    die(f"未找到 WallDesk.exe，请确认 --build-dir 正确且已编译。已尝试：\n  "
        + "\n  ".join(candidates))


def find_windeployqt(qt_bin):
    search = []
    if qt_bin:
        search.append(qt_bin)
    if os.environ.get("QTDIR"):
        search.append(os.path.join(os.environ["QTDIR"], "bin"))
    for directory in search:
        path = os.path.join(directory, "windeployqt.exe")
        if os.path.isfile(path):
            return path
    found = shutil.which("windeployqt")
    if found:
        return found
    die("找不到 windeployqt.exe。用 --qt-bin 指定 Qt 套件的 bin 目录，"
        "例如 --qt-bin \"C:\\Qt\\6.8.3\\mingw_64\\bin\"；"
        "或改用 --deploy-from <已部署目录> 跳过 windeployqt。")


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def run_windeployqt(tool, exe_path, keep_opengl_sw):
    # 注意：不可加 --no-compiler-runtime。该参数对 MSVC 是优化（系统自带 msvcp/ucrt），
    # 但对 MinGW 会把 libgcc_s_seh-1/libstdc++-6/libwinpthread-1 一并跳过——
    # 这三个 DLL 用户系统里没有，发行包缺了必然启动失败。
    args = [tool, "--release", "--force",
            "--no-translations",           # 本程序界面为中文，不需要 Qt 自带的翻译包
            "--no-system-d3d-compiler"]    # Windows 10+ 自带 D3D 编译器
    if not keep_opengl_sw:
        args.append("--no-opengl-sw")      # 省约 30MB；Widgets 与 libVLC 都用 D3D11
    args.append(exe_path)
    info("运行 windeployqt（拷贝 Qt 运行时依赖）...")
    result = subprocess.run(args, capture_output=True, text=True)
    if result.returncode != 0:
        die("windeployqt 执行失败：\n" + (result.stdout or "") + (result.stderr or "")
            + "\n若报 'Unable to query qtpaths'，见本脚本顶部说明，改用 --deploy-from <已部署目录>。")


def deploy_from_reference(ref_dir, stage_dir, keep_opengl_sw):
    """从一份已部署好的目录拷贝 Qt 运行时（windeployqt 不可用时的替代路径）。

    只取「运行时需要」的部分：顶层 DLL + 插件目录。
    跳过构建产物、文档、数据目录，并按 windeployqt 的参数语义裁掉
    opengl32sw.dll / D3Dcompiler_47.dll / translations/。
    """
    if not os.path.isdir(ref_dir):
        die(f"--deploy-from 指定的目录不存在：{ref_dir}")

    copied = 0
    for name in sorted(os.listdir(ref_dir)):
        src = os.path.join(ref_dir, name)
        if not (os.path.isfile(src) and name.lower().endswith(".dll")):
            continue
        low = name.lower()
        if low in SKIP_DLL_ALWAYS:
            continue
        if low in SKIP_DLL_UNLESS_OPENGL_SW and not keep_opengl_sw:
            continue
        shutil.copy2(src, os.path.join(stage_dir, name))
        copied += 1

    for sub in PLUGIN_DIRS:
        src_dir = os.path.join(ref_dir, sub)
        if not os.path.isdir(src_dir):
            continue
        dst_dir = os.path.join(stage_dir, sub)
        os.makedirs(dst_dir, exist_ok=True)
        for name in sorted(os.listdir(src_dir)):
            src = os.path.join(src_dir, name)
            if os.path.isfile(src):
                shutil.copy2(src, os.path.join(dst_dir, name))
                copied += 1

    info(f"从 {ref_dir} 拷入 {copied} 个运行时文件（替代 windeployqt）")
    if copied == 0:
        die(f"--deploy-from 目录里没有可拷贝的运行时文件：{ref_dir}")
    return copied


def write_sums(stage_dir):
    lines = []
    for base, _, files in os.walk(stage_dir):
        for name in sorted(files):
            path = os.path.join(base, name)
            rel = os.path.relpath(path, stage_dir).replace(os.sep, "/")
            lines.append(f"{sha256(path)}  {rel}")
    sums = os.path.join(stage_dir, "SHA256SUMS.txt")
    with open(sums, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    return sums


def make_zip(stage_dir, out_dir):
    zip_path = os.path.join(out_dir, os.path.basename(stage_dir) + ".zip")
    info(f"压缩为 {zip_path} ...")
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for base, _, files in os.walk(stage_dir):
            for name in files:
                path = os.path.join(base, name)
                # zip 内以顶层目录开头，解压后直接得到文件夹
                arc = os.path.join(os.path.basename(stage_dir),
                                   os.path.relpath(path, stage_dir))
                zf.write(path, arc.replace(os.sep, "/"))
    return zip_path


def main():
    parser = argparse.ArgumentParser(description="WallDesk 发布打包")
    parser.add_argument("--build-dir", default=os.path.join(ROOT, "build"),
                        help="CMake 构建目录（默认 build）")
    parser.add_argument("--out", default=os.path.join(ROOT, "dist"),
                        help="输出目录（默认 dist）")
    parser.add_argument("--version", default=DEFAULT_VERSION, help="版本号（默认 %(default)s）")
    parser.add_argument("--qt-bin", default=None,
                        help="Qt 套件 bin 目录（windeployqt 所在处）")
    parser.add_argument("--deploy-from", default=None,
                        help="已部署好的目录（如 build）。给定则跳过 windeployqt，"
                             "直接从该目录拷 Qt 运行时")
    parser.add_argument("--no-zip", action="store_true", help="只生成目录，不打 zip")
    parser.add_argument("--keep-opengl-sw", action="store_true",
                        help="保留 opengl32sw.dll（老机器兼容性更好，体积 +30MB）")
    args = parser.parse_args()

    exe = find_executable(args.build_dir)

    stage_dir = os.path.join(args.out, f"WallDesk-{args.version}-win64")
    if os.path.isdir(stage_dir):
        shutil.rmtree(stage_dir)
    os.makedirs(stage_dir)
    shutil.copy2(exe, os.path.join(stage_dir, "WallDesk.exe"))
    info(f"程序：{exe}")

    if args.deploy_from:
        deploy_from_reference(os.path.abspath(args.deploy_from), stage_dir,
                              args.keep_opengl_sw)
    else:
        deploy = find_windeployqt(args.qt_bin)
        info(f"windeployqt：{deploy}")
        run_windeployqt(deploy, os.path.join(stage_dir, "WallDesk.exe"),
                        args.keep_opengl_sw)

    for doc in EXTRA_DOCS:
        src = os.path.join(ROOT, doc)
        if os.path.isfile(src):
            shutil.copy2(src, os.path.join(stage_dir, doc))
        else:
            print(f"[警告] {doc} 不存在，未随包发布")

    build_info = os.path.join(stage_dir, "BUILDINFO.txt")
    with open(build_info, "w", encoding="utf-8", newline="\n") as f:
        f.write(f"WallDesk {args.version} (Windows x64)\n")
        f.write(f"构建时间: {datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
        f.write(f"主程序 SHA256: {sha256(os.path.join(stage_dir, 'WallDesk.exe'))}\n")
        f.write("Qt 运行时: " + (
            "从预部署目录拷贝（windeployqt 在本机不可用）" if args.deploy_from
            else "由 windeployqt 自动部署") + "\n")
        f.write("视频引擎: libVLC 3.0.x（内嵌或随系统检测）\n")

    sums = write_sums(stage_dir)
    file_count = sum(len(files) for _, _, files in os.walk(stage_dir))
    total_mb = sum(
        os.path.getsize(os.path.join(base, name))
        for base, _, files in os.walk(stage_dir) for name in files
    ) / 1024 / 1024
    info(f"发布目录就绪：{stage_dir}（{file_count} 个文件，共 {total_mb:.1f} MB）")
    info(f"校验清单：{sums}")

    if not args.no_zip:
        zip_path = make_zip(stage_dir, args.out)
        info(f"发布包：{zip_path}（{os.path.getsize(zip_path) / 1024 / 1024:.1f} MB）")

    print()
    print("下一步：")
    print(f"  * 绿色版：直接把 {os.path.basename(stage_dir)} 整个目录（或 zip）发给用户，"
          "解压后运行 WallDesk.exe")
    print("  * 安装程序（可选）：安装 Inno Setup 6 后执行")
    print(f"      iscc installer\\WallDesk.iss")
    return 0


if __name__ == "__main__":
    sys.exit(main())
