# WallDesk UI 交互动感 overhaul 方案

> 日期：2026-10-22 · 状态：P0 实施中 · 范围：仅交互与样式，不动业务逻辑
>
> 用户反馈原话：「所有按钮没有交互动画，下拉框没有动画，样式也不对」。
>
> **2026-10-22 修订**：风格定为**完整 Material（大圆角 + ripple）**（用户确认选择），
> motion token 口径由 M2 旧值升级为 M3 官方 tokens-specs 当前值。

---

## 1. 结论（TL;DR）

1. **根因是架构性的**：整套界面皮肤 = 单张 QSS 样式表（`AppTheme.cpp`），而 **QSS 没有 transition/animation 概念**（已联网实证：QSS 不是 CSS，无 `transition` 属性），所有 hover/pressed 态都是伪类瞬时硬切。25 个按钮、17 个下拉框、19 个复选框、滑块、导航、菜单全部如此。
2. **方案**：新建轻量自绘动画控件模块（约 500 行、零新依赖），把「QSS 硬切」替换为「QVariantAnimation 插值 + state layer 叠加 + ripple 触点扩散」；下拉弹出做「淡入 + 下落」容器动画；顺带修正 10 项样式偏差。时长/曲线取 **Material 3 官方 motion token**（已核验精确数值）。
3. **不引第三方库**：调研 4 个候选，唯一许可证干净且带 ripple 动画的 qt-material 是 Python 包（ripple 在 Py 侧实现，无法直接引入 C++ 项目）；PyQt-Fluent-Widgets 为 GPL+商业，**许可证不合，排除**。
4. **分三阶段落地**，P0 约 500 行新代码 + 42 处构造点替换，每阶段有可机械验收的判据。
5. **P0 已完成并验收（2026-10-22）**：悬停 8% / 按压 10% / ripple 225ms / 下拉展开均按 M3 数值实测通过，
   详见 §6.0；唯一未取证项是 Tab 焦点环（本机键盘注入不可靠）。

---

## 2. 现状诊断（代码级证据）

### 2.1 零动画的证据

| 证据 | 位置 | 说明 |
|---|---|---|
| hover/pressed 瞬时硬切 | `AppTheme.cpp:99-110` | `QPushButton:hover { border-color/background 直接换 }`；`:pressed { background 直接换 }` |
| QSS 无过渡能力 | — | 联网实证（exchangetuts，2025-10）：QSS 不支持 `transition` 属性，只能换 QVariantAnimation/QPropertyAnimation |
| 全项目唯一动画 | `MainWindow.cpp:1574-1604` | 一处 `QParallelAnimationGroup` + opacity 淡入；且已正确检测 `SPI_GETCLIENTAREAANIMATION`（系统减弱动画偏好）——**这个基建可以直接复用** |
| 下拉零处理 | `AppTheme.cpp:113-130` | QComboBox 只设了外观；弹出走 Fusion 原生 `showPopup()`，瞬间出现/消失，popup 容器（`view()->parentWidget()`）无任何动画处理 |
| 控件规模 | `MainWindow.cpp` | 25 × QPushButton、17 × QComboBox、19 × QCheckBox，另有 QSlider ×3、导航 QListWidget、QMenu、状态胶囊 |

### 2.2 视觉实证（已启动开发副本截图核对）

截图确认界面整体是「静态平面风」：悬停只有边框变色、按压整块换底色、下拉直接弹出、无焦点环、复选框 16px 小方块硬切。截图存档于 `%TEMP%/bench/ui_*.png`（壁纸库/设置-壁纸/设置-外观与效果/悬停态）。

### 2.3 样式偏差清单（对照主流规范，按影响排序）

