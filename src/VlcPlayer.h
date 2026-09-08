#pragma once

#include <QString>
#include <QStringList>
#include <QtGlobal>
#include <windows.h>

/** 播放状态，与 libvlc_state_t 取值一一对应。 */
enum class PlaybackState {
    Nothing = 0,   // 空闲
    Opening = 1,   // 打开中
    Buffering = 2, // 缓冲中
    Playing = 3,   // 播放中
    Paused = 4,    // 已暂停
    Stopped = 5,   // 已停止
    Ended = 6,     // 播放结束
    Error = 7,     // 出错
    Unknown = 8    // 未加载或取值越界
};

/** 解码档位：在画质、CPU 占用与兼容性之间取不同的平衡点。 */
enum class VlcProfile {
    Auto = 0,     // 默认：丢帧保流畅，解码方式交给 VLC 判断
    Hardware = 1, // 强制硬件解码 + D3D11 输出，CPU 占用最低
    Software = 2, // 强制软件解码 + GDI 输出，兼容性最好
    Quality = 3   // 不丢帧，画质优先，CPU 占用略高
};

/**
 * libVLC 动态加载器。
 *
 * 设计要点：不链接 libvlc.lib / libvlc.dll.a，而是在运行时 LoadLibrary("libvlc.dll")
 * 并逐个 GetProcAddress 取函数地址。好处：
 *   1. MSVC / MinGW 两套工具链都能编译，不需要为导入库格式操心；
 *   2. 缺少 libVLC 时程序照常启动，只是视频壁纸功能不可用（自动降级为纯图片壁纸）；
 *   3. 构建期零第三方依赖，CMake 不需要配置 VLC SDK 路径。
 *
 * V3 变更：
 *   1. 加载源新增「程序内置的 VLC 运行时」（见 VlcBundle），优先级最高；
 *   2. 支持解码档位（VlcProfile），可按机器配置在画质与 CPU 占用之间取舍；
 *   3. 支持离线抓帧，用于生成视频缩略图（使用独立实例，不影响正在播放的壁纸）。
 *
 * 所有 libvlc 句柄统一用 void* 表示，因此本文件不 include <vlc/vlc.h>。
 */
class VlcPlayer {
public:
    VlcPlayer();
    ~VlcPlayer();

    /**
     * 载入 libvlc.dll。搜索顺序：程序内置运行时 → hintDir → 程序目录 → 注册表
     * InstallDir → VLC 常见安装目录 → 系统 PATH。
     */
    bool load(const QString& hintDir, QString* err = nullptr);
    /** 显式指定目录加载（供界面「手动定位」按钮使用）。 */
    bool loadFromDirectory(const QString& dir, QString* err = nullptr);
    bool isLoaded() const { return m_dll != nullptr; }

    /** 实际加载到的 libvlc.dll 全路径，用于界面回显与排障。 */
    QString libraryPath() const { return m_libPath; }
    QString pluginPath() const { return m_pluginDir; }

    /** 设置解码档位。已加载时会在下次播放生效（libvlc 实例参数不能热改）。 */
    void setProfile(VlcProfile profile);
    VlcProfile profile() const { return m_profile; }

    /** 在指定窗口句柄上播放媒体文件。hwnd 为宿主窗口（HWND）。 */
    bool play(const QString& file, void* hwnd, int volume, bool loop);
    /**
     * 从头重播当前媒体：只 seek 到 0 再 play，不 stop、不重建 media。
     * 用于片尾循环，避免重建解码链路造成的黑帧与卡顿。
     */
    bool restart();
    void stop();
    /** 停止并释放当前媒体（解码缓冲随之归还），用于切到图片壁纸后省内存。 */
    void releaseMedia();
    void setPaused(bool paused);
    void setVolume(int volume);
    bool isPlaying() const;

    /** 播放状态查询：用于「黑屏但状态正常」这类故障的自动判定。 */
    PlaybackState state() const;
    qint64 time() const;        // 当前播放位置（毫秒），用于休眠唤醒后续播
    void setTime(qint64 ms);
    qint64 length() const;      // 总时长（毫秒）；<=0 表示媒体尚未解析成功

