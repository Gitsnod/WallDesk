# Revision Ledger — WallDesk UI 交互动感 overhaul（2026-10-22）

## 当前状态
- 工程：WallpaperDesk（C++17 / Qt 6.11.2 / MinGW），单仓库多文件改造
- 任务：用户反馈「所有按钮没有交互动画，下拉框没有动画，样式也不对」→ 方案 → 实施
- 用户明确要求：**风格采用 Material 大圆角 + ripple**（2026-10-22 14:2x，对话中确认）
- 排除项：不动业务逻辑/信号槽；不引第三方控件库；不主动 git 提交
- 关联文档：`docs/ui-interaction-plan-2026-10-22.md`

## 冻结重开记录（DARP §6）
| 项 | 原决定 | 当时依据 | 新证据 | 处理 |
|---|---|---|---|---|
| 风格定位 | §3.2「Fluent 克制 + M3 反馈体系，圆角 8px，**ripple 不做**」 | 初版判断「桌面软件语言不符 Android 水波纹」 | 用户明确选择「Material 大圆角 + ripple」 | **重新打开**，重走第一步~第三步后同步全文 |

## 已确认修改（方案文档，第一轮传播+预演稳定）
1. 头部：补修订注记（风格定为完整 Material + token 口径升级 M3 官方）
2. §1 第2条：模块行数 400→约 500 行；表述补 ripple
3. §1 第3条：qt-material 结论补「ripple 扩散时序可参考」
4. §2.3 偏差表第3行：按钮 8px → full 全圆角（M3 默认映射）；卡片 12px 保持（官方 caution：信息密集组件不用 full）
5. §3.1 token 表：M2 口径（150/100ms）→ **M3 官方 tokens-specs 口径**（Standard 300/250/200、Emphasized 500/400/200、精确贝塞尔）+ ripple 400ms/225ms/曲线(0,0,0.2,1)
6. §3.2 整段重写：完整 Material 定位
7. §4.1：AnimatedPushButton 描述补 ripple；行数 200→约 280
8. §4.2：状态机加 ripple 子状态（触点扩散 400ms / release 后 fade 200ms）
9. §4.3：popup 时长升级（展开 250ms Std decelerate / 收起 200ms Std accelerate）
10. §4.4 QSS：按钮 border-radius 8px→20px（等效 full）；补光学圆整说明
11. §4.5：补 ripple repaint 成本同类论证
12. §5.1：行数同步
13. §5.2：新增 4 条 ripple 专属坑（触点记录/未完成 fade/主题刷新/disable 清理）
14. §6 P0 判据2：加「触点 ripple 圈 + 400ms 扩散淡出」截图判据
15. §7 来源：补 m3.material.io 官方 token 页；ripple 400ms 标「第三方转引 M3 规范」

## 已分析但无需修改
- §2.1 零动画证据表：风格变化不影响「QSS 无 transition」事实——无需修改
- §3.3 许可证结论：仍不引入任何库——无需修改
- §5.1 的 MainWindow 42 处构造点替换：与风格无关——无需修改

## 未确定事项
- M3 官方独立 ripple spec 页（material.io 改版后未检索到独立时长页）：400ms 为第三方转引值，文档已标注；行业实现一致（Compose/Web MDC 均 400ms 级），风险可接受
- 「减弱动画」关闭时 ripple 行为：直接落终态（无圈）——与 state layer 一致，无歧义

## 实施批次（P0，2026-10-22）
- 新文件：src/ui/MotionTokens.h、AnimatedPushButton.{h,cpp}、AnimatedComboBox.{h,cpp}
- 改文件：src/CMakeLists.txt（FILES/PAIRS）、src/MainWindow.cpp（构造点替换）、src/AppTheme.cpp（QSS）
- 验收：drive_ui.py 五判据 + ninja 编译 + check_sources.py