| # | 现状 | 主流做法 | 影响 |
|---|---|---|---|
| 1 | hover = 边框变色 | M3 **state layer**：悬停区叠 8% 主色，不换整块色 | 反馈弱且硬 |
| 2 | pressed = 整块背景硬切 | state layer 加深（10%）+ 内容微下沉 | 塑料感 |
| 3 | 按钮/输入框/下拉/卡片/菜单**全部 8px 圆角** | 形状分层（M3 shape scale）：按钮 **full 全圆角**（M3 默认映射）、输入框/菜单 8px、卡片 12px、弹窗 28px | 界面无层级语言，「样式不对」的主因之一 |
| 4 | 扁平无海拔 | Fluent：卡片/弹商用轻阴影分层（0-2px 模糊） | 主次按钮只差底色 |
| 5 | 无焦点环 | 键盘焦点必须有可见环（2px accent 50%） | 可达性缺陷 |
| 6 | 下拉箭头未主题化、不旋转 | 箭头随主题着色 + 展开时旋转 180° | 廉价感 |
| 7 | 复选框 16px、勾选硬切 | 18px + 勾绘过渡 120ms | 同上 |
| 8 | 控件高 ~30px（min-height 16 + padding 14 + 边框） | 桌面软件按钮 32-36px 高，触摸目标 ≥40px | 点击目标偏小 |
| 9 | 滚动条/滑块 handle 硬切 | 150ms 过渡 | 一致性质感缺口 |
| 10 | danger 按钮仅 hover 变红 | 常态 50% 红边可辨识 | 危险操作无预警 |

---

## 3. 对标规范与轮子调研

### 3.1 动效 token（取 Material 3 官方数值，已联网核验）

M3 官方 easing/duration tokens（m3.material.io tokens-specs 页，2026-10-22 核验）：

| Token | 时长 | 曲线 cubic-bezier | 用途 |
|---|---|---|---|
| Standard decelerate | 250ms | (0, 0, 0, 1) | 进入（下拉展开、菜单淡入） |
| Standard accelerate | 200ms | (0.3, 0, 1, 1) | 退出（下拉收起、ripple 淡出） |
| Standard | 300ms | (0.2, 0, 0, 1) | 状态切换默认（hover state layer） |
| Emphasized decelerate | 400ms | (0.05, 0.7, 0.1, 1) | 大元素进入（P2 场景转场） |
| Emphasized | 500ms | (0.2, 0, 0, 1) | 场景级过渡（P2） |
| **Ripple 扩散** | **400ms** | **(0, 0, 0.2, 1)** | 按压触点扩散至全按钮并淡出 |
| Ripple 最小按压 | 225ms | — | 短于此时长不触发 ripple |
| State layer hover | — | — | 主色 8% 叠加 |
| State layer pressed | — | — | 主色 10% 叠加 |

> Qt 侧用 `QEasingCurve::BezierSpline` 精确复刻上述三次贝塞尔（float 控制点直读）。
> ripple 400ms/225ms/曲线为 M3 规范的第三方转引值（docs.rs uzor-animation 标注
> Source: Material Design 3 ripple specifications；Compose/Web MDC 实现一致），
> 官方现行独立 ripple spec 页未检索到，已在 §7 标注来源级别。

### 3.2 风格定位：完整 Material（大圆角 + ripple）

用户已确认选择**完整 Material 方向**：大圆角按钮 + ripple 触点扩散 + state layer。
这不是 Android 移植——M3 的 buttons specs 页明确「Shape: Round (default)」即按钮默认
全圆角，是 M3 桌面端实现（如 ChromeOS、新版 Google 桌面端应用）的既有语言。落地口径：

- **圆角**：按钮走 M3 默认 full（WallDesk 按钮高约 30px，实现取 20px radius 等效 full）；
  输入框/菜单 8px（small）、卡片 12px（medium，官方 caution：信息密集组件不用 full）、
  弹窗 28px（extra large）。形状分层语言 = M3 shape scale。
- **ripple**：按压触点扩散（400ms，曲线 (0,0,0.2,1)），最小按压 225ms 内抬手不触发；
  颜色按底色分流——主按钮（primary 深底）用 on-primary 白色涟漪，
  次按钮（surface 浅底）用 primary 色涟漪（M3 on-container 规则）。
- **state layer 保留**：hover 8% / pressed 10%，与 ripple 叠加不互斥。
- **克制项**：不做物理弹簧（M3 Expressive motion physics 未成熟落地桌面 Qt）；
  阴影只做弹窗/下拉容器一层（0-2px 模糊），不用 M3 elevation 的五级体系。

### 3.3 轮子调研与许可证闸门结论

