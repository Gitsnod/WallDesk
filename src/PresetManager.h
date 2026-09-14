#pragma once

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

#include "DesktopOverlay.h"
#include "ImageEffects.h"
#include "WallpaperEngine.h"

/**
 * 场景预设（V4.7 新增）。
 *
 * 动机：4.6 引入的多屏、画面效果、桌面挂件、频谱都是各自独立的开关，
 * 用户想从「工作」切到「游戏」得手动改七八处。这里把「一套完整的桌面状态」
 * 打包成一个命名预设，一键切换。
 *
 * 设计取舍：
 *   1. 预设存 JSON（不是 QSettings 数组）——结构本身就是嵌套的，
 *      JSON 读改删都比 QSettings 的 setArrayIndex 走位清楚，也便于后续做导入导出；
 *   2. 只收录「用户可以感知到的桌面外观与行为」，不含窗口几何、缩略图尺寸这类
 *      纯界面偏好——那些键每次导入都写一遍会污染配置；
 *   3. 不存在的键一律不写：这样旧预设可以被新版本读，不会把新加的开关重置。
 *
 * 存储位置：AppPaths::dataDir()/presets.json
 */
struct WallpaperPreset {
    QString name;
    QString description;

    // 壁纸
    QString wallpaperPath;
    int wallpaperType = 0; // 0 图片 / 1 视频

    // 图片 / 视频参数
    int fit = 0;           // ImageFit
    int screenTarget = 1;  // ScreenTarget
    int monitorIndex = 0;
    int volume = 0;
    int profile = 0;       // VlcProfile

    // 画面效果
    ImageEffect effect;

    // 桌面挂件与频谱
    // 用 DesktopOverlay::Settings 直接承载，字段一一对应，避免两处定义漂移。
    DesktopOverlay::Settings overlay;
    bool spectrumEnabled = false;
    int spectrumBands = 48;

    // 自动化
    bool autoSwitch = false;
    int intervalMinutes = 30;
    int switchMode = 0;   // 0 顺序 / 1 随机 / 2 不重复随机
    int switchScope = 0;  // 0 全部 / 1 仅图片 / 2 仅视频 / 3 仅收藏

    // 逐屏壁纸：显示器设备名 → 图片路径（空路径表示跟随主设置）
    QList<QPair<QString, QString>> monitors;
};

/**
 * 场景预设的读写。全部为静态方法，无实例状态。
 *
 * 文件格式：
 * {
 *   "version": 1,
 *   "presets": [
 *     { "name": "工作", "description": "...", "wallpaper": {...}, ... }
 *   ]
 * }
 */
class PresetManager {
public:
    /** 预设文件路径（数据目录下的 presets.json）。 */
    static QString filePath();

    /** 读取全部预设。文件缺失或损坏时返回空列表并写日志。 */
    static QList<WallpaperPreset> load(QString* err = nullptr);

    /** 整体覆写保存。返回是否成功。 */
    static bool save(const QList<WallpaperPreset>& presets, QString* err = nullptr);

    /** 追加或按名字覆盖一个预设。返回 false 表示写入失败。 */
    static bool upsert(const WallpaperPreset& preset, QString* err = nullptr);

    /** 按名字删除。返回 false 表示没找到或写入失败。 */
    static bool remove(const QString& name, QString* err = nullptr);

    /** 校验并规整名字：去空白、限长。返回 false 表示名字非法。 */
    static bool validateName(const QString& name, QString* reason = nullptr);

    /** 供界面提示用的路径缩写（数据目录下的相对名）。 */
    static QString displayPath();
};
