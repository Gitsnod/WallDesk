#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""用 GitHub Git Data API 把本地提交推送到远端（绕开不可达的 github.com）。

背景：本机经 WorkBuddy 宿主的 HTTP 代理访问网络，该代理只放行
  api.github.com / uploads.github.com，github.com 本身不可达
（git push 走 github.com，必然失败）。
因此改用 Git Data API 组装与本地提交等价的树和提交对象，再移动分支引用。

只搬运「本地有、远端没有」的提交，不重写历史。

--- v2 修复（2026-09-14）---
v1 的 build_tree() 以「远端父提交的树」为 base_tree，再叠加 `git diff-tree
<父> <子>` 的 name-status 清单。这两者在语义上并不匹配：
  * base_tree 要求叠加项是「相对该 base 的变更」
  * 而 diff-tree 算的是「相对本地父提交的变更」
当远端父提交与本地父提交不是同一个对象（例如远端曾被回滚、或两者历史分叉）时，
diff 清单会整体错位，结果是远端树被覆盖成近乎空白 —— v1 曾把远端 main 推成
只剩 6 个条目，事故级。

v2 的策略是「不做增量、只做全量」：直接从本地提交递归构建完整树
（逐个 subtree 建，叶子装 blob），不再传 base_tree。代价是首次推送要创建的
子对象更多，但正确性不依赖任何远端历史假设。

用法：
    python tools/push_via_api.py <TOKEN> [--dry-run]
    set GITHUB_TOKEN=xxx && python tools/push_via_api.py
