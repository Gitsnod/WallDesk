## WallDesk v4.11.1

一个小版本，只改一件事：缩略图缓存从 PNG 换成 WebP，磁盘占用与读盘时间都降下来。

### 变更

- **缓存格式 PNG → WebP**：同一张缩略图实测 11,592 → 2,528 字节（约 22%）。
  壁纸库几百张时，缓存目录能少占一大半空间。
- **老缓存自动迁移**：已有的 PNG 缩略图在首次读取时转成 WebP 并删除旧文件，
  不用手动清缓存，也不会重复生成。
- **随包新增 Qt WebP 插件**（`imageformats/qwebp.dll`，取自 Qt 6.11.2 官方 qtimageformats 模块，
  已核对官方 sha1）。第三方许可声明同步更新。
- 启动日志多一行 `缩略图缓存格式：webp`，可直接确认插件是否加载成功。

### 兼容性

- 没有 WebP 写插件时会自动退回 PNG，功能与之前完全一致（只是占空间大些）。
- 配置文件、预设、播放与暂停逻辑均未改动。

### 安装

- 安装包：`WallDesk-4.11.1-setup.exe`（安装到用户目录，无需管理员权限）
- 绿色版：`WallDesk-4.11.1-win64.zip`，解压即用；便携模式在 exe 同目录放空的 `portable.ini`。

完整变更记录见 [CHANGELOG.md](https://github.com/Gitsnod/WallDesk/blob/main/CHANGELOG.md)。
