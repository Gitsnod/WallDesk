// WallDesk v4.7.0 单元验证：MediaInfo 朝向/档位判定 + PresetManager 存取往返。
// 只依赖 Qt Core，独立于主程序，用 qmake/CMake 单独编译运行。
#include "MediaInfo.h"
#include "PresetManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>

static int g_fail = 0;
static int g_pass = 0;

static void check(bool cond, const QString& what)
{
    if (cond) {
        ++g_pass;
        QTextStream(stdout) << "  PASS  " << what << "\n";
    } else {
        ++g_fail;
        QTextStream(stdout) << "  FAIL  " << what << "\n";
    }
}

static MediaInfo mk(int w, int h)
{
    MediaInfo m;
    m.width = w;
    m.height = h;
    m.ok = true;
    return m;
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);

    // ---- 朝向判定（含 6% 容差边界）----
    out << "[MediaInfo] 朝向判定\n";
    check(MediaInfoProbe::orientationOf(mk(1920, 1080)) == Orientation::Landscape,
          "1920x1080 -> 横向");
    check(MediaInfoProbe::orientationOf(mk(1080, 1920)) == Orientation::Portrait,
          "1080x1920 -> 纵向");
    check(MediaInfoProbe::orientationOf(mk(1920, 1920)) == Orientation::Square,
          "1920x1920 -> 方形（完全相等）");
    // 容差边界：差 6% 恰好算方形，差 7% 不算
    check(MediaInfoProbe::orientationOf(mk(1000, 940)) == Orientation::Square,
          "1000x940（差 6.0%）-> 方形（边界内）");
    check(MediaInfoProbe::orientationOf(mk(1000, 930)) == Orientation::Landscape,
          "1000x930（差 7.0%）-> 横向（边界外）");
    check(MediaInfoProbe::orientationOf(MediaInfo{}) == Orientation::Landscape,
          "无效尺寸 -> 横向兜底");

    // ---- 分辨率档位（按短边）----
    out << "[MediaInfo] 分辨率档位\n";
    check(MediaInfoProbe::tierOf(mk(3840, 2160)) == ResolutionTier::Uhd, "3840x2160 -> UHD");
    check(MediaInfoProbe::tierOf(mk(2560, 1440)) == ResolutionTier::Qhd, "2560x1440 -> QHD");
    check(MediaInfoProbe::tierOf(mk(1920, 1080)) == ResolutionTier::Fhd, "1920x1080 -> FHD");
    check(MediaInfoProbe::tierOf(mk(1280, 720)) == ResolutionTier::Hd, "1280x720 -> HD");
    check(MediaInfoProbe::tierOf(mk(854, 480)) == ResolutionTier::Sd, "854x480 -> SD");
    // 竖屏与横屏同档（按短边判定）
    check(MediaInfoProbe::tierOf(mk(1080, 1920)) == ResolutionTier::Fhd,
          "1080x1920 -> FHD（与横屏同档）");
    check(MediaInfoProbe::tierOf(MediaInfo{}) == ResolutionTier::Unknown, "无效尺寸 -> Unknown");
    // 边界：短边恰好 2160 / 1440 / 1080 / 720
    check(MediaInfoProbe::tierOf(mk(2160, 2160)) == ResolutionTier::Uhd, "短边恰 2160 -> UHD");
    check(MediaInfoProbe::tierOf(mk(2159, 2159)) == ResolutionTier::Qhd, "短边 2159 -> QHD");
    check(MediaInfoProbe::tierOf(mk(1439, 1439)) == ResolutionTier::Fhd, "短边 1439 -> FHD");
    check(MediaInfoProbe::tierOf(mk(1079, 1079)) == ResolutionTier::Hd, "短边 1079 -> HD");
    check(MediaInfoProbe::tierOf(mk(719, 719)) == ResolutionTier::Sd, "短边 719 -> SD");

    // ---- 真实文件探测（PNG 走 IHDR 手工解析兜底）----
    out << "[MediaInfo] 真实文件探测\n";
    const QString png = QDir::tempPath() + "/wd_probe_test.png";
    {
        // 手工构造一个 3x2 的合法 PNG（IHDR 宽高为 3、2）
        static const unsigned char pngBytes[] = {
            0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, // 签名
            0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, // 长度 + IHDR
            0x00, 0x00, 0x00, 0x03,                         // 宽 = 3
            0x00, 0x00, 0x00, 0x02,                         // 高 = 2
            0x08, 0x02, 0x00, 0x00, 0x00,                   // 位深等
            0x00, 0x00, 0x00, 0x00                          // CRC 占位（探测不校验）
        };
        QFile f(png);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(reinterpret_cast<const char*>(pngBytes), sizeof(pngBytes));
            f.close();
        }
    }
    MediaInfoProbe::clearCache();
    const MediaInfo probed = MediaInfoProbe::probe(png);
    check(probed.ok && probed.width == 3 && probed.height == 2,
          QString("PNG 探测 3x2 -> 实得 %1x%2").arg(probed.width).arg(probed.height));
    check(MediaInfoProbe::cacheSize() >= 1, "探测后缓存非空");
    const int before = MediaInfoProbe::cacheSize();
    MediaInfoProbe::probe(png); // 命中缓存
    check(MediaInfoProbe::cacheSize() == before, "重复探测不增缓存条目");
    check(MediaInfoProbe::orientationOf(probed) == Orientation::Landscape, "探测结果 -> 横向");
    MediaInfoProbe::clearCache();
    check(MediaInfoProbe::cacheSize() == 0, "clearCache 后缓存归零");
    check(!MediaInfoProbe::probe(QDir::tempPath() + "/wd_not_exist_12345.png").ok,
          "不存在的文件 -> ok=false");
    QFile::remove(png);

    // ---- 预设名字校验 ----
    out << "[PresetManager] 名字校验\n";
    QString reason;
    check(PresetManager::validateName(QStringLiteral("我的场景"), &reason), "中文名合法");
    check(PresetManager::validateName(QStringLiteral("Scene-1"), nullptr), "英文数字连字符合法");
    check(!PresetManager::validateName(QString(), &reason), "空名字被拒");
    check(!PresetManager::validateName(QStringLiteral("   "), &reason), "全空白被拒");
    check(!PresetManager::validateName(QString(200, QLatin1Char('x')), &reason),
          "超长名字被拒");

    // ---- 预设存取往返 ----
    out << "[PresetManager] 存取往返\n";
    const QString presetPath = PresetManager::filePath();
    // 备份用户既有 presets.json，避免测试污染真实配置
    QByteArray backup;
    bool hadBackup = false;
    {
        QFile f(presetPath);
        if (f.exists() && f.open(QIODevice::ReadOnly)) {
            backup = f.readAll();
            hadBackup = true;
            f.close();
        }
    }

    const QList<WallpaperPreset> empty = PresetManager::load();
    const int baseline = empty.size();

    WallpaperPreset p1;
    p1.name = QStringLiteral("测试场景A");
    QString err;
    check(PresetManager::upsert(p1, &err), "写入预设A成功: " + err);
    WallpaperPreset p2;
    p2.name = QStringLiteral("测试场景B");
    check(PresetManager::upsert(p2, &err), "写入预设B成功: " + err);
    check(PresetManager::load().size() == baseline + 2, "读回数量 +2");

    // 同名覆盖不应新增条目
    WallpaperPreset p1b;
    p1b.name = QStringLiteral("测试场景A");
    check(PresetManager::upsert(p1b, &err), "同名覆盖成功");
    check(PresetManager::load().size() == baseline + 2, "同名覆盖不新增条目");

    check(PresetManager::remove(QStringLiteral("测试场景A"), &err), "删除预设A成功");
    check(PresetManager::load().size() == baseline + 1, "读回数量回到 +1");
    check(!PresetManager::remove(QStringLiteral("不存在的名字"), &err), "删除不存在 -> false");
    PresetManager::remove(QStringLiteral("测试场景B"), &err);

    // 还原用户原文件
    if (hadBackup) {
        QFile f(presetPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.write(backup);
            f.close();
        }
    } else {
        QFile::remove(presetPath);
    }

    out << "\n结果: " << g_pass << " 通过, " << g_fail << " 失败\n";
    return g_fail == 0 ? 0 : 1;
}