| 候选 | 许可证 | 结论 |
|---|---|---|
| **qt-material**（UN-GCPDS） | BSD-2-Clause ✅ | Py 包，QSS 主题 + ripple；**ripple 是 Python 侧实现，无法引入 C++ 项目**；整体换肤会覆盖我们精调的双主题。→ 只参考其 QSS 结构与 state layer 时序，**不引入** |
| **laserpants/qt-material-widgets** | BSD ✅ | C++ Material 控件，但多年不活跃、控件集与 Fusion 基座不匹配。→ 不引入 |
| **PyQt-Fluent-Widgets**（zhiyiYo） | **GPL + 商业** ❌ | **许可证不合，排除**（会传染开源项目） |
| QDarkStyleSheet | MIT ✅ | 仅暗色 QSS、无动画，与我们自建主题重叠。→ 不引入 |

**结论：自建动画控件模块。** 400 行级、零新依赖、零许可证风险，且与现有 `AppTheme` 双主题机制天然衔接。

---

## 4. 交互方案

### 4.1 新模块：`src/ui/`（零业务侵入）

```
AnimatedPushButton.{h,cpp}   # QPushButton 子类：state layer + ripple + 按压下沉
AnimatedComboBox.{h,cpp}     # QComboBox 子类：弹出容器动画 + 箭头旋转
AnimatedCheckBox.{h,cpp}     # P1：勾绘过渡
MotionTokens.h               # 时长/曲线常量 + 减弱动画开关（复用 SPI 检测）
```

### 4.2 按钮状态机（AnimatedPushButton）

```
              hover 进(300ms Std)      press ≥225ms
   normal ─────────────────► hover ──────────────► pressed
     ▲                          │                      │
     │       leave(300ms Std)   │   release<225ms      │ release
     └──────────────────────────┴──────────────────────┘
                                        │
                                   ripple 圈从触点扩散
                                   (400ms (0,0,0.2,1)，半径到覆盖触点对角，
                                    透明度 0.12→0；抬手未完成则接 200ms 淡出)
```

- **hover**：state layer 从 0 插值到 8% 主色（`QVariantAnimation`，300ms，Standard 曲线）；主按钮（#primary，深底）改用 on-primary 白 8%。
- **pressed**：state layer 插值到 10%，同时文字/图标下沉 1px（paint 时 offset，不动 geometry）。
- **ripple**：mousePressEvent 记录触点（devicePixelRatio 归一），启动 400ms 扩散动画；半径终点 = 触点到按钮最远角的距离（保证铺满）；paint 时按当前进度画裁剪圆（半径、透明度双插值）。release 时若动画未完成，不停扩散，接 200ms Standard accelerate 淡出后清除。
- **中断**：动画中反向操作 → stop 当前动画从当前值起始（不跳变）。
- **不做整体缩放**：布局内按钮缩放会带动相邻控件抖动（Qt 无 CSS transform）。
- **减弱动画模式**：`MotionTokens::animationsEnabled()` 读 `SPI_GETCLIENTAREAANIMATION`（复用 `MainWindow.cpp:1574` 已有写法）——关闭时所有动画时长置 0，直接落终态，不画 ripple 圈。
- **焦点环**：paintEvent 自绘 2px accent 50% 外环（`:focus` 态）。

### 4.3 下拉框（AnimatedComboBox）

重写 `showPopup()` / `hidePopup()`，对 popup 容器（`view()->parentWidget()`，一个 QFrame）做动画：

- **展开**：容器 `windowOpacity` 0→1 + 位置从「贴 combo 顶、高 0」展开到全高，**250ms Standard decelerate (0,0,0,1)**；箭头指示器同步旋转 180°（200ms）。
- **收起**：反向 **200ms Standard accelerate (0.3,0,1,1)**；Esc / 失焦 / 选中项均触发。
- **键盘导航**（Up/Down/Enter/Esc）动画期间全程可用，不被拦截。
- **中断**：动画中再次点击 combo → stop 当前动画并从当前几何/透明度反向，不跳变。
- **技术依据**：`setWindowOpacity` 对 popup 容器窗口在 Windows 可用（SO 实证：`combo->view()->parentWidget()->setWindowOpacity(...)`）；Qt::Popup 的窗口 flag 不影响。
- **列表项**：P1 加交错淡入（每项延迟 15ms，总预算 <150ms）。

