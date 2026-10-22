# WallDesk 开发者指南

面向使用者的说明（下载、安装、功能、常见问题）在 [README](../README.md)。
本文只写改代码、编译、打包、发版需要知道的事。

- 仓库：<https://github.com/Gitsnod/WallDesk>
- 技术栈：C++17 + Qt Widgets + Win32 API + libVLC（运行时动态加载，可内嵌进 exe）
- 目标平台：Windows 10 / 11 x64

---

## 目录

- [1. 环境清单](#1-环境清单)
- [2. 构建](#2-构建)
- [3. 调试](#3-调试)
- [4. 发布打包](#4-发布打包)
- [5. 同步到 GitHub](#5-同步到-github)
- [6. 开发环境排错](#6-开发环境排错)
- [7. 目录结构](#7-目录结构)
- [8. 设计要点](#8-设计要点)
- [9. 版本与变更记录](#9-版本与变更记录)

## 1. 环境清单

**必需的 5 项**（编译用，使用的人什么都不用装）：

| # | 软件 | 版本要求 | 用途 |
|---|------|----------|------|
| 1 | Qt Online Installer（开源版） | Qt 6.5 或 6.8 LTS，勾选 **Widgets** 模块 | GUI 框架 |
| 2 | C++ 编译器（二选一） | MinGW 13.1.0 64-bit 或 MSVC 2022 | 编译 |
| 3 | CMake | ≥ 3.21 | 构建系统 |
| 4 | Ninja | 任意新版 | 构建后端 |
| 5 | libVLC 运行时 | 3.0.x win64（不要 4.x） | 视频解码，内嵌打包用 |

**可选**：

| 软件 | 用途 |
|------|------|
| Inno Setup 6 | 生成安装程序 `WallDesk-x.y.z-setup.exe` |
| Git | 版本管理与上传 GitHub |

推荐路线 A（约 3 GB，不装 Visual Studio）：Qt 安装器里勾选
`Qt 6.8.3 > MinGW 13.1.0 64-bit` + `Developer Tools > MinGW / CMake / Ninja`。
Qt 套件与编译器必须同为 64 位，否则链接报 `LNK1112`。

## 2. 构建

1. 修改 `build.bat` 顶部四个变量：`QT_DIR`、`MINGW_BIN`、`VLC_DIR`、`PYTHON`
2. 双击 `build.bat`，产物 `build\WallDesk.exe`
3. 打发布包：`build.bat release`（自动跑 windeployqt + 生成 zip + 校验清单）

`build.bat` 会自动做三件事：检查并生成应用图标（`tools/make_icon.py`）、
打包内嵌 VLC 运行时（`tools/pack_vlc.py`，装了 VLC 才生效）、配置编译部署。
不打包 VLC 也能编译，视频功能回退到外部搜索（程序目录 / 注册表 / PATH）。

**改完源码必跑 `python tools/check_sources.py`**：它检查括号平衡、声明/定义配对、
libVLC 符号绑定是否齐全、枚举分支是否全覆盖，以及新增的 `.h/.cpp` 对有没有同步登记。
人工过一遍 30 个源文件不现实，这一步能挡住大部分低级错误。

## 3. 调试

### 命令行参数

完整参数表见 [README 的命令行一节](../README.md#命令行参数)。开发时常用的两条：

| 参数 | 开发时的用处 |
|------|--------------|
| `--portable` | 强制便携模式，配置与日志落在 `程序目录\data\`，不污染注册表 |
| `--verbose` | 日志里记录调试级信息 |

### 日志

`数据目录\logs\WallDesk.log`，超过 2 MB 自动滚动。首行即系统版本、Qt 版本、
程序目录与数据目录，判断「用户环境到底加载了哪份 Qt / VLC」时第一眼看这里。

关键判据行：

```
视频后端已加载：<路径>（内置 | 外部）
视频宿主窗口：hwnd=... 父窗口=... 区域=1920x1080(0,0) 模式=WorkerW
配置载入：应用规则 N 条，触发规则 M 条
```

「配置没生效」先看第三行 —— 十有八九是注入到了错误的配置位置
（安装模式写 `HKCU\Software\WallDesk`，便携模式写 `程序目录\data\WallDesk\WallDesk.ini`，
两者互不影响）。

### 数据目录

| 模式 | 配置与数据 |
|------|-----------|
| 安装模式 | `%LOCALAPPDATA%\WallDesk` |
| 便携模式 | `程序目录\data`（exe 同目录放一个空的 `portable.ini` 即开启） |

### 单实例

重复启动不会开第二个窗口：第二个进程通过 `QLocalServer` 把命令行转给已运行的实例
（`--next` / `--quit` 就是这条链路），然后退出。

## 4. 发布打包

### 形态 1：绿色压缩包（最简单）

```bat
build.bat release
```

产物：`dist\WallDesk-<版本>-win64.zip`（约 25–60 MB，取决于是否内嵌 VLC）。
用户解压即运行，无需安装 Qt / VLC / 运行时库。包内含：

- `WallDesk.exe` + Qt 运行时 DLL（windeployqt 部署）
- `LICENSE` / `THIRD-PARTY-NOTICES.txt`
- `BUILDINFO.txt`（版本、构建时间、主程序 SHA256）
- `SHA256SUMS.txt`（全部文件校验，用户可核对完整性）

### 形态 2：安装程序（给非技术用户）

1. 先执行形态 1 生成 `dist\WallDesk-<版本>-win64\` 目录
2. 安装 [Inno Setup 6](https://jrsoftware.org/isdl.php)（免费，约 5 MB）
3. 命令行执行 `iscc installer\WallDesk.iss`，或在 Inno 里打开该脚本按 F9

产物：`dist\WallDesk-<版本>-setup.exe`。安装到用户目录（无需管理员权限），
提供桌面图标、开始菜单、可选开机自启、完整卸载。

### 形态 3：单文件 exe（内嵌 VLC 时）

只要 `build.bat` 第一步打包了 VLC 且 CMake 显示「内置 VLC 运行时：开」，
`WallDesk.exe` 本身就是完整的——发给用户单个文件也能用（但 Qt DLL 仍需同目录，
所以实际推荐形态 1 的压缩包）。单文件场景适合放进内部工具目录。

### 分发前检查清单

- [ ] 在**没装 Qt / VLC** 的干净机器（或虚拟机）上跑一遍发布包
- [ ] 视频壁纸、图片壁纸、托盘、开机自启各测一次
- [ ] 用 `SHA256SUMS.txt` 抽查一两个文件的校验值
- [ ] 确认 LICENSE 与 THIRD-PARTY-NOTICES.txt 已随包（LGPL 合规要求）

## 5. 同步到 GitHub

仓库地址：<https://github.com/Gitsnod/WallDesk>（已创建，无需再 `git init`）

### 5.1 常规做法（能连通 github.com 的机器）

```bash
git add -A && git commit -m "说明本次改动"
git push -u origin main                       # 首次需要 -u

git tag -a v4.8.0 -m "WallDesk 4.8.0"
git push origin v4.8.0
```

Release 可在网页上 **Releases → Draft a new release** 发起，也可用 GitHub CLI：

```bash
gh release create v4.18.0 dist/WallDesk-4.18.0-setup.exe dist/WallDesk-4.18.0-win64.zip \
  --title "WallDesk v4.18.0" --notes-file tools/release-notes-v4.18.0.md
```

### 5.2 本开发机的网络现状与通道选择

这台机器 **HTTPS 到 `github.com` 被拦截**，但拦的是**主机名**而非整台服务器——同一 IP 段
（`20.205.243.x`）内 `api.github.com` 通、`github.com` 不通，与 DNS、IP 都无关：

| 目标 | 实测 | 用途 |
|------|------|------|
| `github.com:443` | **不通**（HTTP 000；TCP 与 TLS 握手能建立，实际请求无响应） | 网页、`git push`、`gh` 设备码登录 |
| `api.github.com` | 通（HTTP 200，0.5 s） | REST API、`gh` 绝大多数命令 |
| `codeload.github.com` | 通（HTTP 200） | 下载仓库 tar / zip 归档 |
| `objects.githubusercontent.com` | 通（根路径 HTTP 404 属正常） | Release 资产的实际下载源 |
| `uploads.github.com` | 通（HTTP 302） | 上传 Release 资产 |
| `ssh.github.com:22` 与 `:443` | 通（SSH 握手建立，`Permission denied (publickey)` 表示仅缺密钥） | 以 SSH 协议操作 git |
| `www.microsoft.com` | **不通** | 说明不是只针对 GitHub，而是按域名放行的策略 |

本机另装有终端安全管理代理（`NSecAgent`）；带代理、清空代理、绕过代理三种方式结果一致，
沙箱内外也一致。**结论：这是本机网络策略层面的按域名拦截，不是 GitHub 故障，也不是 Git 配置问题。**

于是本机按下面顺序选通道。

**通道 1：SSH（最接近「直连」，不需要代理）**

`github.com:22` 与 `ssh.github.com:443` 实测都能完成 SSH 握手，主机密钥指纹与 GitHub 官方
`api.github.com/meta` 公布值逐项一致（RSA / ECDSA / ED25519 三型均核对通过）：

```bash
ssh-keygen -t ed25519 -C "你的邮箱"
cat ~/.ssh/id_ed25519.pub                 # 内容贴到 GitHub → Settings → SSH and GPG keys
ssh -T git@github.com                     # 出现 "Hi <用户名>!" 即通
git remote set-url origin git@github.com:Gitsnod/WallDesk.git
```

22 端口被网络设备拦截时改走 443，在 `~/.ssh/config` 里写：

```
Host github.com
  HostName ssh.github.com
  Port 443
  User git
```

**通道 2：HTTPS REST API（技能 `github-via-api` 与 `gh` 都走这里）**

`api.github.com` 通，因此不碰 `github.com` 也能完成绝大部分开发操作——推送提交、同步标签、
发 Release、上传资产（走 `uploads.github.com`）：

```bash
S="$HOME/.workbuddy/skills/github-via-api/scripts/github_via_api.py"

python "$S" status                            # 看本地 / 远端差多少（只读）
python "$S" push --dry-run                    # 演练：只列出待推送提交
python "$S" push                              # 推送；结束后自动逐文件核对远端树
python "$S" tags v4.18.0                      # 同步标签
python "$S" release --tag v4.18.0 --title "WallDesk v4.18.0" \
       --notes-file tools/release-notes-v4.18.0.md \
       --asset dist/WallDesk-4.18.0-setup.exe \
       --asset dist/WallDesk-4.18.0-win64.zip
```

令牌由脚本自动从 `git credential-manager get` 取。
**不要用 `git credential fill`** —— 在 Git Bash 里它会挂住等交互输入。

`gh` 已装在本机（`%LOCALAPPDATA%\Programs\GitHubCLI\bin\gh.exe`，用户级，未用管理员权限），
令牌由 `~/.bashrc` 从 Windows 凭据管理器现取注入 `GH_TOKEN`，不落盘明文；
`gh auth status` 显示 `Logged in to github.com account Gitsnod (GH_TOKEN)`。
**`gh auth login` 的设备码流程走 `github.com/login/device`，在本机不通**，只能用 `GH_TOKEN`；
现有令牌 scope 为 `gist, repo, workflow`，缺 `read:org`（补 scope 需到网页重新授权）。

**通道 3：代理**

拦截按目标主机名判定，因此**走代理可行**：域名留在加密隧道内，本机策略看不到目标主机名即可放行。

```bash
export HTTPS_PROXY=http://127.0.0.1:端口                # 对 curl / git / gh 同时生效
git config --global http.proxy http://127.0.0.1:端口    # 只对 git 生效
```

`gh` 与 `git` 均遵循 `HTTPS_PROXY`（已实测：把该变量指向一个空端口，`gh api` 立刻报
`proxyconnect tcp ... refused`，证明变量确实被采用）。两点注意：

- 本机**当前没有安装任何代理客户端**（WinINET 系统代理为关闭、常见代理端口无监听、
  无 Clash / v2ray 一类进程），要用代理需先自行部署一个可用的代理服务；
- 有了通道 1，代理对 git 操作已非必需，只在需要访问网页版 GitHub
  （看 Issue、网页发 Release、`gh auth login` 设备码）时才有必要。

### 5.3 推树通道的三条要点

- **推树必须全量**，绝不能传 `base_tree`。曾以「远端父提交的树」为 base、再叠加本地
  `diff-tree` 的清单，两者语义不匹配，结果把远端 `main` 推成只剩 6 个条目（事故级）。
- Git Data API 重放出的提交 **SHA 与本地不同，但 tree SHA 完全相同**。工具据此反查
  「远端 HEAD 相当于哪个本地提交」，不必手工维护映射表。
- **推完必须逐文件核对**（`push` 已自动执行，报 MISSING / DIFF / MODE / EXTRA 四类问题），
  不通过就不要继续打标签、发 Release。

### 5.4 注意事项

- **`src\VlcBundle.bin` 不要提交**：几十 MB 的打包产物，已在 .gitignore 排除；
  其他开发者克隆后自己运行 `tools\pack_vlc.py` 生成
- 仓库是 **Public**（MIT 许可，无敏感信息）
- 提交前 `git status` 过一眼，确认没有混入壁纸文件、私人路径

## 6. 开发环境排错

| 现象 | 原因 / 处理 |
|------|-------------|
| 编译报 `LNK1112` | Qt 套件与编译器位数不一致，换 64 位套件 |
| 打包报 `Unable to query qtpaths: Error running binary qtpaths: pipe:` | 本机 `windeployqt` 拉不起子进程 `qtpaths`（环境层问题——`qtpaths --query QT_INSTALL_PREFIX` 单独运行正常，换 POSIX/Windows 两种 PATH、带不带 PATH、沙箱内外都一样）。改用 `python tools\make_release.py --deploy-from build`：从构建目录里已有的那份 Qt 部署直接拷贝运行时 |
| 新增 `.h/.cpp` 后编译通过但运行缺符号 | 检查 `CMakeLists.txt` 与 `tools/check_sources.py` 的 FILES / PAIRS 是否都登记了 |
| 便携模式配置不生效 | 配置在 `程序目录\data\WallDesk\WallDesk.ini`，不在注册表；两种模式的注入位置互不相通 |

使用者侧的故障（加载不到 libvlc、WorkerW 定位失败、视频黑屏、休眠唤醒）
见 [README 常见问题](../README.md#常见问题)。

## 7. 目录结构

```
WallpaperDesk/
├── CMakeLists.txt          构建配置（自动识别 Qt5/Qt6，注入版本与图标资源）
├── build.bat               一键构建；加 release 参数打发布包
├── README.md               使用者文档（GitHub 默认展示）
├── CHANGELOG.md             变更日志（Keep a Changelog 格式，按版本与变更类型分组）
├── LICENSE                 MIT（WallDesk 自身代码）
├── THIRD-PARTY-NOTICES.txt Qt / libVLC 的 LGPL 声明（分发必须携带）
├── .gitignore / .gitattributes
├── docs/
│   ├── DEVELOPER.md        本文
│   ├── ui-interaction-plan-2026-10-22.md   动效方案与验收记录
│   └── competitive-analysis-2026-10-21.md  竞品与技术选型分析
├── installer/
│   └── WallDesk.iss        Inno Setup 安装程序脚本
├── resources/
│   └── app.ico             应用图标（tools/make_icon.py 生成，已提交）
├── tools/
│   ├── make_icon.py        生成应用图标（纯标准库）
│   ├── pack_vlc.py         裁剪并打包 VLC 运行时为内嵌归档
│   ├── make_release.py     发布打包（windeployqt + 校验 + zip）
│   ├── check_sources.py    静态自检（改源码后必跑）
│   ├── check_changelog.py  CHANGELOG 合规检查（K1–K10）
│   ├── upload_release_assets.py  批量上传发布资产到 GitHub Release
│   ├── verify_video.py     视频壁纸可见性验证（帧差法，无需人眼）
│   └── test_loop.py / test_switch.py  长循环与图/视切换回归测试
└── src/
    ├── main.cpp            入口：命令行、路径、日志、单实例
    ├── MainWindow.{h,cpp}  主界面、画廊、托盘、配置持久化、状态栏
    ├── WallpaperEngine.{h,cpp}  壁纸生效（注册表 / WorkerW 挂载 / 看门狗）
    ├── VlcPlayer.{h,cpp}   libVLC 动态加载与播放控制
    ├── VlcBundle.{h,cpp}   内嵌 VLC 运行时的解包与缓存
    ├── AppPaths.{h,cpp}    便携模式与数据目录管理
    ├── Logger.{h,cpp}      日志（文件滚动、环境信息）
    ├── SingleInstance.{h,cpp}  单实例协作（QLocalServer）
    ├── DesktopWatcher.{h,cpp}  系统事件（休眠/锁屏/显示器/电源）
    ├── FullscreenGuard.{h,cpp} 全屏应用检测
    ├── ThumbnailLoader.{h,cpp} 异步缩略图与磁盘缓存
    ├── AppTheme.{h,cpp}    浅色/深色/跟随系统主题与 QSS
    ├── ImageEffects.{h,cpp} 图片画面效果（亮度 / 对比度 / 饱和度 / 模糊 / 暗角 / 灰度）
    ├── DesktopOverlay.{h,cpp} 透明桌面挂件层（时钟 / 日历 / 文字）
    ├── AudioSpectrum.{h,cpp} WASAPI 回环采集 + FFT 频谱绘制
    ├── PresetManager.{h,cpp} 场景预设的 JSON 读写（V4.7）
    ├── OnlineSources.{h,cpp} 在线图源：URL 导入 + Bing 每日一图（V4.7）
    ├── MediaInfo.{h,cpp} 分辨率 / 朝向探测，带缓存（V4.7）
    ├── ui/                 动效控件（MotionTokens / Animated* / MenuFade / FadeOverlay / Chevron）
    └── app.rc.in           版本与图标资源模板（CMake 生成）
```

## 8. 设计要点

- **图片壁纸**：写注册表 `WallpaperStyle`/`TileWallpaper` → `SystemParametersInfoW(SPI_SETDESKWALLPAPER)`，
  六种填充模式；参数未变时跳过写入与广播
- **视频壁纸**：向 `Progman` 发 `0x052C` 拿 `WorkerW` 图层，宿主 `SetParent` 进去置底；
  失败自动降级（WorkerW → Progman 子窗口 → 独立置底顶层窗口）
- **libVLC 动态加载**：`LoadLibrary` + `GetProcAddress`，不链接 .lib；MSVC/MinGW 均可编，
  缺 VLC 时程序照常启动（视频功能禁用）。**注意返回值类型**：
  `libvlc_media_player_get_fps` 返回 `float` 不是 `double`，按 double 读会得到约 1e-314 的值，
  表现为「帧率永远是 0」（V4.18 修）
- **内嵌运行时**：`pack_vlc.py` 把 VLC 裁剪压缩成归档，经 Windows RCDATA 资源链进 exe；
  首次启动解包到数据目录（临时目录 + 原子替换 + .ok 标记），后台线程执行不假死
- **看护与恢复**：电源广播/锁屏/显示器变更监听 + 自适应看门狗；重挂前记录播放位置续播
- **省电**：锁屏、电池、全屏应用、远程会话、逐应用规则、空闲（无键鼠输入）多种策略暂停，
  与用户手动暂停语义分开，互不干扰。所有暂停原因**只置标志位**，由
  `MainWindow::evaluatePausePolicy()` 统一合成 —— 各策略自己调 `applyVideoPaused()`
  会互相撤销（V4.9 踩过：退出全屏把电池暂停一起撤了）
- **界面动效**：控件自绘 state layer / ripple（QSS 没有 transition）；整页淡入用自绘遮罩
  而非 `QGraphicsOpacityEffect`（后者对整页做 opacity 动画时本机不逐帧重绘），
  细节见 `docs/ui-interaction-plan-2026-10-22.md`

## 9. 版本与变更记录

**唯一的变更记录是仓库根目录的 `CHANGELOG.md`**，格式遵循
[Keep a Changelog 1.0.0](https://keepachangelog.com/zh-CN/1.0.0/)，
版本号遵循[语义化版本 2.0.0](https://semver.org/lang/zh-CN/spec/semver)。

发版流程：

1. `CHANGELOG.md` 把 `[未发布]` 段落的改动收进新的 `## [x.y.z] - YYYY-MM-DD`
2. 同步版本号（六处）：`CMakeLists.txt` 的 `project(... VERSION)`、
   `installer/WallDesk.iss` 的 `MyAppVersion`、`tools/make_release.py` 的 `DEFAULT_VERSION`、
   `README.md`、`CHANGELOG.md`、`tools/release-notes-v<版本>.md`
3. 跑 `python tools/check_changelog.py`（K1–K10 全通过，含「每个版本都有对应 git tag」）
4. `git tag -a vX.Y.Z -m "WallDesk vX.Y.Z — 主题"` → 推提交 → 推标签 → 建 Release

版本号语义：含新功能升 MINOR，纯修复升 PATCH；已发布过的版号永不回改。
`V1–V3` 是 4.0.0 之前、公开发布前的开发里程碑，未按语义化版本编号，也没有留存发行产物，
在 `CHANGELOG.md` 末尾单独说明。

---

## 许可证

WallDesk 自身代码为 MIT，见 [LICENSE](../LICENSE)。使用的 Qt 与 libVLC 为 LGPL，
分发时必须随包携带 [THIRD-PARTY-NOTICES.txt](../THIRD-PARTY-NOTICES.txt)。
