#pragma once

#include <QString>

/**
 * 媒体基础信息探测（V4.7 新增）。
 *
 * 动机：壁纸库一大，「找一张竖屏图给副屏」这种需求靠文件名搜索根本没法做。
 * 这里只取「分辨率 + 宽高比」这一档信息——它足够支撑分组筛选，
 * 又不需要解码整张图。
 *
 * 实现取舍：
 *   1. 只读文件头，不构造 QImage。QImageReader::size() 对 JPEG/PNG/GIF/BMP/WebP
 *      都是看头部字段，实测几毫秒级；相比全量解码（几十毫秒到大图几百毫秒）
 *      便宜两个数量级，可以在有几百项的库上直接同步跑；
 *   2. 常见格式走 QImageReader，PNG 额外做一次手工兜底——某些带异常 chunk 的
 *      PNG 会让 Qt 插件返回无效尺寸，此时手工解析 IHDR 反而稳；
 *   3. 探测结果按「路径 + 大小 + 修改时间」缓存，文件没变就不重复读盘；
 *   4. 失败返回 invalid 的 MediaInfo，界面按「未知」处理，不影响其它分组。
 */
struct MediaInfo {
    int width = 0;
    int height = 0;
    bool ok = false;

    bool valid() const { return ok && width > 0 && height > 0; }
};

/** 宽高比分类，用于「按朝向」筛选。 */
enum class Orientation {
    Landscape = 0, // 横向（宽 > 高）
    Portrait = 1,  // 纵向（高 > 宽）
    Square = 2     // 方形（差异在 6% 以内）
};

/** 分辨率档位，用于「按分辨率」筛选。 */
enum class ResolutionTier {
    Unknown = 0,
    Sd = 1,   // 短边 < 720
    Hd = 2,   // 短边 < 1080
    Fhd = 3,  // 短边 < 1440
    Qhd = 4,  // 短边 < 2160
    Uhd = 5   // 短边 >= 2160
};

class MediaInfoProbe {
public:
    /** 探测媒体尺寸。结果带缓存；失败返回 ok=false。 */
    static MediaInfo probe(const QString& path);

    /** 清空缓存（库清空或手动刷新时调用）。 */
    static void clearCache();

    /** 缓存条目数，供「关于 / 日志」观测。 */
    static int cacheSize();

    static Orientation orientationOf(const MediaInfo& info);
    static ResolutionTier tierOf(const MediaInfo& info);

    static QString orientationLabel(Orientation value);
    static QString tierLabel(ResolutionTier value);
};