### 4.4 QSS 修正（AppTheme.cpp 配套改动）

删除 hover/pressed 硬切规则（交自动画处理），保留形状与色彩；补：

```qss
/* 焦点环（键盘可达性） */
QPushButton:focus, QComboBox:focus, QCheckBox:focus { outline: 2px solid <accent 50%>; }
/* 尺寸：按内容自适应，不再抬到 40px 级（用户 2026-10-22 反馈「过高」后回调） */
QPushButton { min-height: 18px; padding: 7px 14px; border-radius: 20px; }
QComboBox, QLineEdit, QSpinBox { min-height: 18px; padding: 5px 10px; border-radius: 8px; }
/* 形状分层：按钮 20px(等效full) / 输入 8px / 卡片 12px / 菜单 8px */
QGroupBox { border-radius: 12px; }
QMenu { border-radius: 8px; }
/* 复选框 18px */
QCheckBox::indicator { width: 18px; height: 18px; }
/* 海拔：分组卡片与弹窗轻阴影 */
QGroupBox { border: 1px solid <border>; }
```

形状/色值继续走现有 `Palette` 表，不引入第二套配色。按钮 20px 是 ~30px 控件高下的
等效 full（M3 按钮默认 full，但 full 用过大值在边框抗锯齿下边缘发虚，取高度 2/3 收敛）；
卡片按 M3 官方 caution 保持 12px（信息密集组件不用 full）。

> **尺寸回调（2026-10-22 晚）**：P0 首版把按钮/输入抬到 `min-height:24 + padding 8/7`
> （实测按钮 42px、下拉 40px 逻辑高），用户判定「过高、没有按内容走」。回落到
> `min-height:18 + padding 7px 14px / 5px 10px`，实测**按钮 29px、下拉 30px**（逻辑像素，
> 119% DPI），与改版前的 30px 量级一致。宽度随之收紧（左右 padding 16→14）。
> 教训：M3 的 40dp 是触摸口径，桌面鼠标场景直接照搬会显得臃肿。
>
> **禁用态形状（同轮修复）**：`AnimatedPushButton` 原来对 `!isEnabled()` 直接转基类绘制，
> 走 QSS ⇒ `QPushButton#primary:disabled` 这类更具体的选择器**不带 `border-radius`**，
> 渲染成直角（「应用选中」在未选中项时尤其明显）。改为**禁用态同样自绘**
> （`baseFill`/`borderColor` 在 primary 禁用时取 `accentSoft`），形状与其它按钮统一；
> 文字仍由 `CE_PushButtonLabel` 画，QSS 的 `color: {disabled}` 继续生效。
> **通用教训：Qt QSS 里更具体的选择器不会补齐未声明的属性时，不要依赖基类绘制兜底。**

### 4.5 成本估算

- hover 动画 = 每 16ms 对一个 ~100×32 区域 repaint，60fps 下 CPU 增量远低于 0.5%（参照：壁纸视频播放全程 CPU 仅 2.89%，UI 线程负载完全不在一个量级）。
- ripple 同量级：圆形裁剪区域（平均 <50% 按钮面积）+ 一次 `QPainter` 抗锯齿圆绘制；与 hover 同为 16ms 级 repaint，合计仍 <1%。
- 动画对象按需创建、`DeleteWhenStopped`，无常驻定时器。
- 壁纸场景下 UI 线程与解码/渲染线程隔离，互不影响。

---

## 5. 实现路径与坑点

### 5.1 改动点清单（P0）

