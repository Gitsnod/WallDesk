#include "PresetManager.h"

#include "AppPaths.h"
#include "Logger.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace {

constexpr int kFormatVersion = 1;
constexpr int kMaxNameLength = 40;
constexpr int kMaxPresets = 60;

/**
 * 只读取存在的键。
 *
 * 这么写而不是 QJsonObject::value(k).toInt() 直接取默认值，是为了让
 * 「旧预设 + 新增字段」组合正常工作：键缺失时保持结构体默认值，
 * 而不是被 JSON 里的 0 覆盖。写入侧（toJson）对称地只写需要表达的值。
 */
QJsonObject toJson(const WallpaperPreset& p)
{
    QJsonObject root;
    root[QStringLiteral("name")] = p.name;
    if (!p.description.isEmpty()) {
        root[QStringLiteral("description")] = p.description;
    }

    QJsonObject wallpaper;
    wallpaper[QStringLiteral("path")] = QDir::toNativeSeparators(p.wallpaperPath);
    wallpaper[QStringLiteral("type")] = p.wallpaperType;
    wallpaper[QStringLiteral("fit")] = p.fit;
    root[QStringLiteral("wallpaper")] = wallpaper;

    QJsonObject video;
    video[QStringLiteral("screenTarget")] = p.screenTarget;
    video[QStringLiteral("monitorIndex")] = p.monitorIndex;
    video[QStringLiteral("volume")] = p.volume;
    video[QStringLiteral("profile")] = p.profile;
    root[QStringLiteral("video")] = video;

    QJsonObject fx;
    fx[QStringLiteral("brightness")] = p.effect.brightness;
    fx[QStringLiteral("contrast")] = p.effect.contrast;
    fx[QStringLiteral("saturation")] = p.effect.saturation;
    fx[QStringLiteral("blur")] = p.effect.blur;
    fx[QStringLiteral("vignette")] = p.effect.vignette;
    fx[QStringLiteral("grayscale")] = p.effect.grayscale;
    root[QStringLiteral("effect")] = fx;

    QJsonObject overlay;
    overlay[QStringLiteral("enabled")] = p.overlay.enabled;
    overlay[QStringLiteral("clock")] = p.overlay.showClock;
    overlay[QStringLiteral("calendar")] = p.overlay.showCalendar;
    overlay[QStringLiteral("text")] = p.overlay.showText;
    overlay[QStringLiteral("textValue")] = p.overlay.text;
    overlay[QStringLiteral("position")] = p.overlay.position;
    overlay[QStringLiteral("screenIndex")] = p.overlay.screenIndex;
    overlay[QStringLiteral("fontSize")] = p.overlay.fontSize;
    root[QStringLiteral("overlay")] = overlay;

    root[QStringLiteral("spectrum")] = p.spectrumEnabled;
    root[QStringLiteral("spectrumBands")] = p.spectrumBands;

    QJsonObject automation;
    automation[QStringLiteral("autoSwitch")] = p.autoSwitch;
    automation[QStringLiteral("intervalMinutes")] = p.intervalMinutes;
    automation[QStringLiteral("switchMode")] = p.switchMode;
    automation[QStringLiteral("switchScope")] = p.switchScope;
    root[QStringLiteral("automation")] = automation;

    if (!p.monitors.isEmpty()) {
        QJsonArray monitors;
        for (const auto& pair : p.monitors) {
            QJsonObject entry;
            entry[QStringLiteral("device")] = pair.first;
            entry[QStringLiteral("path")] = QDir::toNativeSeparators(pair.second);
            monitors.append(entry);
        }
        root[QStringLiteral("monitors")] = monitors;
    }
    return root;
}

