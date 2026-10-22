#pragma once

#include <QEasingCurve>
#include <windows.h>

/**
 * 动效时长 / 曲线常量（Material 3 官方 token）。
 *
 * 数值来源见 docs/ui-interaction-plan-2026-10-22.md §3.1：
 *   - Standard 300ms / Standard decelerate 250ms / Standard accelerate 200ms
 *     （m3.material.io easing-and-duration tokens-specs 官方页）
 *   - ripple 400ms 扩散 / 225ms 最小按压 / 曲线 (0,0,0.2,1)
 *     （M3 规范的第三方转引值，Compose/Web MDC 实现一致）
 *
 * 所有时长统一过 duration()：系统「关闭动画效果」时归零，直接落终态。
 */
class MotionTokens {
public:
    /** state layer 进入/离开（hover，M3 Standard 300ms）。 */
    static constexpr int kStateLayerMs = 300;
    /** ripple 触点扩散全程（M3 ripple 400ms）。 */
    static constexpr int kRippleMs = 400;
    /** 抬手时 ripple 未完成的淡出（Standard accelerate 200ms）。 */
    static constexpr int kRippleFadeMs = 200;
    /** 短于此时长的按压不触发 ripple（M3 最小按压 225ms）。 */
    static constexpr int kRippleMinPressMs = 225;
    /** 下拉展开（Standard decelerate 250ms）。 */
    static constexpr int kPopupEnterMs = 250;
    /** 下拉收起（Standard accelerate 200ms）。 */
    static constexpr int kPopupExitMs = 200;

    /** state layer 强度：hover 8% / pressed 10%（M3 state layer）。 */
    static constexpr double kLayerHover = 0.08;
    static constexpr double kLayerPressed = 0.10;
    /** ripple 峰值透明度（M3 ripple 12%）。 */
    static constexpr double kRippleOpacity = 0.12;

    static QEasingCurve standard();           // (0.2, 0, 0, 1)
    static QEasingCurve standardDecelerate(); // (0, 0, 0, 1)
    static QEasingCurve standardAccelerate(); // (0.3, 0, 1, 1)
    static QEasingCurve rippleCurve();        // (0, 0, 0.2, 1)

    /** 系统「在 Windows 中显示动画」开关（SPI_GETCLIENTAREAANIMATION）。 */
    static bool animationsEnabled();

    /** 名义时长 → 实际时长：尊重系统开关，关闭时为 0。 */
    static int duration(int nominalMs);
};