"""
import base64
import json
import os
import subprocess
import sys
import time
import urllib.error
import urllib.request

REPO = "Gitsnod/WallDesk"
BRANCH = "main"
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
API = "https://api.github.com"
HTTP_TIMEOUT = 180


def find_git():
    """定位 git 可执行文件。

    本机 PATH 里没有裸 `git`（WorkBuddy 的 bash shim 不注入 PATH），
    所以按常见位置探测，最后才回退到裸名。
    """
    import shutil
    env_git = os.environ.get("GIT_EXE")
    if env_git and os.path.isfile(env_git):
        return env_git
    candidates = [
        r"C:\Users\Lenovo\.workbuddy\binaries\PortableGit\versions\1.2.0\cmd\git.exe",
        r"C:\Users\Lenovo\AppData\Local\Programs\PortableGit\cmd\git.exe",
        r"C:\Program Files\Git\cmd\git.exe",
        r"C:\Program Files (x86)\Git\cmd\git.exe",
    ]
    for c in candidates:
        if os.path.isfile(c):
            return c
    return shutil.which("git") or "git"


GIT = find_git()


def sh(*args):
    """在仓库根目录执行 git 命令，返回去尾空白的结果。"""
    out = subprocess.check_output(
        [GIT] + list(args), cwd=ROOT, stderr=subprocess.STDOUT
    )
    return out.decode("utf-8", "replace").strip()


def api(method, path, token, data=None, raw=False, content_type="application/json",
        retries=3):
    """调用 GitHub API。对 5xx / 网络抖动做有限重试（代理偶发抽风）。"""
    url = path if path.startswith("http") else API + path
    body = None
    if data is not None:
        body = data if raw else json.dumps(data).encode("utf-8")

    last = None
    for attempt in range(retries):
        req = urllib.request.Request(url, data=body, method=method)
        req.add_header("Authorization", "Bearer " + token)
        req.add_header("Accept", "application/vnd.github+json")
        req.add_header("X-GitHub-Api-Version", "2022-11-28")
        req.add_header("User-Agent", "WallDesk-Push-Script")
        if body is not None:
            req.add_header("Content-Type", content_type)
        try:
            with urllib.request.urlopen(req, timeout=HTTP_TIMEOUT) as resp:
                payload = resp.read()
                return resp.status, (json.loads(payload) if payload else {})
        except urllib.error.HTTPError as e:
            detail = e.read().decode("utf-8", "replace")
            last = (e.code, detail)
            # 4xx 除了 429 都是确定性错误，不重试
            if e.code < 500 and e.code != 429:
                return e.code, detail
        except Exception as e:                      # 网络层抖动
            last = (0, repr(e))
        if attempt < retries - 1:
            time.sleep(1.5 * (attempt + 1))
    return last


def list_tree(commit_sha, path_prefix=""):
    """`git ls-tree -r -z` 列出该提交的全部 blob（不带 -t，只要文件）。

    返回 [(mode, blob_sha, path), ...]。用递归模式一次拿全，避免逐层查询。
    """
    raw = subprocess.check_output(
        [GIT, "ls-tree", "-r", "-z", commit_sha] + ([path_prefix] if path_prefix else []),
        cwd=ROOT,
    ).decode("utf-8", "replace")
    entries = []
    for rec in raw.split("\0"):
        if not rec:
            continue
        meta, path = rec.split("\t", 1)
        mode, objtype, sha = meta.split()
        if objtype != "blob":
            continue
        entries.append((mode, sha, path))
    return entries


def create_blob_if_needed(token, blob_sha, cache):
    """本地 git blob -> 远端 blob。同一本地 blob 只上传一次。"""
    if blob_sha in cache:
        return cache[blob_sha]
    content = subprocess.check_output([GIT, "cat-file", "-p", blob_sha], cwd=ROOT)
    st, res = api("POST", f"/repos/{REPO}/git/blobs", token, {
        "content": base64.b64encode(content).decode("ascii"),
        "encoding": "base64",
    })
    if st not in (200, 201):
        raise RuntimeError(f"创建 blob 失败 {blob_sha[:7]}（HTTP {st}）：{res}")
    cache[blob_sha] = res["sha"]
    return res["sha"]


def build_full_tree(token, commit_sha, cache):
    """从本地提交构建**完整**远端树（不带 base_tree），返回 tree sha。

    递归策略：按目录分组，先建子目录的 subtree，再建根 tree。
    这样远端树的每一个条目都由本地内容显式给出，不依赖远端既有状态。
    """
    files = list_tree(commit_sha)

    # 把 path 折叠成目录树：{"src": {"sub": {...}, "__files__": [(mode,blob,name)]}}
    def newdir():
        return {"__files__": []}

    root = newdir()
    for mode, blob_sha, path in files:
        parts = path.split("/")
        node = root
        for part in parts[:-1]:
            node = node.setdefault(part, newdir())
        node["__files__"].append((mode, blob_sha, parts[-1]))

    created_trees = {}

    def build(node):
        entries = []
        for name, child in node.items():
            if name == "__files__":
                continue
            sub_sha = build(child)
            created_trees[sub_sha] = True
            entries.append({"path": name, "mode": "040000",
                            "type": "tree", "sha": sub_sha})
        for mode, blob_sha, name in node["__files__"]:
            remote_blob = create_blob_if_needed(token, blob_sha, cache)
            entries.append({"path": name, "mode": mode,
                            "type": "blob", "sha": remote_blob})
        if not entries:
            # 空目录在 git 里不存在，但为保险起见仍建一个空 tree
            st, res = api("POST", f"/repos/{REPO}/git/trees", token, {"tree": []})
            if st not in (200, 201):
                raise RuntimeError(f"创建空 tree 失败（HTTP {st}）：{res}")
            return res["sha"]

        st, res = api("POST", f"/repos/{REPO}/git/trees", token, {"tree": entries})
        if st not in (200, 201):
            raise RuntimeError(f"创建 tree 失败（HTTP {st}）：{res}")
        return res["sha"]

    return build(root), len(files)


def verify_remote_tree(token, tree_sha, expected):
    """逐文件比对远端树与本地 (mode, blob_sha, path) 清单。返回 (ok, 问题列表)。

    递归拉取远端树；GitHub 的 tree 接口在 truncated=false 时给出全部条目。
    """
    st, res = api("GET", f"/repos/{REPO}/git/trees/{tree_sha}?recursive=1", token)
    if st != 200:
        return False, [f"读取远端树失败（HTTP {st}）：{res}"]

    remote = {}
    for ent in res.get("tree", []):
        if ent["type"] == "blob":
            remote[ent["path"]] = (ent["mode"], ent["sha"])

    problems = []
    for mode, blob_sha, path in expected:
        if path not in remote:
            problems.append(f"MISSING  {path}")
        elif remote[path][1] != blob_sha:
            problems.append(
                f"DIFF     {path}  本地 {blob_sha[:7]} / 远端 {remote[path][1][:7]}")
        elif remote[path][0] != mode:
            problems.append(
                f"MODE     {path}  本地 {mode} / 远端 {remote[path][0]}")
    for path in remote:
        if path not in {p for _m, _s, p in expected}:
            problems.append(f"EXTRA    {path}")

    if res.get("truncated"):
        problems.append("警告：远端树返回 truncated=true，比对可能不完整")
    return (not problems and not res.get("truncated")), problems


def load_sha_map():
    """本地提交 SHA -> 远端重放 SHA。

    Git Data API 重放出来的提交与本地是不通对象，SHA 必然不同。一旦推过一轮，
    远端 HEAD 就不在本地历史里了，再想推新提交必须知道「哪个本地提交对应远端
    HEAD」。这个映射由 push_tags_via_api.py 同步维护在同一文件里。
    """
    p = os.path.join(ROOT, "tools", "remote_sha_map.json")
    if os.path.isfile(p):
        try:
            with open(p, encoding="utf-8") as f:
                return json.load(f)
        except Exception:
            return {}
    return {}


def find_mapped_local(remote_head, mapping):
    """由远端 HEAD 反查它对应的本地提交 SHA。"""
    for local_sha, remote_sha in mapping.items():
        if remote_sha == remote_head or remote_sha.startswith(remote_head) \
                or remote_head.startswith(remote_sha):
            return local_sha
    return None


def main():
    argv = [a for a in sys.argv[1:] if not a.startswith("--")]
    dry = "--dry-run" in sys.argv
    verify_only = "--verify" in sys.argv
    token = (argv[0].strip() if argv else os.environ.get("GITHUB_TOKEN", "").strip())
    if not token:
        print("用法: python tools/push_via_api.py <TOKEN> [--dry-run] [--verify]",
              file=sys.stderr)
        print("      或：set GITHUB_TOKEN=xxx && python tools/push_via_api.py",
              file=sys.stderr)
        return 2

    local_head = sh("rev-parse", "HEAD")
    subject = sh("log", "-1", "--pretty=%s")
    print(f"本地 HEAD : {local_head[:7]}  {subject}")

    st, ref = api("GET", f"/repos/{REPO}/git/ref/heads/{BRANCH}", token)
    if st != 200:
        print(f"[错误] 读取远端分支失败（HTTP {st}）：{ref}", file=sys.stderr)
        return 1
    remote_head = ref["object"]["sha"]
    print(f"远端 {BRANCH} : {remote_head[:7]}")

    if verify_only:
        expected = list_tree(local_head)
        st, c = api("GET", f"/repos/{REPO}/git/commits/{remote_head}", token)
        if st != 200:
            print(f"[错误] 读取远端提交失败（HTTP {st}）：{c}", file=sys.stderr)
            return 1
        ok, problems = verify_remote_tree(token, c["tree"]["sha"], expected)
        print(f"\n核对 {len(expected)} 个文件：{'全部一致' if ok else '存在问题'}")
        for p in problems[:80]:
            print("  " + p)
        return 0 if ok else 1

    if remote_head == local_head:
        print("远端已包含本地提交，无需推送。")
        return 0

    # 远端 HEAD 不在本地历史时，尝试用映射把它翻译回本地 SHA
    mapping = load_sha_map()
    base = remote_head
    try:
        sh("cat-file", "-e", f"{remote_head}^{{commit}}")
        base_is_local = True
    except subprocess.CalledProcessError:
        base_is_local = False

    if not base_is_local:
        mapped = find_mapped_local(remote_head, mapping)
        if not mapped:
            print(f"[错误] 远端 HEAD {remote_head[:7]} 不在本地历史中，且 "
                  f"tools/remote_sha_map.json 里没有它的映射。\n"
                  f"       请在映射文件里补一条 {{\"<本地提交>\": \"{remote_head}\"}} 后重试。",
                  file=sys.stderr)
            return 1
        print(f"远端 HEAD 是重放对象，按映射等价于本地 {mapped[:7]}")
        base = mapped

    try:
        pending = sh("rev-list", "--reverse", f"{base}..{local_head}").splitlines()
    except subprocess.CalledProcessError:
        print("[错误] 无法计算待推送提交，需人工处理（本脚本不重写历史）",
              file=sys.stderr)
        return 1
    print(f"待推送提交数：{len(pending)}")
    for c in pending:
        print(f"  {c[:7]} {sh('log', '-1', '--pretty=%s', c)}")

    if dry:
        print("[dry-run] 未做任何改动。")
        return 0

    cache = {}
    parent_sha = remote_head
    new_map = dict(mapping)
    for i, c in enumerate(pending, 1):
        tree_sha, nfiles = build_full_tree(token, c, cache)
        msg = sh("log", "-1", "--pretty=%B", c).strip() or "update"
        st, commit = api("POST", f"/repos/{REPO}/git/commits", token, {
            "message": msg,
            "tree": tree_sha,
            "parents": [parent_sha],
        })
        if st not in (200, 201):
            print(f"[错误] 创建提交 {c[:7]} 失败（HTTP {st}）：{commit}",
                  file=sys.stderr)
            return 1
        new_map[c] = commit["sha"]
        print(f"  [{i}/{len(pending)}] {c[:7]} -> {commit['sha'][:7]}"
              f"  (tree {tree_sha[:7]}, {nfiles} 文件)")
        parent_sha = commit["sha"]

    # 移动分支引用（快进，不做 force —— 防止再次误伤远端）
    st, res = api("PATCH", f"/repos/{REPO}/git/refs/heads/{BRANCH}", token, {
        "sha": parent_sha,
        "force": False,
    })
    if st not in (200, 201):
        print(f"[错误] 更新分支失败（HTTP {st}）：{res}", file=sys.stderr)
        return 1
    print(f"分支 {BRANCH} 已更新 -> {parent_sha[:7]}")

    # 推送后立刻自检：远端树必须与本地逐文件一致
    print("\n--- 推送后核对 ---")
    expected = list_tree(local_head)
    st, c = api("GET", f"/repos/{REPO}/git/commits/{parent_sha}", token)
    ok, problems = verify_remote_tree(token, c["tree"]["sha"], expected)
    print(f"核对 {len(expected)} 个文件：{'全部一致 ✅' if ok else '存在问题 ❌'}")
    for p in problems[:80]:
        print("  " + p)
    if not ok:
        print(f"\n[警告] 远端树与本地不一致，请勿继续后续步骤，可回滚："
              f"\n  PATCH /repos/{REPO}/git/refs/heads/{BRANCH} "
              f'{{"sha":"{remote_head}","force":true}}')
        return 1

    # 维护映射文件，供下次 push / 标签同步使用
    map_path = os.path.join(ROOT, "tools", "remote_sha_map.json")
    with open(map_path, "w", encoding="utf-8") as f:
        json.dump(new_map, f, ensure_ascii=False, indent=2, sort_keys=True)
    print(f"已更新 {os.path.relpath(map_path, ROOT)}（{len(new_map)} 条映射）")

    print(f"\n本地 HEAD {local_head[:7]} / 远端 HEAD {parent_sha[:7]}"
          f"（Git Data API 重放，SHA 与本地不同属正常）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