WallpaperPreset fromJson(const QJsonObject& root)
{
    WallpaperPreset p;
    p.name = root.value(QStringLiteral("name")).toString();
    p.description = root.value(QStringLiteral("description")).toString();

    const QJsonObject wallpaper = root.value(QStringLiteral("wallpaper")).toObject();
    p.wallpaperPath = wallpaper.value(QStringLiteral("path")).toString();
    // 历史文件里可能是 Qt 正斜杠形式，统一成系统分隔符便于与库内路径比对
    p.wallpaperPath = QDir::toNativeSeparators(p.wallpaperPath);
    p.wallpaperType = qBound(0, wallpaper.value(QStringLiteral("type")).toInt(), 1);
    p.fit = qBound(0, wallpaper.value(QStringLiteral("fit")).toInt(), 5);

    const QJsonObject video = root.value(QStringLiteral("video")).toObject();
    p.screenTarget = qBound(0, video.value(QStringLiteral("screenTarget")).toInt(1), 2);
    p.monitorIndex = qMax(0, video.value(QStringLiteral("monitorIndex")).toInt());
    p.volume = qBound(0, video.value(QStringLiteral("volume")).toInt(), 100);
    p.profile = qBound(0, video.value(QStringLiteral("profile")).toInt(), 3);

    const QJsonObject fx = root.value(QStringLiteral("effect")).toObject();
    p.effect.brightness = qBound(-100, fx.value(QStringLiteral("brightness")).toInt(), 100);
    p.effect.contrast = qBound(-100, fx.value(QStringLiteral("contrast")).toInt(), 100);
    p.effect.saturation = qBound(-100, fx.value(QStringLiteral("saturation")).toInt(), 100);
    p.effect.blur = qBound(0, fx.value(QStringLiteral("blur")).toInt(), 20);
    p.effect.vignette = qBound(0, fx.value(QStringLiteral("vignette")).toInt(), 100);
    p.effect.grayscale = fx.value(QStringLiteral("grayscale")).toBool();

    const QJsonObject overlay = root.value(QStringLiteral("overlay")).toObject();
    p.overlay.enabled = overlay.value(QStringLiteral("enabled")).toBool();
    p.overlay.showClock = overlay.value(QStringLiteral("clock")).toBool();
    p.overlay.showCalendar = overlay.value(QStringLiteral("calendar")).toBool();
    p.overlay.showText = overlay.value(QStringLiteral("text")).toBool();
    p.overlay.text = overlay.value(QStringLiteral("textValue")).toString();
    p.overlay.position = qBound(0, overlay.value(QStringLiteral("position")).toInt(1), 4);
    p.overlay.screenIndex = overlay.value(QStringLiteral("screenIndex")).toInt(-1);
    p.overlay.fontSize = qBound(12, overlay.value(QStringLiteral("fontSize")).toInt(30), 96);

    p.spectrumEnabled = root.value(QStringLiteral("spectrum")).toBool();
    p.spectrumBands = qBound(16, root.value(QStringLiteral("spectrumBands")).toInt(48), 128);

    const QJsonObject automation = root.value(QStringLiteral("automation")).toObject();
    p.autoSwitch = automation.value(QStringLiteral("autoSwitch")).toBool();
    p.intervalMinutes = qBound(1, automation.value(QStringLiteral("intervalMinutes")).toInt(30), 1440);
    p.switchMode = qBound(0, automation.value(QStringLiteral("switchMode")).toInt(), 2);
    p.switchScope = qBound(0, automation.value(QStringLiteral("switchScope")).toInt(), 3);

    const QJsonArray monitors = root.value(QStringLiteral("monitors")).toArray();
    for (const QJsonValue& value : monitors) {
        const QJsonObject entry = value.toObject();
        const QString device = entry.value(QStringLiteral("device")).toString();
        if (device.isEmpty()) {
            continue;
        }
        p.monitors.append({device, QDir::toNativeSeparators(entry.value(QStringLiteral("path")).toString())});
    }
    return p;
}

} // namespace

QString PresetManager::filePath()
{
    return QDir(AppPaths::dataDir()).filePath(QStringLiteral("presets.json"));
}

QString PresetManager::displayPath()
{
    return QStringLiteral("presets.json");
}

bool PresetManager::validateName(const QString& name, QString* reason)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        if (reason) {
            *reason = QStringLiteral("名称不能为空");
        }
        return false;
    }
    if (trimmed.size() > kMaxNameLength) {
        if (reason) {
            *reason = QStringLiteral("名称过长（最多 %1 个字符）").arg(kMaxNameLength);
        }
        return false;
    }
    // 换行会破坏列表展示
    if (trimmed.contains(QLatin1Char('\n')) || trimmed.contains(QLatin1Char('\r'))) {
        if (reason) {
            *reason = QStringLiteral("名称不能包含换行");
        }
        return false;
    }
    return true;
}

