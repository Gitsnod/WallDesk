#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""用 GitHub Git Data API 同步标签（绕开不可达的 github.com）。

背景与 push_via_api.py 相同：本机只能访问 api.github.com / uploads.github.com。

关键点：因为 push_via_api.py 走的是「Git Data API 重放」，远端提交 SHA 与本地
不同，所以**不能**直接把本地 tag 里的 object 指过去 —— 必须把本地提交 SHA
映射到远端重放出来的 SHA 之后，再重建 tag 对象。

用法：
    python tools/push_tags_via_api.py <TOKEN> v4.7.0 [v4.8.0 ...]
    python tools/push_tags_via_api.py <TOKEN> --all-missing
    python tools/push_tags_via_api.py <TOKEN> --list      # 只列远端标签
环境变量 GITHUB_TOKEN 可代替位置参数。
映射文件（可选）：tools/remote_sha_map.json
    形如 {"c0fbc757a589b6fc7c3cd071e7aaff083557f8da": "<远端重放 sha>"}
"""
import json
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from push_via_api import GIT, REPO, ROOT, api, sh  # noqa: E402


def read_tag(tag):
    """读取本地标签，返回 dict；返回 None 表示标签不存在。

    附注标签（annotated）：object / type / tag / tagger / message
    轻量标签（lightweight）：没有独立对象，直接指向提交。
    """
    try:
        objtype = sh("cat-file", "-t", tag)
    except subprocess.CalledProcessError:
        return None

    if objtype == "tag":
        raw = subprocess.check_output([GIT, "cat-file", "-p", tag],
                                      cwd=ROOT).decode("utf-8", "replace")
        # 头部逐行 key value，空行之后是 message
        head, _, message = raw.partition("\n\n")
        info = {}
        for line in head.splitlines():
            k, _, v = line.partition(" ")
            info.setdefault(k, []).append(v)
        obj = info["object"][0]
        ttype = info["type"][0]
        tname = info["tag"][0]
        tagger = info["tagger"][0] if info.get("tagger") else None
        return {
            "annotated": True,
            "object": obj,
            "target_type": ttype,
            "name": tname,
            "tagger": tagger,
            "message": message,
        }

    # 轻量标签
    sha = sh("rev-parse", f"{tag}^{{commit}}")
    return {"annotated": False, "object": sha, "target_type": "commit",
            "name": tag, "tagger": None, "message": ""}


def parse_tagger(text):
    """'Gitsnod <x@y> 1789372737 +0800' -> dict(name, email, date)

    API 的 tagger.date 要求 ISO 8601，而 git 给的是「unix 秒 + 时区偏移」，
    必须转换，否则 422: "... is not a valid date-time"。
    """
    if not text:
        return None
    try:
        name_email, epoch, tz = text.rsplit(" ", 2)
        name, _, email = name_email.partition(" <")
        email = email.rstrip(">")

        # 1789372737 +0800 -> 2026-09-14T15:58:57+08:00
        import datetime
        sign = 1 if tz.startswith("+") else -1
        hh, mm = int(tz[1:3]), int(tz[3:5])
        offset = datetime.timedelta(hours=hh, minutes=mm) * sign
        utc = datetime.datetime.fromtimestamp(int(epoch), datetime.timezone.utc)
        local = utc + offset
        date = local.strftime("%Y-%m-%dT%H:%M:%S") + tz[:3] + ":" + tz[3:]
        return {"name": name, "email": email, "date": date}
    except Exception:
        return None


def load_sha_map():
    p = os.path.join(ROOT, "tools", "remote_sha_map.json")
    if os.path.isfile(p):
        with open(p, encoding="utf-8") as f:
            return json.load(f)
    return {}


def map_sha(local_sha, mapping):
    return mapping.get(local_sha, local_sha)


def main():
    argv = [a for a in sys.argv[1:] if not a.startswith("--")]
    flags = [a for a in sys.argv[1:] if a.startswith("--")]
    token = argv[0].strip() if argv else os.environ.get("GITHUB_TOKEN", "").strip()
    tags = argv[1:]
    if not token:
        print(__doc__, file=sys.stderr)
        return 2

    st, remote_refs = api("GET", f"/repos/{REPO}/git/refs/tags", token)
    if st != 200:
        print(f"[错误] 读取远端标签失败（HTTP {st}）：{remote_refs}", file=sys.stderr)
        return 1
    remote_tags = {r["ref"].split("/")[-1]: r["object"]["sha"] for r in remote_refs}
    print(f"远端已有标签 {len(remote_tags)} 个")

    if "--list" in flags:
        for k in sorted(remote_tags):
            print(f"  {k}  {remote_tags[k][:10]}")
        return 0

    if not tags or "--all-missing" in flags:
        local_all = sh("tag", "-l").splitlines()
        tags = [t for t in local_all if t not in remote_tags]
        if not tags:
            print("本地所有标签都已在远端。")
            return 0
        print(f"缺失标签：{', '.join(tags)}")

    mapping = load_sha_map()
    if mapping:
        print(f"已载入 SHA 映射 {len(mapping)} 条")

    for tag in tags:
        info = read_tag(tag)
        if info is None:
            print(f"[跳过] 本地无标签 {tag}")
            continue
        target = map_sha(info["object"], mapping)

        # 校验目标对象在远端存在
        st, _ = api("GET", f"/repos/{REPO}/git/"
                    f"{'commits' if info['target_type'] == 'commit' else 'tags'}"
                    f"/{target}", token)
        if st != 200:
            print(f"[错误] {tag} 指向的对象 {target[:7]} 不在远端（HTTP {st}）。"
                  f"先把对应提交推上去，或在 tools/remote_sha_map.json 里补映射。",
                  file=sys.stderr)
            return 1
        if target != info["object"]:
            print(f"  {tag}: 本地 {info['object'][:7]} -> 远端 {target[:7]}（已按映射重定向）")

        if info["annotated"]:
            body = {
                "tag": info["name"],
                "message": info["message"],
                "object": target,
                "type": info["target_type"],
            }
            tagger = parse_tagger(info["tagger"])
            if tagger:
                body["tagger"] = tagger
            st, res = api("POST", f"/repos/{REPO}/git/tags", token, body)
            if st not in (200, 201):
                print(f"[错误] 创建 tag 对象 {tag} 失败（HTTP {st}）：{res}",
                      file=sys.stderr)
                return 1
            new_sha = res["sha"]
            print(f"  {tag}: 已建附注标签对象 {new_sha[:7]} -> {target[:7]}")
        else:
            new_sha = target

        st, res = api("POST", f"/repos/{REPO}/git/refs", token, {
            "ref": f"refs/tags/{tag}",
            "sha": new_sha,
        })
        if st not in (200, 201):
            print(f"[错误] 创建标签引用 {tag} 失败（HTTP {st}）：{res}", file=sys.stderr)
            return 1
        print(f"✅ 标签 {tag} 已推送 -> {target[:7]}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