    /**
     * 离线抓帧：用独立实例把视频某一帧存成 png，用于生成缩略图。
     * 与主播放实例互不干扰；任何一步超时或失败都返回 false，调用方降级即可。
     */
    bool takeSnapshot(const QString& file, const QString& outPng, int width, int height);

    void release();

private:
    template <typename FnPtr>
    bool bind(FnPtr& fn, const char* symbol);
    bool tryLoadLibrary(const QString& dir, QString* err);
    bool bindAll();
    bool createInstance();
    QStringList instanceArguments() const;

    HMODULE m_dll = nullptr;
    HMODULE m_coreDll = nullptr; // libvlccore.dll（显式目录加载时先行载入，见 cpp 注释）
    QString m_libPath;    // 命中的 libvlc.dll 路径
    QString m_pluginDir;  // 命中的 plugins 目录
    VlcProfile m_profile = VlcProfile::Auto;
    void* m_instance = nullptr; // libvlc_instance_t*
    void* m_player = nullptr;   // libvlc_media_player_t*

    // libvlc C API 函数指针
    typedef void* (*PfnNew)(int argc, const char* const* argv);
    typedef void  (*PfnRelease)(void*);
    typedef void* (*PfnMediaNewPath)(void* instance, const char* path);
    typedef void  (*PfnMediaAddOption)(void* media, const char* option);
    typedef void* (*PfnPlayerNew)(void* instance);
    typedef void  (*PfnPlayerSetMedia)(void* player, void* media);
    typedef void  (*PfnPlayerSetHwnd)(void* player, void* hwnd);
    typedef int   (*PfnPlayerPlay)(void*);
    typedef void  (*PfnPlayerStop)(void*);
    typedef void  (*PfnPlayerSetPause)(void* player, int pause);
    typedef int   (*PfnPlayerIsPlaying)(void*);
    typedef void  (*PfnAudioSetVolume)(void* player, int volume);
    typedef void  (*PfnVideoSetInput)(void* player, unsigned on);
    typedef int   (*PfnPlayerGetState)(void* player);
    typedef qint64 (*PfnPlayerGetTime)(void* player);
    typedef void  (*PfnPlayerSetTime)(void* player, qint64 time);
    typedef qint64 (*PfnPlayerGetLength)(void* player);
    typedef int   (*PfnVideoTakeSnapshot)(void* player, unsigned num, const char* path,
                                          unsigned width, unsigned height);

    PfnNew             libvlc_new_ = nullptr;
    PfnRelease         libvlc_release_ = nullptr;
    PfnMediaNewPath    libvlc_media_new_path_ = nullptr;
    PfnRelease         libvlc_media_release_ = nullptr;
    PfnMediaAddOption  libvlc_media_add_option_ = nullptr;
    PfnPlayerNew       libvlc_media_player_new_ = nullptr;
    PfnRelease         libvlc_media_player_release_ = nullptr;
    PfnPlayerSetMedia  libvlc_media_player_set_media_ = nullptr;
    PfnPlayerSetHwnd   libvlc_media_player_set_hwnd_ = nullptr;
    PfnPlayerPlay      libvlc_media_player_play_ = nullptr;
    PfnPlayerStop      libvlc_media_player_stop_ = nullptr;
    PfnPlayerSetPause  libvlc_media_player_set_pause_ = nullptr;
    PfnPlayerIsPlaying libvlc_media_player_is_playing_ = nullptr;
    PfnAudioSetVolume  libvlc_audio_set_volume_ = nullptr;
    PfnVideoSetInput   libvlc_video_set_mouse_input_ = nullptr;
    PfnVideoSetInput   libvlc_video_set_key_input_ = nullptr;
    PfnPlayerGetState  libvlc_media_player_get_state_ = nullptr;
    PfnPlayerGetTime   libvlc_media_player_get_time_ = nullptr;
    PfnPlayerSetTime   libvlc_media_player_set_time_ = nullptr;
    PfnPlayerGetLength libvlc_media_player_get_length_ = nullptr;
    PfnVideoTakeSnapshot libvlc_video_take_snapshot_ = nullptr;
};
