#include "VlcPlayer.h"

#include "VlcBundle.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QSettings>
#include <QtGlobal>
#include <algorithm>
#include <string>

namespace {

/**
 * libvlc.dll 候选目录，按优先级排列。
 * 顺序：程序内置运行时 → 显式指定目录 → 程序目录 → 注册表 InstallDir
 * → 常见安装目录 → PATH（由调用方兜底）。
 */
QStringList candidateDirectories(const QString& hintDir)
{
    QStringList dirs;
    const auto push = [&dirs](const QString& dir) {
        if (!dir.isEmpty() && !dirs.contains(dir, Qt::CaseInsensitive)) {
            dirs.append(dir);
        }
    };

    // V3：exe 内嵌的运行时优先级最高——它才是「单文件可用」的保证
    push(VlcBundle::runtimeDir());

    push(hintDir);
    push(QCoreApplication::applicationDirPath());

    for (const wchar_t* key : {L"HKEY_LOCAL_MACHINE\\SOFTWARE\\VideoLAN\\VLC",
                               L"HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\VideoLAN\\VLC"}) {
        QSettings reg(QString::fromWCharArray(key), QSettings::NativeFormat);
        push(reg.value(QStringLiteral("InstallDir")).toString());
    }

    const QString pf = qEnvironmentVariable("ProgramFiles");
    const QString pf86 = qEnvironmentVariable("ProgramFiles(x86)");
    push(pf + QStringLiteral("/VideoLAN/VLC"));
    push(pf86 + QStringLiteral("/VideoLAN/VLC"));

    return dirs;
}

/**
 * 泵送本线程消息。
 * 抓帧时宿主窗口由工作线程创建，VLC 会通过 SendMessage 与它通信；
 * 该线程没有 Qt 事件循环，必须手动派发消息，否则 VLC 会一直等不到响应。
 */
void pumpMessages(int ms)
{
    MSG msg;
    const DWORD deadline = GetTickCount() + static_cast<DWORD>(ms);
    while (GetTickCount() < deadline) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(10);
    }
}

constexpr int kStatePlaying = 3;
constexpr int kStateError = 7;

} // namespace

VlcPlayer::VlcPlayer() = default;

VlcPlayer::~VlcPlayer()
{
    if (m_abandoned) {
        return; // 后台恢复线程可能还在这个播放器上，交给进程退出时回收
    }
    release();
}

template <typename FnPtr>
bool VlcPlayer::bind(FnPtr& fn, const char* symbol)
{
    fn = reinterpret_cast<FnPtr>(reinterpret_cast<void*>(GetProcAddress(m_dll, symbol)));
    return fn != nullptr;
}

bool VlcPlayer::load(const QString& hintDir, QString* err)
{
    if (m_dll) {
        return true;
    }

    QStringList tried;
    for (const QString& dir : candidateDirectories(hintDir)) {
        tried.append(dir);
        if (tryLoadLibrary(dir, nullptr)) {
            return true;
        }
    }
    // 最后交给系统 PATH 搜索
    if (tryLoadLibrary(QString(), nullptr)) {
        return true;
    }

    if (err) {
        *err = QStringLiteral("未找到可用的 libVLC（最后错误码 %1）。已尝试目录：%2。"
                              "修复方式：本程序若未内嵌 VLC 运行时，请把 VLC 3.0.x 的 "
                              "libvlc.dll、libvlccore.dll、plugins 复制到程序目录，"
                              "或点击主界面「定位 libvlc.dll」手动指定。")
                   .arg(GetLastError())
                   .arg(tried.join(QStringLiteral("、")));
    }
    return false;
}

bool VlcPlayer::loadFromDirectory(const QString& dir, QString* err)
{
    release();
    return tryLoadLibrary(dir, err);
}

