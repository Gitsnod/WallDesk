# WallDesk — Windows 图片 / 视频壁纸工具（C++）

管理图片与视频清单，一键切换桌面壁纸；视频壁纸挂载到桌面 `WorkerW` 图层
（位于桌面图标**下方**，不拦截鼠标、不影响右键菜单）。

技术栈：C++17 + Qt Widgets + Win32 API + libVLC（运行时动态加载，可内嵌进 exe）。

> 已发布：<https://github.com/Gitsnod/WallDesk> —— 安装程序与绿色版见
> [Releases](https://github.com/Gitsnod/WallDesk/releases)。

**当前版本：V4（发行版）**。在 V3（内嵌 VLC + 画廊界面 + 性能优化）基础上补齐
「发给别人用」所需的一切：发布打包脚本、Inno Setup 安装程序、应用图标与版本资源、
日志系统、单实例协作、便携模式。版本演进见 [九、版本变更](#九版本变更)。

---

## 一、需要安装什么（环境清单）

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

## 二、构建

1. 修改 `build.bat` 顶部四个变量：`QT_DIR`、`MINGW_BIN`、`VLC_DIR`、`PYTHON`
2. 双击 `build.bat`，产物 `build\WallDesk.exe`
3. 打发布包：`build.bat release`（自动跑 windeployqt + 生成 zip + 校验清单）

`build.bat` 会自动做三件事：检查并生成应用图标（`tools/make_icon.py`）、
打包内嵌 VLC 运行时（`tools/pack_vlc.py`，装了 VLC 才生效）、配置编译部署。
不打包 VLC 也能编译，视频功能回退到外部搜索（程序目录 / 注册表 / PATH）。

## 三、运行与使用

- 双击运行：主界面出现，添加图片 / 视频后点「应用选中」或双击画廊项
- 命令行参数：
  - `WallDesk.exe --minimized`：静默驻留托盘（开机自启用的就是它）
  - `WallDesk.exe --next`：通知已运行的实例切换下一张
  - `WallDesk.exe --quit`：通知已运行的实例退出
  - `WallDesk.exe --portable`：本次强制便携模式
  - `WallDesk.exe --verbose`：日志中记录调试信息
- **单实例**：重复启动不会开第二个窗口，只会把已运行的窗口唤醒（或执行上面两条指令）
- **便携模式**：界面「其它」分组里勾选，或在 exe 同目录放一个空的 `portable.ini`。
  启用后配置与数据全部保存在程序目录的 `data\` 下，不写注册表，整个文件夹拷 U 盘带走
- **日志**：`数据目录\logs\WallDesk.log`，超过 2 MB 自动滚动。「关于」对话框可一键打开

## 四、发布给其他用户（三种形态）

### 形态 1：绿色压缩包（最简单）

```bat
build.bat release
```

产物：`dist\WallDesk-4.0.0-win64.zip`（约 25–60 MB，取决于是否内嵌 VLC）。
用户解压即运行，无需安装 Qt / VLC / 运行时库。包内含：

- `WallDesk.exe` + Qt 运行时 DLL（windeployqt 部署）
- `LICENSE` / `README.md` / `THIRD-PARTY-NOTICES.txt`
- `BUILDINFO.txt`（版本、构建时间、主程序 SHA256）
- `SHA256SUMS.txt`（全部文件校验，用户可核对完整性）

### 形态 2：安装程序（给非技术用户）

1. 先执行形态 1 生成 `dist\WallDesk-4.0.0-win64\` 目录
2. 安装 [Inno Setup 6](https://jrsoftware.org/isdl.php)（免费，约 5 MB）
3. 命令行执行 `iscc installer\WallDesk.iss`，或在 Inno 里打开该脚本按 F9

产物：`dist\WallDesk-4.0.0-setup.exe`。安装到用户目录（无需管理员权限），
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

## 五、上传到 GitHub（步骤）

### 5.1 首次上传

```bash
cd WallpaperDesk

git init                          # 初始化仓库（只需一次）
git add .                         # 暂存全部文件（.gitignore 已排除构建产物）
git status                        # 确认 src/VlcBundle.bin 等不在列表里
git commit -m "WallDesk v4.0.0: 图片/视频壁纸，内嵌 VLC，便携模式"

# 在 GitHub 网页上新建空仓库（不要勾选初始化 README），然后：
git branch -M main
git remote add origin https://github.com/<你的用户名>/WallDesk.git
git push -u origin main
```

### 5.2 发布 Release（把构建产物挂到 GitHub 供下载）

```bash
# 打标签
git tag -a v4.0.0 -m "WallDesk 4.0.0"
git push origin v4.0.0
```

然后到 GitHub 仓库页 → **Releases** → **Draft a new release**：
- Tag 选 `v4.0.0`，标题 `WallDesk 4.0.0`
- 上传 `dist\WallDesk-4.0.0-win64.zip` 和（可选）`WallDesk-4.0.0-setup.exe`
- 描述里粘贴「更新内容」+ 校验值（从 SHA256SUMS.txt 取主程序那一行）
- 点 **Publish release**

有 GitHub CLI（`winget install GitHub.cli`）的话一条命令搞定：

```bash
gh release create v4.0.0 dist\WallDesk-4.0.0-win64.zip dist\WallDesk-4.0.0-setup.exe ^
  --title "WallDesk 4.0.0" --notes "见 README 九、版本变更"
```

### 5.3 日常更新

```bash
git add -A && git commit -m "说明本次改动" && git push
```

### 注意事项

- **`src\VlcBundle.bin` 不要提交**：几十 MB 的打包产物，已在 .gitignore 排除；
  其他开发者克隆后自己运行 `tools\pack_vlc.py` 生成
- 仓库建议设为 **Public**（MIT 许可，无敏感信息）；内部使用就设 Private
- 提交前 `git status` 过一眼，确认没有混入壁纸文件、私人路径

## 六、已知限制与排查

| 现象 | 原因 / V4 的处理方式 |
|------|----------------------|
| 「未能加载 libvlc.dll」 | 未内嵌且外部搜索失败；用界面「定位 libvlc」手动指定，或改用内嵌构建 |
| 「未能定位桌面 WorkerW 图层」 | 第三方桌面美化工具占用；重启 `explorer.exe`；失败时自动降级为顶层置底窗口 |
| 视频黑屏但状态显示已应用 | 编码不受支持（HEVC/AV1 需硬件解码）；切「软件解码」档位或换 H.264 测试 |
| 编译报 `LNK1112` | Qt 套件与编译器位数不一致，换 64 位套件 |
| 休眠唤醒后视频消失 | 已自动处理：监听电源广播自动重挂续播，另有看门狗兜底 |
| 资源管理器重启后失效 | 看门狗每 2–8 秒校验，异常自动重挂 |
| 用户说「装了打不开」 | 让其打开 `数据目录\logs\WallDesk.log`，首行即有系统与 Qt 版本 |
| 视频壁纸耗电 | 物理规律；默认已启用「锁屏暂停」「全屏暂停」，笔记本可再开「电池暂停」 |

数据目录：安装模式 `%LOCALAPPDATA%\WallDesk`；便携模式 `程序目录\data`。

## 七、目录结构

```
WallpaperDesk/
├── CMakeLists.txt          构建配置（自动识别 Qt5/Qt6，注入版本与图标资源）
├── build.bat               一键构建；加 release 参数打发布包
├── README.md
├── LICENSE                 MIT（WallDesk 自身代码）
├── THIRD-PARTY-NOTICES.txt Qt / libVLC 的 LGPL 声明（分发必须携带）
├── .gitignore / .gitattributes
├── installer/
│   └── WallDesk.iss        Inno Setup 安装程序脚本
├── resources/
│   └── app.ico             应用图标（tools/make_icon.py 生成，已提交）
├── tools/
│   ├── make_icon.py        生成应用图标（纯标准库）
│   ├── pack_vlc.py         裁剪并打包 VLC 运行时为内嵌归档
│   ├── make_release.py     发布打包（windeployqt + 校验 + zip）
│   └── check_sources.py    静态自检（改源码后必跑）
└── src/
    ├── main.cpp            入口：命令行、路径、日志、单实例
    ├── MainWindow.{h,cpp}  主界面、画廊、托盘、配置持久化
    ├── WallpaperEngine.{h,cpp}  壁纸生效（注册表 / WorkerW 挂载 / 看门狗）
    ├── VlcPlayer.{h,cpp}   libVLC 动态加载与播放控制
    ├── VlcBundle.{h,cpp}   内嵌 VLC 运行时的解包与缓存
    ├── AppPaths.{h,cpp}    便携模式与数据目录管理
    ├── Logger.{h,cpp}      日志（文件滚动、环境信息）
    ├── SingleInstance.{h,cpp}  单实例协作（QLocalServer）
    ├── DesktopWatcher.{h,cpp}  系统事件（休眠/锁屏/显示器/电源）
    ├── FullscreenGuard.{h,cpp} 全屏应用检测
    ├── ThumbnailLoader.{h,cpp} 异步缩略图与磁盘缓存
    ├── AppTheme.{h,cpp}    浅色/深色/跟随系统主题
    └── app.rc.in           版本与图标资源模板（CMake 生成）
```

## 八、设计要点

- **图片壁纸**：写注册表 `WallpaperStyle`/`TileWallpaper` → `SystemParametersInfoW(SPI_SETDESKWALLPAPER)`，
  六种填充模式；参数未变时跳过写入与广播
- **视频壁纸**：向 `Progman` 发 `0x052C` 拿 `WorkerW` 图层，宿主 `SetParent` 进去置底；
  失败自动降级（WorkerW → Progman 子窗口 → 独立置底顶层窗口）
- **libVLC 动态加载**：`LoadLibrary` + `GetProcAddress`，不链接 .lib；MSVC/MinGW 均可编，
  缺 VLC 时程序照常启动（视频功能禁用）
- **内嵌运行时**：`pack_vlc.py` 把 VLC 裁剪压缩成归档，经 Windows RCDATA 资源链进 exe；
  首次启动解包到数据目录（临时目录 + 原子替换 + .ok 标记），后台线程执行不假死
- **看护与恢复**：电源广播/锁屏/显示器变更监听 + 自适应看门狗；重挂前记录播放位置续播
- **省电**：锁屏、电池、全屏应用三种策略暂停，与用户手动暂停语义分开，互不干扰

## 九、版本变更

| 版本 | 主要变更 |
|------|----------|
| V4 | 发布打包脚本（zip + SHA256 清单）、Inno Setup 安装程序、应用图标与版本资源、日志系统、单实例（--next/--quit）、便携模式、「关于」对话框；修复暂停按钮未创建（崩溃）、缺 `<future>` 头（编译失败） |
| V3 | libVLC 内嵌进 exe（打包脚本 + 运行时解包）、画廊式界面与三套主题、全屏暂停、解码档位、异步缩略图 |
| V2 | 系统事件监听（休眠/锁屏/电源/显示器）、自动重挂续播、降级挂载链、五级 libVLC 搜索、指定显示器 |
| V1 | 图片/视频壁纸基础功能、清单、定时切换、托盘、开机自启 |