QList<WallpaperPreset> PresetManager::load(QString* err)
{
    const QString path = filePath();
    QFile file(path);
    if (!file.exists()) {
        return {}; // 还没有任何预设，不是错误
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (err) {
            *err = QStringLiteral("无法读取预设文件：%1").arg(path);
        }
        Logger::warning(QStringLiteral("打开预设文件失败：%1").arg(path));
        return {};
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();
    if (parseError.error != QJsonParseError::NoError) {
        if (err) {
            *err = QStringLiteral("预设文件格式损坏（偏移 %1）：%2")
                       .arg(parseError.offset)
                       .arg(parseError.errorString());
        }
        // 不覆盖写：留着损坏文件，用户可能还想人工抢救
        Logger::warning(QStringLiteral("预设文件解析失败：%1（%2）")
                            .arg(path, parseError.errorString()));
        return {};
    }
    if (!doc.isObject()) {
        if (err) {
            *err = QStringLiteral("预设文件根节点不是对象");
        }
        return {};
    }

    QList<WallpaperPreset> result;
    const QJsonArray array = doc.object().value(QStringLiteral("presets")).toArray();
    for (const QJsonValue& value : array) {
        if (!value.isObject()) {
            continue;
        }
        WallpaperPreset preset = fromJson(value.toObject());
        if (preset.name.trimmed().isEmpty()) {
            continue; // 无名预设无法在界面上引用，直接丢弃
        }
        result.append(preset);
    }
    Logger::info(QStringLiteral("已加载 %1 个场景预设").arg(result.size()));
    return result;
}

bool PresetManager::save(const QList<WallpaperPreset>& presets, QString* err)
{
    if (presets.size() > kMaxPresets) {
        if (err) {
            *err = QStringLiteral("预设数量已达上限（%1）").arg(kMaxPresets);
        }
        return false;
    }

    QJsonArray array;
    for (const WallpaperPreset& preset : presets) {
        array.append(toJson(preset));
    }
    QJsonObject root;
    root[QStringLiteral("version")] = kFormatVersion;
    root[QStringLiteral("presets")] = array;

    const QString path = filePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    // QSaveFile：先写临时文件再原子替换，中途断电不会留下半个 JSON
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (err) {
            *err = QStringLiteral("无法写入预设文件：%1").arg(file.errorString());
        }
        Logger::warning(QStringLiteral("保存预设失败：%1").arg(file.errorString()));
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (err) {
            *err = QStringLiteral("预设文件提交失败：%1").arg(file.errorString());
        }
        Logger::warning(QStringLiteral("提交预设文件失败：%1").arg(file.errorString()));
        return false;
    }
    return true;
}

bool PresetManager::upsert(const WallpaperPreset& preset, QString* err)
{
    QString reason;
    if (!validateName(preset.name, &reason)) {
        if (err) {
            *err = reason;
        }
        return false;
    }

    QList<WallpaperPreset> presets = load();
    WallpaperPreset normalized = preset;
    normalized.name = normalized.name.trimmed();

    bool replaced = false;
    for (int i = 0; i < presets.size(); ++i) {
        if (presets.at(i).name.compare(normalized.name, Qt::CaseInsensitive) == 0) {
            presets[i] = normalized; // 同名视为「更新」，保留原位置不跳到末尾
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        if (presets.size() >= kMaxPresets) {
            if (err) {
                *err = QStringLiteral("预设数量已达上限（%1），请先删除不用的").arg(kMaxPresets);
            }
            return false;
        }
        presets.append(normalized);
    }

    if (!save(presets, err)) {
        return false;
    }
    Logger::info(QStringLiteral("场景预设已%1：%2")
                     .arg(replaced ? QStringLiteral("更新") : QStringLiteral("保存"), normalized.name));
    return true;
}

bool PresetManager::remove(const QString& name, QString* err)
{
    QList<WallpaperPreset> presets = load();
    const int before = presets.size();
    for (int i = presets.size() - 1; i >= 0; --i) {
        if (presets.at(i).name.compare(name.trimmed(), Qt::CaseInsensitive) == 0) {
            presets.removeAt(i);
        }
    }
    if (presets.size() == before) {
        if (err) {
            *err = QStringLiteral("没有找到名为「%1」的预设").arg(name);
        }
        return false;
    }
    if (!save(presets, err)) {
        return false;
    }
    Logger::info(QStringLiteral("场景预设已删除：%1").arg(name));
    return true;
}
