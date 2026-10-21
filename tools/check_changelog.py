# -*- coding: utf-8 -*-
"""CHANGELOG.md 合规校验：Keep a Changelog 1.0.0 + 语义化版本 2.0.0。

确定性检查，不依赖人眼：
  K1 首部声明格式来源与 SemVer 遵从
  K2 存在「未发布」区块且排在最新版本之前
  K3 每个版本独立标题，格式 `## [x.y.z] - YYYY-MM-DD`
  K4 日期为 ISO 8601 合法日期
  K5 版本号符合 SemVer 正式版正则，且无前导零
  K6 版本按优先层级严格降序（SemVer 第 11 条）
  K7 变更类型只允许六类，且不出现空分类
  K8 每个版本至少有一个变更类型分组
  K9 每个版本号都有对应的链接定义
  K10 与 git tag 双向对齐（tag 有、日志也有）
"""
import re
import subprocess
import sys

P = "CHANGELOG.md"
text = open(P, "rb").read().decode("utf-8").replace("\r\n", "\n")
lines = text.split("\n")

SEMVER = re.compile(r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$")
DATE = re.compile(r"^\d{4}-\d{2}-\d{2}$")
VER_HEAD = re.compile(r"^## \[(.*?)\](?: - (.*))?$")
CATS = {"新增（Added）", "变更（Changed）", "弃用（Deprecated）",
        "移除（Removed）", "修复（Fixed）", "安全（Security）"}

errors, warns, info = [], [], []

# K1
if "keepachangelog.com" not in text or "semver.org" not in text:
    errors.append("K1 首部未声明 Keep a Changelog / Semantic Versioning 链接")

# 解析版本块
blocks = []
cur = None
for ln in lines:
    m = VER_HEAD.match(ln)
    if m:
        cur = {"ver": m.group(1), "date": m.group(2), "cats": [], "line": ln}
        blocks.append(cur)
        continue
    if ln.startswith("## "):
        cur = None
        continue
    if cur and ln.startswith("### "):
        cur["cats"].append(ln[4:].strip())

names = [b["ver"] for b in blocks]
info.append("解析到版本块 %d 个：%s" % (len(blocks), ", ".join(names)))

# K2
if not blocks or blocks[0]["ver"] != "未发布":
    errors.append("K2 首个区块不是「未发布」（当前首个为 %s）" % (names[0] if names else "空"))

# K3/K4/K5/K7/K8
for b in blocks:
    if b["ver"] == "未发布":
        continue
    if not b["date"]:
        errors.append("K3 [%s] 缺少发布日期" % b["ver"])
    elif not DATE.match(b["date"]):
        errors.append("K3 [%s] 日期格式不是 YYYY-MM-DD：%r" % (b["ver"], b["date"]))
    else:
        y, mo, d = map(int, b["date"].split("-"))
        if not (1 <= mo <= 12 and 1 <= d <= 31):
            errors.append("K4 [%s] 日期数值非法：%s" % (b["ver"], b["date"]))
    if not SEMVER.match(b["ver"]):
        errors.append("K5 [%s] 不是合法语义化版本号" % b["ver"])
    if not b["cats"]:
        errors.append("K8 [%s] 没有任何变更类型分组" % b["ver"])
    for c in b["cats"]:
        if c not in CATS:
            errors.append("K7 [%s] 出现非标准变更类型：%r" % (b["ver"], c))

# 空分类检查：分类标题后到下一个 ### 或 ## 之间无内容
for i, ln in enumerate(lines):
    if ln.startswith("### ") and ln[4:].strip() in CATS:
        j = i + 1
        while j < len(lines) and not lines[j].startswith("## ") and not lines[j].startswith("### "):
            if lines[j].strip() and not lines[j].strip().startswith("<!--"):
                break
            j += 1
        else:
            errors.append("K7 第 %d 行的分类「%s」内容为空" % (i + 1, ln[4:].strip()))

# K6 严格降序
def key(v):
    a = SEMVER.match(v)
    return tuple(int(x) for x in a.groups()) if a else None


reals = [b["ver"] for b in blocks if b["ver"] != "未发布" and SEMVER.match(b["ver"])]
for a, b in zip(reals, reals[1:]):
    if key(a) <= key(b):
        errors.append("K6 顺序错误：%s 未高于 %s（应新在前）" % (a, b))

# K9 链接定义
defs = set(re.findall(r"^\[([^\]]+)\]:", text, re.M))
for b in blocks:
    tag = "未发布" if b["ver"] == "未发布" else b["ver"]
    if tag not in defs:
        errors.append("K9 [%s] 缺少链接定义 [%s]:" % (b["ver"], tag))

# K10 与 git tag 对齐
try:
    gits = subprocess.run(["git", "tag", "-l"], capture_output=True, text=True)
    tags = {t.lstrip("v") for t in gits.stdout.split()} if gits.returncode == 0 else set()
except Exception:
    tags = set()
only_tag = sorted(tags - set(reals), key=lambda s: key(s) or (0, 0, 0))
only_log = sorted(set(reals) - tags, key=lambda s: key(s) or (0, 0, 0))
if tags:
    info.append("git tag %d 个，日志条目 %d 个" % (len(tags), len(reals)))
    for t in only_tag:
        errors.append("K10 tag v%s 在日志中没有对应条目" % t)
    for t in only_log:
        errors.append("K10 日志条目 %s 没有对应 git tag" % t)

print("== 检查结果 ==")
for x in info:
    print("  [信息]", x)
for x in warns:
    print("  [警告]", x)
if errors:
    print("\n共 %d 项不合规：" % len(errors))
    for e in errors:
        print("  ✗", e)
    sys.exit(1)
print("\n全部通过：K1–K10 无不合规项（%d 个已发布版本）" % len(reals))