| 文件 | 改动 |
|---|---|
| `src/ui/MotionTokens.h`（新） | 时长/曲线常量 + `animationsEnabled()`（SPI 检测） |
| `src/ui/AnimatedPushButton.{h,cpp}`（新，~280 行） | state layer + ripple + 按压下沉 + 焦点环 |
| `src/ui/AnimatedComboBox.{h,cpp}`（新，~150 行） | 弹出/收起容器动画 + 箭头旋转 |
| `src/CMakeLists.txt` / `FILES`、`PAIRS` | 同步新增源文件（`tools/check_sources.py` 会查） |
| `src/MainWindow.cpp` | 25 处 `new QPushButton` → `new AnimatedPushButton`；17 处 `new QComboBox` → `new AnimatedComboBox`（纯构造点替换，信号槽/业务不动） |
| `src/AppTheme.cpp` | 补焦点环/高度/形状分层（§4.4）。**偏差**：`hover`/`pressed` 两条硬切规则**保留**——`QMessageBox`/`QInputDialog`/`QFileDialog` 内部建的是原生 `QPushButton`，不走自绘控件，删了这些控件就没有按压反馈 |

### 5.2 已知坑点（均有规避手段）

1. **QSS 不能过渡**——所有状态变化必须走 `QVariantAnimation` 插值 + `repaint()`，不能指望样式表。
2. **动画中不得改布局内控件的 geometry**——会触发父布局重排抖动；state layer 走 paint，不走 setGeometry。
3. **`view()->parentWidget()` 才是 popup 容器**（QFrame），不是 view 本身；Qt 版本升级后需回归验证。
4. **高 DPI**：动画量纲一律逻辑像素；ripple 触点记录须按 `devicePixelRatio()` 归一（Qt6 高 DPI 下 event->position() 已是逻辑像素，但 setGeometry 的 popup 容器是物理像素坐标系，混用会偏移——实测校准）。
5. **`DeleteWhenStopped` + 父对象**：防 GC（Python bench 侧踩过的同类坑在 C++ 侧的对应物是裸函数指针/动画对象生命周期）。
6. **主题热切换**（浅/深/跟随系统）：`AppTheme::apply()` 后需通知动画控件刷新缓存色值（新增 `refreshTheme()` 槽或读 `AppTheme::accentColor()` 现取）。
7. **ripple 触点定位**：mousePressEvent 的 `event->position()` 是**控件局部坐标**，直接用于 painter 绘制；不得用 globalPosition（popup/多屏下会错）。
8. **ripple 与 disable 竞态**：按压动画途中控件被 disable（如壁纸播放中批量操作）→ 立即停止全部动画并清 ripple，否则残留圈会画在灰化控件上。
9. **release 后 ripple 未完成**：抬手不打断扩散，接 200ms 淡出（M3 实现行为），不要直接消失（瞬消=塑料感回归）。

---

## 6. 分阶段实施与验收判据

### 6.0 P0 交付状态：已落地并验收（2026-10-22）

代码全部落地（见 §5.1），编译通过，静态闸门 `tools/check_sources.py` 通过。验收不看截图
「像不像」，而是对按钮内部一点做**毫秒级像素采样**，用 M3 的理论混合值当标尺：

| 判据 | 理论值 | 实测（4K 素材旁的主按钮 `应用`，角部像素） | 结论 |
|---|---|---|---|
| 悬停出现中间值（非硬切） | 300ms 内单调爬到终值 | `(37,99,235)` → 44ms `(43,103,236)` → 96ms `(50,108,236)` → 145ms `(52,110,236)` → 245ms `(54,111,237)` 后恒定 | **通过**，插值时长 ≈245–300ms |
| 悬停终值 = M3 8% | 37+218×0.08=54.4、99+156×0.08=111.5 | `(54,111,237)` | **通过**，误差 <1 |
| 按压 = M3 10% | 58.8 / 114.6 | 130ms `(58,114,237)` | **通过** |
| ripple 225ms 起爆并扩散 | 225ms 后出现非均匀亮斑 | 332ms `(61,117,237)`、397ms `(63,118,237)`（角部被扩散中的白圈扫到） | **通过** |
| ripple 淡出回单层 | 400ms 后回按压态 | 532ms `(59,115,237)` = 按压 10% 色 | **通过** |
| 抬手回落 | 回到基色 | 265ms `(37,99,235)` | **通过** |
| 下拉展开非瞬开 | 100ms 半展开、250ms 全展开 | 弹层下沿 110px 处像素：基准 `(245,247,250)` → 81ms `(252,253,254)` → 175ms `(255,255,255)`；弹层最终高 167px | **通过**，约 175–250ms 内长开 |
| Tab 焦点环 | 见 accent 色环 | 注入 Tab 后整窗差异仅 12–27px，无法稳定归因 | **未验证**（见下） |

