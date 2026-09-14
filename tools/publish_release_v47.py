#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""用 GitHub API 发布 WallDesk 4.7.0：创建 Release 并上传安装包。

走 api.github.com / uploads.github.com（这两个域网络可达），
不依赖 git push 通道。

用法：
    python tools/publish_release_v47.py <TOKEN>
"""
import json
import os
import sys
import time
import urllib.request
import urllib.error

REPO = "Gitsnod/WallDesk"
TAG = "v4.7.0"
NAME = "WallDesk v4.7.0"
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIST = os.path.join(ROOT, "dist")

ASSETS = [
    os.path.join(DIST, "WallDesk-4.7.0-setup.exe"),
    os.path.join(DIST, "WallDesk-4.7.0-win64.zip"),
]

BODY = """## WallDesk v4.7.0

本次新增 5 项功能。

### 新增功能

- **场景预设** — 保存 / 套用整套壁纸配置（壁纸、适配模式、音量、画面效果、桌面挂件、频谱、自动切换策略），支持多显示器逐屏记录，最多 60 个预设。旧预设自动兼容后续新增字段。
- **播放历史** — 记录壁纸播放轨迹，可在列表中回溯切换。
- **在线图源** — 支持 URL 直接导入图片，内置 Bing 每日壁纸（可回溯 8 天）。下载带 40MB 上限、20s 超时、Content-Length 预检与文件名防穿越。
- **智能分组** — 按尺寸 / 朝向（横 / 纵 / 方）与分辨率档位（SD / HD / FHD / QHD / UHD）筛选壁纸库，新增「按分辨率」排序。
- **视频配频谱** — 音量归零时改用 `:no-audio` 不建立音频输出链路（比音量衰减更省电），需要频谱响应时才建立链路。

### 修复

- 自动切换的「随机不重复」策略此前退化为可重复随机：手动「下一张」与定时自动切换走的不是同一条路径。已抽出 `nextIndexByPolicy()` 统一两者。

### 性能

- 尺寸 / 朝向 / 分辨率信息仅在实际筛选或排序时读取文件头，普通浏览不触发探测。
- PNG 额外手工解析 IHDR，作为 Qt 插件失效时的兜底。

### 安装说明

- `WallDesk-4.7.0-setup.exe` — 安装程序，装到 `%LOCALAPPDATA%\\Programs\\WallDesk`，无需管理员权限。
- `WallDesk-4.7.0-win64.zip` — 绿色版，解压即用。

视频引擎 libVLC 3.0.x 已内嵌，无需单独安装 VLC。
"""


def api(method, path, token, data=None, raw=False, content_type="application/json"):
    url = path if path.startswith("http") else f"https://api.github.com{path}"
    body = None
    if data is not None:
        body = data if raw else json.dumps(data).encode("utf-8")
    req = urllib.request.Request(url, data=body, method=method)
    req.add_header("Authorization", f"Bearer {token}")
    req.add_header("Accept", "application/vnd.github+json")
    req.add_header("X-GitHub-Api-Version", "2022-11-28")
    req.add_header("User-Agent", "WallDesk-Release-Script")
    if body is not None:
        req.add_header("Content-Type", content_type)
    try:
        with urllib.request.urlopen(req, timeout=180) as resp:
            payload = resp.read()
            return resp.status, (json.loads(payload) if payload else {})
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", "replace")


def main():
    if len(sys.argv) < 2:
        print("用法: python tools/publish_release_v47.py <TOKEN>", file=sys.stderr)
        return 2
    token = sys.argv[1].strip()

    for a in ASSETS:
        if not os.path.isfile(a):
            print(f"[错误] 缺少产物：{a}", file=sys.stderr)
            return 1

    # 1. 校验 token
    st, me = api("GET", "/user", token)
    if st != 200:
        print(f"[错误] token 无效（HTTP {st}）：{me}", file=sys.stderr)
        return 1
    print(f"[1/4] 已认证：{me.get('login')}")

    # 2. 查是否已有该 tag 的 release
    st, rel = api("GET", f"/repos/{REPO}/releases/tags/{TAG}", token)
    if st == 200:
        print(f"[2/4] Release {TAG} 已存在，复用（id={rel['id']}）")
        release = rel
    else:
        # 3. 创建 release（tag 若不存在会自动基于默认分支创建）
        print(f"[2/4] 创建 Release {TAG} ...")
        st, release = api("POST", f"/repos/{REPO}/releases", token, {
            "tag_name": TAG,
            "name": NAME,
            "body": BODY,
            "draft": False,
            "prerelease": False,
        })
        if st not in (200, 201):
            print(f"[错误] 创建 Release 失败（HTTP {st}）：{release}", file=sys.stderr)
            return 1
        print(f"      已创建（id={release['id']}）")

    # 4. 上传附件（uploads.github.com）
    print("[3/4] 上传附件 ...")
    existing = {a["name"] for a in release.get("assets", [])}
    upload_url = release["upload_url"].split("{")[0]

    for path in ASSETS:
        name = os.path.basename(path)
        if name in existing:
            print(f"      跳过（已存在）：{name}")
            continue
        size_mb = os.path.getsize(path) / 1024 / 1024
        print(f"      上传 {name}（{size_mb:.0f} MB）...")
        with open(path, "rb") as f:
            data = f.read()
        st, res = api(
            "POST", f"{upload_url}?name={name}", token,
            data=data, raw=True, content_type="application/octet-stream",
        )
        if st not in (200, 201):
            print(f"[错误] 上传 {name} 失败（HTTP {st}）：{res}", file=sys.stderr)
            return 1
        print(f"      OK -> {res.get('browser_download_url')}")
        time.sleep(1)

    print("[4/4] 完成")
    print()
    print(f"Release 页面：https://github.com/{REPO}/releases/tag/{TAG}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
