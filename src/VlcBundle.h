#pragma once

#include <QObject>
#include <QString>
#include <functional>

/**
 * 内置 libVLC 运行时（V3 新增，可选特性）。
 *
 * 目标：让 WallDesk.exe 成为单文件——用户不必安装 VLC，也不用手动放 DLL。
 *
 * 实现方式：构建前用 tools/pack_vlc.py 把 VLC 运行时裁剪、zlib 压缩成一个归档，
 * 通过 Windows 资源（RCDATA）嵌入 exe；程序首次启动时把归档解包到
 * %LOCALAPPDATA%\WallDesk\vlc\<指纹>\ 并缓存，之后直接复用。
 *
 * 为什么不是静态链接 libvlc：VLC 官方不发布 Windows 静态库，且其插件体系
 * （plugins 目录下数百个 dll）必须在磁盘上存在才能加载，静态链接在技术上不可行。
 * 「内嵌资源 + 首次自解压」是在保持单文件分发前提下的等价方案。
 *
 * 未内嵌（未执行打包脚本）时本模块整体失效，程序自动回退到外部搜索逻辑，
 * 见 VlcPlayer::load 的搜索链。
 */
class VlcBundle {
public:
    /** 是否内嵌了 VLC 运行时：编译期宏 + 运行时资源探测同时成立才算。 */
    static bool available();

    /** 内嵌包的指纹（版本号或内容指纹），用作缓存目录名。 */
    static QString fingerprint();

    /** 解包目标目录：%LOCALAPPDATA%\WallDesk\vlc\<指纹>。 */
    static QString cacheDir();

    /** 缓存目录是否已完整就绪（存在 .ok 标记且指纹匹配）。 */
    static bool isPrepared();

    /**
     * 同步解包。首次调用需要解压数十 MB，耗时约 1-4 秒，
     * 必须在后台线程调用；界面请用 prepareAsync。
     */
    static bool prepare(QString* err = nullptr);

    /**
     * 异步解包：内部用 std::async 执行，完成后在主线程回调。
     * context 用于绑定生命周期；context 销毁后回调不再触发，不会访问野指针。
     */
    static void prepareAsync(QObject* context,
                             std::function<void(bool ok, const QString& err)> callback);

    /** 解包后的运行时目录（含 libvlc.dll）；未就绪返回空字符串。 */
    static QString runtimeDir();

    /** 清理旧指纹遗留的缓存目录，返回释放的字节数。可在后台线程调用。 */
    static qint64 purgeStaleCaches();
};