bool VlcPlayer::tryLoadLibrary(const QString& dir, QString* err)
{
    // 显式目录加载时，libvlc.dll 的依赖（libvlccore.dll）默认不会从该目录解析——
    // Windows 只搜程序目录/系统目录/PATH，这正是错误码 126（模块未找到）的根因。
    // 用 LOAD_WITH_ALTERED_SEARCH_PATH 让加载器改搜 DLL 自身所在目录，
    // 并先显式加载 libvlccore.dll，双保险。
    QString libPath = QStringLiteral("libvlc.dll");
    DWORD loadFlags = 0;
    if (!dir.isEmpty()) {
        const QString nativeDir = QDir::toNativeSeparators(dir);
        const std::wstring corePath =
            (nativeDir + QStringLiteral("\\libvlccore.dll")).toStdWString();
        m_coreDll = LoadLibraryExW(corePath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        // core 加载失败不立即返回：某些目录结构下 libvlc 可能静态内联 core，交给下面统一判
        libPath = nativeDir + QStringLiteral("\\libvlc.dll");
        loadFlags = LOAD_WITH_ALTERED_SEARCH_PATH;
    }
    const std::wstring widePath = libPath.toStdWString();
    m_dll = LoadLibraryExW(widePath.c_str(), nullptr, loadFlags);

    if (!m_dll) {
        if (err) {
            *err = QStringLiteral("未能加载 %1（错误码 %2）。").arg(libPath).arg(GetLastError());
        }
        if (m_coreDll) {
            FreeLibrary(m_coreDll);
            m_coreDll = nullptr;
        }
        return false;
    }

    if (!bindAll()) {
        if (err) {
            *err = QStringLiteral("libvlc.dll 中缺少期望的导出函数，请确认版本为 VLC 3.0.x（64 位）。");
        }
        FreeLibrary(m_dll);
        m_dll = nullptr;
        if (m_coreDll) {
            FreeLibrary(m_coreDll);
            m_coreDll = nullptr;
        }
        return false;
    }

    // 记录实际命中的路径，便于界面回显
    wchar_t modulePath[MAX_PATH] = {0};
    if (GetModuleFileNameW(m_dll, modulePath, MAX_PATH) > 0) {
        m_libPath = QDir::fromNativeSeparators(QString::fromWCharArray(modulePath));
    } else {
        m_libPath = libPath;
    }

    const QString plugins = QFileInfo(m_libPath).dir().filePath(QStringLiteral("plugins"));
    m_pluginDir = QFileInfo::exists(plugins) ? plugins : QString();

    if (!createInstance()) {
        if (err) {
            *err = QStringLiteral("libvlc 实例创建失败，通常是 plugins 目录缺失或与 libvlc.dll 版本不匹配。");
        }
        FreeLibrary(m_dll);
        m_dll = nullptr;
        if (m_coreDll) {
            FreeLibrary(m_coreDll);
            m_coreDll = nullptr;
        }
        return false;
    }

    return true;
}

bool VlcPlayer::bindAll()
{
    bool ok = true;
    ok &= bind(libvlc_new_, "libvlc_new");
    ok &= bind(libvlc_release_, "libvlc_release");
    ok &= bind(libvlc_media_new_path_, "libvlc_media_new_path");
    ok &= bind(libvlc_media_release_, "libvlc_media_release");
    ok &= bind(libvlc_media_add_option_, "libvlc_media_add_option");
    ok &= bind(libvlc_media_player_new_, "libvlc_media_player_new");
    ok &= bind(libvlc_media_player_release_, "libvlc_media_player_release");
    ok &= bind(libvlc_media_player_set_media_, "libvlc_media_player_set_media");
    ok &= bind(libvlc_media_player_set_hwnd_, "libvlc_media_player_set_hwnd");
    ok &= bind(libvlc_media_player_play_, "libvlc_media_player_play");
    ok &= bind(libvlc_media_player_stop_, "libvlc_media_player_stop");
    ok &= bind(libvlc_media_player_set_pause_, "libvlc_media_player_set_pause");
    ok &= bind(libvlc_media_player_is_playing_, "libvlc_media_player_is_playing");
    ok &= bind(libvlc_audio_set_volume_, "libvlc_audio_set_volume");
    ok &= bind(libvlc_video_set_mouse_input_, "libvlc_video_set_mouse_input");
    ok &= bind(libvlc_video_set_key_input_, "libvlc_video_set_key_input");
    ok &= bind(libvlc_media_player_get_state_, "libvlc_media_player_get_state");
    ok &= bind(libvlc_media_player_get_time_, "libvlc_media_player_get_time");
    ok &= bind(libvlc_media_player_set_time_, "libvlc_media_player_set_time");
    ok &= bind(libvlc_media_player_get_length_, "libvlc_media_player_get_length");
    ok &= bind(libvlc_video_take_snapshot_, "libvlc_video_take_snapshot");
    // 画面效果接口：老版本 libVLC 没有它，缺失时静默降级（仅图片效果可用）
    bind(libvlc_video_set_adjust_float_, "libvlc_video_set_adjust_float");
    return ok;
}

void VlcPlayer::setEffect(const ImageEffect& fx)
{
    m_effect = fx;
    if (!m_player || !libvlc_video_set_adjust_float_) {
        return;
    }
    // libvlc_video_adjust_option_t：0=Enable 1=Contrast 2=Brightness 3=Hue
    //                             4=Saturation 5=Gamma
    const bool on = !fx.isDefault();
    libvlc_video_set_adjust_float_(m_player, 0, on ? 1.0f : 0.0f);
    if (!on) {
        return;
    }
    libvlc_video_set_adjust_float_(m_player, 1, 1.0f + fx.contrast / 100.0f);
    libvlc_video_set_adjust_float_(m_player, 2, 1.0f + fx.brightness / 100.0f);
    libvlc_video_set_adjust_float_(
        m_player, 4,
        fx.grayscale ? 0.0f : std::clamp(1.0f + fx.saturation / 100.0f, 0.0f, 2.0f));
}

void VlcPlayer::setProfile(VlcProfile profile)
{
    m_profile = profile;
}

QStringList VlcPlayer::instanceArguments() const
{
    QStringList args;
    args << QStringLiteral("--no-video-title-show")
         << QStringLiteral("--no-video-deco")
         << QStringLiteral("--no-osd")
         << QStringLiteral("--no-stats")
         << QStringLiteral("--quiet")
         << QStringLiteral("--aout=mmdevice")
         // 不扫描同名字幕、不联网抓元数据：省掉两次无谓 IO，冷启动更快
         << QStringLiteral("--no-sub-autodetect-file")
         << QStringLiteral("--no-metadata-network-access")
         << QStringLiteral("--file-caching=300")
         << QStringLiteral("--network-caching=300")
         << QStringLiteral("--clock-jitter=0");

    switch (m_profile) {
    case VlcProfile::Auto:
        // 卡顿时宁可丢帧也不拖慢系统
        args << QStringLiteral("--drop-late-frames") << QStringLiteral("--skip-frames");
        break;
    case VlcProfile::Hardware:
        args << QStringLiteral("--avcodec-hw=any") << QStringLiteral("--vout=direct3d11")
             << QStringLiteral("--drop-late-frames") << QStringLiteral("--skip-frames");
        break;
    case VlcProfile::Software:
        // 虚拟机 / 老显卡 / 远程桌面下硬件解码常失败，这里强制走 CPU
        args << QStringLiteral("--avcodec-hw=none") << QStringLiteral("--vout=wingdi")
             << QStringLiteral("--drop-late-frames");
        break;
    case VlcProfile::Quality:
        args << QStringLiteral("--avcodec-hw=any") << QStringLiteral("--vout=direct3d11")
             << QStringLiteral("--avcodec-threads=0");
        break;
    }
    return args;
}

bool VlcPlayer::createInstance()
{
    const QStringList arguments = instanceArguments();

    QList<QByteArray> storage;
    for (const QString& arg : arguments) {
        storage.append(arg.toUtf8());
    }
    QByteArray pluginArg;
    if (!m_pluginDir.isEmpty() && QFileInfo::exists(m_pluginDir)) {
        pluginArg = QByteArray("--plugin-path=") + QDir::toNativeSeparators(m_pluginDir).toUtf8();
        storage.append(pluginArg);
    }

    QList<const char*> argv;
    for (const QByteArray& a : storage) {
        argv.append(a.constData());
    }

    m_instance = libvlc_new_(static_cast<int>(argv.size()), argv.data());
    return m_instance != nullptr;
}

bool VlcPlayer::play(const QString& file, void* hwnd, int volume, bool loop)
{
    if (!m_dll || !m_instance) {
        return false;
    }

    stop(); // 干净地重置播放器状态

    void* media = libvlc_media_new_path_(m_instance, QDir::toNativeSeparators(file).toUtf8().constData());
    if (!media) {
        return false;
    }

    if (loop) {
        libvlc_media_add_option_(media, "input-repeat=-1"); // -1 = 无限循环
        libvlc_media_add_option_(media, ":input-fast-seek=1"); // seek 到 0 更快，片尾衔接更顺
    }
    // 只对本地文件播放做最小缓冲，降低起播延迟
    libvlc_media_add_option_(media, ":file-caching=300");
    if (m_effect.blur > 0) {
        // 视频模糊依赖 VLC 的 gaussianblur 滤镜；与亮度/对比度可以同时生效
        libvlc_media_add_option_(media, ":video-filter=gaussianblur");
        libvlc_media_add_option_(media, QStringLiteral(":gaussianblur-sigma=%1")
                                            .arg(qMax(1, m_effect.blur / 2))
                                            .toUtf8()
                                            .constData());
    }

    if (!m_player) {
        m_player = libvlc_media_player_new_(m_instance);
    }
    if (!m_player) {
        libvlc_media_release_(media);
        return false;
    }

    libvlc_media_player_set_media_(m_player, media);
    libvlc_media_release_(media); // set_media 内部已持有引用

    // 关闭 VLC 自身对鼠标/键盘的接管，避免影响桌面交互
    libvlc_video_set_mouse_input_(m_player, 0);
    libvlc_video_set_key_input_(m_player, 0);

    libvlc_media_player_set_hwnd_(m_player, hwnd);
    libvlc_audio_set_volume_(m_player, volume);

    const bool started = libvlc_media_player_play_(m_player) == 0;
    if (started) {
        setEffect(m_effect); // 起播后套用画面效果（滤镜要等 input 就绪）
    }
    return started;
}

bool VlcPlayer::restart()
{
    if (!m_player || !libvlc_media_player_play_) {
        return false;
    }
    // 播完（Ended）后播放器里残留一个已结束的 input，此时 set_time + play
    // 不会重新起播（实测：状态每秒仍是 Ended，画面冻在末帧）。
    // 先 stop 回收该 input，再 play 才会从头重新解码播放。
    if (libvlc_media_player_stop_) {
        libvlc_media_player_stop_(m_player);
    }
    return libvlc_media_player_play_(m_player) == 0;
}

void VlcPlayer::stop()
{
    if (m_player) {
        libvlc_media_player_stop_(m_player);
    }
}

void VlcPlayer::releaseMedia()
{
    if (!m_player || !libvlc_media_player_set_media_) {
        return;
    }
    libvlc_media_player_stop_(m_player);
    // 置空媒体会连带释放解码器与缓冲，壁纸停在图片时可省下几十 MB
    libvlc_media_player_set_media_(m_player, nullptr);
}

void VlcPlayer::setPaused(bool paused)
{
    if (m_player) {
        libvlc_media_player_set_pause_(m_player, paused ? 1 : 0);
    }
}

void VlcPlayer::setVolume(int volume)
{
    if (m_player) {
        libvlc_audio_set_volume_(m_player, qBound(0, volume, 100));
    }
}

bool VlcPlayer::isPlaying() const
{
    return m_player && libvlc_media_player_is_playing_(m_player) == 1;
}

PlaybackState VlcPlayer::state() const
{
    if (!m_player || !libvlc_media_player_get_state_) {
        return PlaybackState::Unknown;
    }
    switch (libvlc_media_player_get_state_(m_player)) {
    case 0: return PlaybackState::Nothing;
    case 1: return PlaybackState::Opening;
    case 2: return PlaybackState::Buffering;
    case 3: return PlaybackState::Playing;
    case 4: return PlaybackState::Paused;
    case 5: return PlaybackState::Stopped;
    case 6: return PlaybackState::Ended;
    case 7: return PlaybackState::Error;
    default: return PlaybackState::Unknown;
    }
}

qint64 VlcPlayer::time() const
{
    return (m_player && libvlc_media_player_get_time_) ? libvlc_media_player_get_time_(m_player) : -1;
}

void VlcPlayer::setTime(qint64 ms)
{
    if (m_player && libvlc_media_player_set_time_ && ms >= 0) {
        libvlc_media_player_set_time_(m_player, ms);
    }
}

qint64 VlcPlayer::length() const
{
    return (m_player && libvlc_media_player_get_length_) ? libvlc_media_player_get_length_(m_player) : -1;
}

bool VlcPlayer::takeSnapshot(const QString& file, const QString& outPng, int width, int height)
{
    if (!m_dll || !libvlc_video_take_snapshot_ || !QFileInfo::exists(file)) {
        return false;
    }
    QFile::remove(outPng);

    // 独立实例：绝不影响正在播放的壁纸（主实例参数与播放状态都不动）
    QStringList args = instanceArguments();
    args.removeAll(QStringLiteral("--aout=mmdevice"));
    args << QStringLiteral("--aout=dummy")
         << QStringLiteral("--vout=wingdi")     // GDI 输出，无显卡环境也能出图
         << QStringLiteral("--no-snapshot-preview");

    QList<QByteArray> storage;
    for (const QString& arg : args) {
        storage.append(arg.toUtf8());
    }
    QByteArray pluginArg;
    if (!m_pluginDir.isEmpty()) {
        pluginArg = QByteArray("--plugin-path=") + QDir::toNativeSeparators(m_pluginDir).toUtf8();
        storage.append(pluginArg);
    }
    QList<const char*> argv;
    for (const QByteArray& a : storage) {
        argv.append(a.constData());
    }

    void* instance = libvlc_new_(static_cast<int>(argv.size()), argv.data());
    if (!instance) {
        return false;
    }

    bool ok = false;
    // 宿主窗口：系统预定义 STATIC 类，隐藏即可，只为给 vout 一个渲染目标
    HWND host = CreateWindowExW(0, L"STATIC", L"WallDeskSnapshot", WS_POPUP, 0, 0,
                                width, height, nullptr, nullptr, nullptr, nullptr);
    if (host) {
        void* media = libvlc_media_new_path_(instance, QDir::toNativeSeparators(file).toUtf8().constData());
        void* player = media ? libvlc_media_player_new_(instance) : nullptr;
        if (media && player) {
            libvlc_media_player_set_media_(player, media);
            libvlc_media_release_(media);
            libvlc_media_player_set_hwnd_(player, host);
            libvlc_audio_set_volume_(player, 0);
            libvlc_video_set_mouse_input_(player, 0);
            libvlc_video_set_key_input_(player, 0);
            libvlc_media_player_play_(player);

            // 等待进入 Playing（最多 5 秒）；期间泵送消息，保证 VLC 的窗口通信不被阻塞
            for (int i = 0; i < 50; ++i) {
                const int current = libvlc_media_player_get_state_(player);
                if (current == kStatePlaying || current == kStateError) {
                    break;
                }
                pumpMessages(100);
            }

            if (libvlc_media_player_get_state_(player) == kStatePlaying) {
                const qint64 total = libvlc_media_player_get_length_(player);
                // 跳到 10% 处：片头常是黑场或纯色，取这里更有代表性
                if (total > 3000) {
                    libvlc_media_player_set_time_(player, total / 10);
                    pumpMessages(600);
                }
                pumpMessages(400);

                const QByteArray target = QDir::toNativeSeparators(outPng).toUtf8();
                ok = libvlc_video_take_snapshot_(player, 0, target.constData(),
                                                 static_cast<unsigned>(width),
                                                 static_cast<unsigned>(height)) == 0;
            }
            libvlc_media_player_stop_(player);
            libvlc_media_player_release_(player);
        } else if (media) {
            libvlc_media_release_(media);
        }
        DestroyWindow(host);
    }

    libvlc_release_(instance);
    return ok && QFileInfo::exists(outPng);
}

void VlcPlayer::release()
{
    if (m_player) {
        libvlc_media_player_stop_(m_player);
        libvlc_media_player_release_(m_player);
        m_player = nullptr;
    }
    if (m_instance) {
        libvlc_release_(m_instance);
        m_instance = nullptr;
    }
    if (m_dll) {
        FreeLibrary(m_dll);
        m_dll = nullptr;
    }
    if (m_coreDll) {
        FreeLibrary(m_coreDll);
        m_coreDll = nullptr;
    }
}