**本轮修掉一个真缺陷**：首版 `currentLayer()` 用 `(isEnabled() && underMouse()) ? 目标值 : 0`
反推起点，而 Enter 到达时 `underMouse()` 已为 true ⇒ `from == to`，**每个按钮首次悬停都是瞬跳**，
只有第二次以后才渐变。改为自持 `m_layerValue`（`valueChanged`/`finished` 回写）后，
首次悬停即为上表那条平滑曲线。

**验收环境的两个限制（不是产品问题，记下来免得下次重踩）**：

1. 本机 `SetCursorPos` / `SendInput` 移动指针**不产生** `WM_MOUSEMOVE`，表现为「悬停 400ms 零变化」，
   极易误判为缺陷；必须 `SendMessage(WM_MOUSEMOVE)` 持续注入，**且真实光标也要停在同一坐标**
   （否则系统按真实位置回一条 `WM_MOUSELEAVE`，hover 会被自己撤掉，衰减成假曲线）。
2. `PrintWindow(PW_RENDERFULLCONTENT)` 对 Qt 窗口返回旧帧（同样是「零变化」假象），抓帧必须走屏幕 DC `BitBlt`。

验收工具：`_github-cli-install-20260924/verify_motion.py`（消息注入 + BitBlt + 像素统计）。
Tab 焦点环未取证的原因同上：键盘焦点注入在本机受控环境不可靠，代码路径与 hover/ripple 同源自绘，
**按「未验证」记录，不当作已通过**。

### 6.0b 全界面走查与一致性修复（2026-10-22，V4.17）

逐屏截图 + UIA 枚举控件几何（`_github-cli-install-20260924/shot_pages.py`、`walk_uia.py`），
覆盖壁纸库、设置五个子页、多屏页，浅色与深色两套主题。发现并修掉 5 项：

| # | 问题 | 证据 | 修法 |
|---|---|---|---|
| 1 | 按钮比下拉高 4px（34 vs 30 逻辑像素） | UIA：`ButtonControl 40px` / `ComboBoxControl 35px`；同一操作行里两者并排，高度差肉眼可见 | `QPushButton` 纵向 padding 7→5，统一到 30px 级 |
| 2 | 微调框比下拉高（33 vs 30） | UIA：`SpinnerControl 39px` | `QSpinBox` 纵向 padding 单独减 2；实测 34px，与下拉差 1px |
| 3 | 单选按钮是 Windows 原生圆点，与自绘复选框割裂 | 设置 → 外观与效果 → 主题选择 | QSS 画 M3「accent 环 + 中心圆点」，`qradialgradient` 出中心点 |
| 4 | 单选按钮选中态被画成**蓝色直角方框** | 放大截图：1800% 下明确直角 | `:checked` 伪类规则缺 `border-radius`，Qt 用伪类规则整体替换基础规则。与 §4.4「`#primary:disabled` 方角」是同一根因，**伪类规则必须逐条重复声明 `border-radius`** |
| 5 | 设置子导航与主导航同为蓝底选中块，像两个并列的一级入口 | 截图：左侧「设置」与子导航「高级」同时高亮 | 子导航换 `objectName=settingsNav`，透明底、选中只变字色加粗 |

**已知但本轮不改**（记录在案）：

- 设置页内容高约 840px，860px 窗口下需滚动约 75px 才能看全「外观与效果」页底部的主题选择行。
  根治要么加大默认窗口，要么把「外观与效果」拆成两页 —— 属信息架构调整，不在本轮范围。
- 滑块（亮度 / 对比度 / 饱和度 / 模糊 / 暗角）没有数值显示，只能靠拖动手感。
- 库列表为空时，空状态节点的下边距与卡片网格的对齐未做像素校准。

### P0：按钮 + 下拉（核心诉求）

