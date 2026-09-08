#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""WallDesk 源码静态自检（无需编译器）。

在缺少 Qt / 编译器的机器上也能做的确定性检查：
  1. 括号平衡（剔除字符串与注释后统计）
  2. 头文件声明的成员函数是否在对应 cpp 中定义
  3. cpp 中定义的成员函数是否都在头文件声明过（V2 曾漏掉 resolveHostParent 的声明）
  4. libVLC 函数指针是否全部 bind 且被实际调用
  5. 槽函数是否都有实现
  6. 枚举分支是否全覆盖（含通过 else 隐式覆盖的情形）

用法：python tools/check_sources.py
"""
import os
import re
import sys

SRC = "src"
FILES = [
    "main.cpp",
    "MainWindow.h", "MainWindow.cpp",
    "WallpaperEngine.h", "WallpaperEngine.cpp",
    "VlcPlayer.h", "VlcPlayer.cpp",
    "DesktopWatcher.h", "DesktopWatcher.cpp",
    "VlcBundle.h", "VlcBundle.cpp",
    "AppTheme.h", "AppTheme.cpp",
    "ThumbnailLoader.h", "ThumbnailLoader.cpp",
    "FullscreenGuard.h", "FullscreenGuard.cpp",
    "AppPaths.h", "AppPaths.cpp",
    "Logger.h", "Logger.cpp",
    "SingleInstance.h", "SingleInstance.cpp",
]

# 声明/定义配对
PAIRS = [("MainWindow.h", "MainWindow.cpp"),
         ("WallpaperEngine.h", "WallpaperEngine.cpp"),
         ("VlcPlayer.h", "VlcPlayer.cpp"),
         ("DesktopWatcher.h", "DesktopWatcher.cpp"),
         ("VlcBundle.h", "VlcBundle.cpp"),
         ("AppTheme.h", "AppTheme.cpp"),
         ("ThumbnailLoader.h", "ThumbnailLoader.cpp"),
         ("FullscreenGuard.h", "FullscreenGuard.cpp"),
         ("AppPaths.h", "AppPaths.cpp"),
         ("Logger.h", "Logger.cpp"),
         ("SingleInstance.h", "SingleInstance.cpp")]

# 通过 else 分支隐式覆盖的枚举项：不需要显式出现枚举名
IMPLICIT_ENUM = {"ScreenTarget::Primary": "target == ScreenTarget::Virtual"}

# 枚举覆盖检查：文件、枚举名、取值列表
ENUM_CHECKS = [
    ("WallpaperEngine.cpp", "ImageFit",
     ["Fill", "Fit", "Stretch", "Tile", "Center", "Span"]),
    ("WallpaperEngine.cpp", "ScreenTarget", ["Primary", "Virtual", "Monitor"]),
    ("VlcPlayer.cpp", "PlaybackState",
     ["Nothing", "Opening", "Buffering", "Playing", "Paused", "Stopped", "Ended", "Error", "Unknown"]),
    ("VlcPlayer.cpp", "VlcProfile", ["Auto", "Hardware", "Software", "Quality"]),
    ("AppTheme.cpp", "ThemeKind", ["Light", "Dark", "System"]),
]

# 成员函数声明：行首缩进 + 可选前缀 + 返回类型 + 函数名 + 参数 + 分号结尾。
# 排除 typedef（函数指针类型）与 return（跨行 return xxx(...) 语句），两者都不是声明。
DECL_RE = (r"^\s+(?!typedef\s)(?!return\s)"
           r"(?:explicit\s+|static\s+|virtual\s+|inline\s+)?"
           r"(?:const\s+)?[\w:<>\*&]+\s+(\w+)\s*\([^;{]*\)"
           r"\s*(?:const\s*)?(?:override\s*)?\s*;")

issues = []


def read(name):
    with open(os.path.join(SRC, name), encoding="utf-8") as f:
        return f.read()


def strip_signals(text):
    """剔除 signals: 区块：信号的实现由 moc 生成，不要求 cpp 中存在定义。"""
    return re.sub(r"\nsignals:.*?(?=\n\s*(?:public|private|protected)[\s:]|\Z)",
                  "\n", text, flags=re.S)


def strip_comments(text):
    """剔除 C++ 单行/多行注释与字符串字面量，避免它们干扰声明/定义匹配。"""
    text = re.sub(r"//[^\n]*", "", text)
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r'"(\\.|[^"\\])*"', '""', text)
    text = re.sub(r"'(\\.|[^'\\])*'", "''", text)
    return text


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    os.chdir(root)
    texts = {name: read(name) for name in FILES}

    # 1) 括号平衡
    for name, text in texts.items():
        stripped = re.sub(r"//[^\n]*", "", text)
        stripped = re.sub(r"/\*.*?\*/", "", stripped, flags=re.S)
        stripped = re.sub(r'"(\\.|[^"\\])*"', '""', stripped)
        stripped = re.sub(r"'(\\.|[^'\\])*'", "''", stripped)
        for op, cl in [("{", "}"), ("(", ")"), ("[", "]")]:
            if stripped.count(op) != stripped.count(cl):
                issues.append(f"[括号] {name}: {op}{cl} 不平衡 "
                              f"({stripped.count(op)} vs {stripped.count(cl)})")

    # 2) 声明 → 定义
    for header, source in PAIRS:
        decls = re.findall(DECL_RE, strip_comments(strip_signals(texts[header])), re.M)
        for decl in decls:
            if f"{header[:-2]}::{decl}" not in strip_comments(texts[source]):
                issues.append(f"[符号] {header} 声明的 {decl}() 在 {source} 中未定义")

    # 3) 定义 → 声明（反向）：漏声明会直接编译失败，必须拦住
    for header, source in PAIRS:
        cls = header[:-2]
        src_clean = strip_comments(texts[source])
        hdr_clean = strip_comments(texts[header])
        for name in sorted(set(re.findall(rf"\b{cls}::(\w+)\s*\(", src_clean))):
            if name == cls:
                continue  # 构造函数
            if re.search(rf"\b{re.escape(name)}\s*\(", hdr_clean) is None:
                issues.append(f"[符号] {source} 定义的 {cls}::{name}() 在 {header} 中未声明")

    # 4) libVLC 函数指针绑定与调用
    members = re.findall(r"^\s*(Pfn\w+)\s+(\w+_)\s*=", texts["VlcPlayer.h"], re.M)
    for _, member in members:
        if f"bind({member}" not in texts["VlcPlayer.cpp"]:
            issues.append(f"[VLC] 函数指针 {member} 未绑定")
    binds = re.findall(r'bind\((\w+), "(libvlc[^"]+)"\)', texts["VlcPlayer.cpp"])
    print(f"已绑定 libvlc 函数: {len(binds)} 个")
    for _, symbol in binds:
        if f"{symbol}(" not in texts["VlcPlayer.cpp"] and \
           f"{symbol}_(" not in texts["VlcPlayer.cpp"]:
            issues.append(f"[VLC] {symbol} 已绑定但从未调用")

    # 5) 槽函数实现
    for slot in re.findall(r"void\s+(on\w+)\s*\(", texts["MainWindow.h"]):
        if f"MainWindow::{slot}" not in texts["MainWindow.cpp"]:
            issues.append(f"[槽函数] {slot} 未定义")

    # 6) 枚举分支覆盖
    for target, enum, values in ENUM_CHECKS:
        for value in values:
            key = f"{enum}::{value}"
            if key in texts[target]:
                continue
            guard = IMPLICIT_ENUM.get(key)
            if guard and guard in texts[target]:
                continue  # 由 else 分支隐式覆盖
            issues.append(f"[枚举] {key} 未在 {target} 中处理")

    # 7) 裸指针成员必须在 cpp 中被赋值。
    #    V4 曾抓到 m_pauseButton 只声明未创建，运行到解引用即崩溃。
    for header, source in PAIRS:
        for member in re.findall(r"^\s+Q\w+\s*\*\s*(m_\w+)\s*=", texts[header], re.M):
            if f"{member} =" not in texts[source]:
                issues.append(f"[成员] {header} 声明的 {member} 从未在 {source} 中赋值")

    print("=" * 46)
    if issues:
        print("发现问题：")
        for issue in issues:
            print("  -", issue)
        return 1
    print("静态校验通过：括号平衡、符号齐备、绑定完整、枚举全覆盖")
    return 0


if __name__ == "__main__":
    sys.exit(main())