- 内容：§5.1 全部。
- **验收判据**（可机械执行，用 `drive_ui.py`）：
  1. `move` 到按钮 → 0ms / 75ms / 300ms 三连拍，背景色应出现**中间值**（当前为 0 中间值直接跳变）；
  2. 按压 40ms 截图应见 state layer 加深 + 内容下沉 1px + **触点处 ripple 圆圈**；400ms 后圈扩散至铺满按钮并淡出；抬手 <225ms 不应出现 ripple；
  3. 下拉点开后 100ms 截图应见**半透明/未完全展开**容器；250ms 后完全展开；选中后 200ms 内消失（非瞬消）；
  4. Tab 遍历控件应见焦点环；
  5. 系统「关闭动画效果」后全部交互瞬时完成（无半态卡死、无 ripple 残留）。

### P1：其余控件一致性质感

计划项：AnimatedCheckBox 勾绘（120ms）、QSlider handle、导航项、QMenu 项淡入、滚动条 handle、列表项交错淡入。

**本轮已落地（2026-10-22 晚）**

| 项 | 实现 | 验收 |
|---|---|---|
| 复选框勾绘 | `src/ui/AnimatedCheckBox.{h,cpp}`：indicator 全自绘，勾分两段（35% / 65%）描出，底色与边框在 card→accent 间插值，hover 边框 300ms 靠拢 accent，焦点环自绘 | **通过**（数据见下） |
| 滑块 handle | `src/ui/AnimatedSlider.{h,cpp}`：轨道/已走过段/handle 自绘，handle 半径随 hover(+1.4px)/按压(+0.9px) 连续放大，附 accent 光晕 | 已实现并编译验证；未逐帧取证（与按钮同源的 enter/press 事件路径） |
| 菜单淡入 | `src/ui/MenuFade.{h,cpp}`：全局事件过滤器拦截 `QMenu` Show，200ms Standard decelerate 做 windowOpacity 0→1；无构造点改动 | **通过**（数据见下） |

**验收数据**

- 复选框（`indicator` 左上角 6×6 纯色区，窗口相对坐标）：
  取消勾选 `0ms (37,99,235)`（accent）→ `27ms (146,177,245)` → `42ms (176,198,248)`
  → `74ms (196,213,249)` → `112ms (212,224,251)`（card）后恒定；
  勾选为对称 S 曲线 `0ms (212,224,251)` → `60ms (69,122,238)` → `127ms (37,99,235)`。
  全程约 120ms，42–57ms 处为明确中间态，符合「60ms 见勾绘中间态」判据。
- 菜单（「更多」弹出层，采样菜单上部 140×24 区域）：
  `11ms (245.3,245.6,246.3)`（接近背景，几乎透明）→ `96ms (240.4,240.9,241.8)`
  → `111ms (239.4,240.0,241.0)` → `231ms (238.9,239.4,240.5)` 稳定。
  单调渐变、无跳变，前 110ms 完成约 90%（standard decelerate 的预期形状）。
  幅度只有 ~6/255：菜单与背景同为浅色，属正常，不是「无动画」。

**本机验收踩到的第三个坑（补进记录）**：已勾选复选框的 indicator 中间有白色勾，
会把 accent 连通块切成两半 ⇒ 直接做连通域永远匹配不到。必须先把掩码做两轮 3×3 膨胀
闭合勾的缝隙，再取尺寸 20–32px 的近正方块。

**降级/未做（如实记录，不算通过）**

- 滚动条 handle 悬停：QSS 无 transition，要动画就得整条自绘 `QScrollBar`，
  与现有主题/QSS 双轨冲突，且 10px 细条的可感知度低 → 本轮不做。
- 下拉列表项交错淡入：弹层整体已有展开+透明度动画，逐项延迟需接管 delegate，
  收益低于成本 → 归入 P2。
- 导航项（`QListWidget#navRail`）：QSS 已有 hover/选中反馈，自绘委托改造成本高于收益 → 归入 P2。

### P2：场景级动效

**本轮已落地（2026-10-22 晚）**

| 项 | 实现 | 验收 |
|---|---|---|
| 主页面切换淡入 | `src/ui/FadeOverlay.{h,cpp}`：在目标区域盖一层背景色遮罩，alpha 1→0 用 `QVariantAnimation` 驱动（OutCubic，200ms），结束自毁；接入 `m_nav::currentRowChanged`，构造期不演 | **通过**（数据见下） |
| 空状态 ↔ 列表切换 | `m_galleryStack` 切页后调 `fadeInWidget(currentWidget, 160)` | 同一代码路径，未单独取证 |
| 设置子页缓动 | 原有 `animateSettingsPage()`（V4.14，逐 GroupBox 的 `QGraphicsOpacityEffect`） | **通过**（数据见下） |

**为什么整页淡入不能用 QGraphicsOpacityEffect（重要结论）**

给整页（`QStackedWidget` 页面或 `m_pages` 本身）设 `QGraphicsOpacityEffect` 做
opacity 0→1，本机实测**动画期间窗口不逐帧重绘**：屏幕一直停在上一个页面的像素，
动画结束瞬间才跳成新页面。用 800ms 时长复核，现象不变，排除了「动画太快采样错过」。
同一机制用在页内 GroupBox 上是正常的（设置子页有中间态），差别在于整页在
`setCurrentIndex` 时被重新 show，离屏缓存失效。因此整页淡入改为自绘遮罩
`FadeOverlay`——渲染路径与 ripple 一致，逐帧可见。

**验收数据（屏幕 DC BitBlt 抓帧，采样区 240×140）**

- 主页面（壁纸库 → 设置）：与终态的差异像素
  `28ms 14417` → `59ms 1221` → `91ms 275` → `127ms 244` → `165ms 160` → `212ms 0`。
  单调收敛、无跳变，200ms 内完成。
- 设置子页（壁纸 → 播放）：`37ms 33157` → `72ms 4824` → `101ms 3265` → `131ms 2882`
  → `164ms 2320` → `196ms 0`。

**采样方法上的坑（新，务必记住）**

第一版探针在采样循环**之前**抓一张整窗图存盘，整窗 `BitBlt + GetDIBits` 约 150ms，
把第一帧推到动画末尾，于是得出「遮罩完全没效果」的错误结论。采样前不要做任何整窗
抓图；采样区也要小（240×140 时单帧约 30ms），并且**先抓帧、后算 diff**。

**未做（如实记录，不算通过）**

- 操作成功反馈（缩放出对勾）：需要新控件 + 定位逻辑，收益低于成本，未实现。
- 对话框淡入：`QMenu` 已有全局淡入，`QDialog` 会牵动模态焦点，本轮未接。
- 空状态淡入只做到「同一代码路径」，没有单独造空库取证。

**P2 判据回顾**：切换设置子页 300ms 内完成淡入、无布局跳动 —— **满足**
（196ms 收敛，全程用 opacity/遮罩，不改 geometry，无重排）。

---

## 7. 附：本方案依据来源

| 类别 | 来源 |
|---|---|
| QSS 无 transition | exchangetuts.com「How to add a hover transition to QPushButton」（2025-10 实证回答） |
| M3 motion token（时长+贝塞尔精确值） | m3.material.io/styles/motion/easing-and-duration/tokens-specs 官方页（2026-10-22 检索）：Standard 300/250/200ms、Emphasized 500/400/200ms、曲线 cubic-bezier 逐值 |
| M3 形状 scale | m3.material.io/styles/shape/corner-radius-scale 官方页（0/4/8/12/16/20/28/32/48/full）+ buttons specs 页「Shape: Round (default)」 |
| M3 ripple 400ms / 225ms 最小按压 / 曲线 (0,0,0.2,1) | **第三方转引**（docs.rs uzor-animation 标注 Source: Material Design 3 ripple specifications；Compose/Web MDC 实现一致）。官方现行独立 ripple spec 页未检索到——标【转引】，采纳因行业实现一致 |
| popup 容器 setWindowOpacity | StackOverflow「QCombobox transparent item list stylesheet」官方支持回复 |
| 控件库许可证 | qt-material 2.17 BSD-2（PyPI 2026-08）、UN-GCPDS/qt-material、zhiyiYo GPL+商业、laserpants BSD（blog.debao.me 控件库小记 2024-11） |
| 项目现状证据 | `src/AppTheme.cpp`、`src/MainWindow.cpp` 全文 + 开发副本实机截图（2026-10-22） |
