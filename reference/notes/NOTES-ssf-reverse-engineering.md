# SSF 兼容层 — 逆向结论与设计（工作笔记）

> 本文件是**开发过程笔记**，记录已经**证据验证**的结论与仍属**启发式推测**的部分。
> 面向用户的最终文档是 `docs/ssf-skin.md`（在 weasel 源码树内）。
>
> 最后更新：构建环境打通、x64 编译成功后。

---

## 1. 证据来源（按可信度排序）

| 来源 | 用途 | 可信度 |
|---|---|---|
| `fkxxyz/ssfconv` (`ssfconv` Python) | 参数解析、marge 顺序、颜色字节序 | 高（可运行代码） |
| `Flygeon/9IME` (`crates/9ime-core/src/ini.rs`, `skin.rs`, `crates/9ime-server/src/window.rs`) | 九宫格拉伸、marge 语义、编码处理、合成流程 | 高（Weasel 血统的 Rust 重写，已实际运行） |
| `vslavik/winsparkle` (`src/settings.h`, `include/winsparkle.h`) | APPCAST 资源按名查找 | 高（上游源码） |
| Color-P PNG 像素级分析 | 反推几何、验证九宫格边界 | 高（实测数据） |
| 用户在 prompt 中的描述 | 视觉验收标准 | 中（描述性） |

**未找到**任何公开资料解释 `anchor`。见 §5。

---

## 2. 已确认结论（有源码/实测证据）

### 2.1 `layout_horizontal` / `layout_vertical` = 九宫格边界

`9IME` → `crates/9ime-core/src/skin.rs`：

```rust
let lh = ini::get_int_list(ini, section, "layout_horizontal");
let lv = ini::get_int_list(ini, section, "layout_vertical");
if lh.len() >= 3 && lv.len() >= 3 {
    s.stretch_left   = lh[1];
    s.stretch_right  = lh[2];
    s.stretch_top    = lv[1];
    s.stretch_bottom = lv[2];
}
```

- **索引 0 = 模式字段**（本皮肤恒为 `0`；`9IME` 完全忽略它）。
- 索引 1 = 左侧不可拉伸宽度，索引 2 = 右侧不可拉伸宽度。
- 同理 `layout_vertical` → 顶/底。

`9IME` → `crates/9ime-server/src/window.rs::blit_nine()` 是完整实现：
左右边界先 `min(sw/2)` 夹紧，若 `ml+mr > dw` 则按比例压缩；
中间区按比例映射（**拉伸**，非平铺）；角块 1:1 复制。

**对 Color-P 的实际取值：**

| 节 | layout_horizontal | layout_vertical |
|---|---|---|
| H1 | `0,8,70` | `0,32,10` |
| V1 | `0,5,54` | `0,31,9` |
| H2 pinyin | `0,13,27` | `0,8,7` |
| H2 zhongwen | `0,12,27` | `0,8,10` |
| V2 pinyin | `0,12,19` | `0,9,10` |
| V2 zhongwen | `0,12,19` | `0,9,8` |

> ⚠️ **已验证的矛盾点**：H2 的 `pinyin_pic=skin2_1.png` 实测尺寸 **70×28**，
> 但 `stretch_left + stretch_right = 13 + 27 = 40`，远小于 70；
> 且 `skin2_1.png` 在 x=67..69 有 3 列全透明填充。
> `zhongwen_pic=skin2_2.png` 为 **68×28**，`12+27=39`。
> 结论：搜狗编辑器写出的 layout 值**不等于**"把图恰好切成三段"的边界，
> 而是**独立于图片 padding 的设计参数**。九宫格语义正确，
> 但**窗口最小尺寸不能假设等于图片尺寸**（`9IME` 的
> "win_w = max(win_w, bg.w)" 兜底在 H2 下会偏大）。
> 需要在实现中对 H2 分开处理：拼音图与候选图各自独立九宫格拉伸后**上下拼接**。

### 2.2 `marge` 四元组顺序

`9IME` → `skin.rs`：

```rust
s.preedit_left     = pm[2];
s.preedit_top      = pm[0];
s.preedit_right    = pm[3];
s.candidate_left   = zm[2];
s.candidate_right  = zm[3];
s.candidate_bottom = zm[1];
s.gap              = pm[1] + zm[0];
```

即 **`[top, bottom, left, right]`**（不是 left,top,right,bottom！）。

`ssfconv` 的注释也支持同一顺序：
```
#   pinyin_marge[1] + sep + zhongwen_marge[0] = TextMargin.Bottom + TextMargin.Top
#   pinyin_marge[0] = ContentMargin.Top + TextMargin.Top
#   zhongwen_marge[1] = ContentMargin.Bottom + TextMargin.Bottom
```

**对 Color-P H2 的取值**（`pinyin_marge=7,6,8,10`, `zhongwen_marge=10,9,8,8`）：

| 量 | 值 |
|---|---|
| preedit_top | 7 |
| preedit_left | 8 |
| preedit_right | 10 |
| candidate_left | 8 |
| candidate_right | 8 |
| candidate_bottom | 9 |
| gap (拼音区↔候选区间距) | 6 + 10 = 16 |

### 2.3 颜色是 BGR 字节序

`9IME` → `crates/9ime-core/src/ini.rs::get_color()`：

```rust
/// Skin colors are written as 0xRRGGBB but stored in BGR byte order
/// (Sogou convention). Returns 0x00BBGGRR (COLORREF-compatible).
((c & 0xFF) << 16) | (c & 0xFF00) | ((c >> 16) & 0xFF)
```

`ssfconv` 同样对 `separator` 做 `swap_bgr`。

**对本皮肤**：`zhongwen_color=0x6e6cff` → 交换后 `0x00ff6c6e` = **RGB(255,108,110)** = 粉红。
已与 `skin2.png` 实测像素 **完全吻合**（粉色实测 `RGBA(255,108,110,230)`）。
→ **决定性证据**：该皮肤确实是 BGR 存储。
`pinyin_color=0x3c3c3c` 三通道相同，无法自证，但按同规则处理安全。

### 2.4 INI 编码

`9IME` → `skin.rs::decode_ini_text()` 的探测顺序：
1. `FF FE` → UTF-16LE（BOM 后按 u16 解码）
2. 有 `EF BB BF` → 跳过 BOM
3. 严格 UTF-8 解码成功 → UTF-8
4. 否则 → **GBK (CP936)**

本工作区 `skin.ini` 实测为可解析；`font_ch=汉仪细中圆简`、`skin_name=【竹子】Color-P`
必须走对分支，否则字体名乱码。

### 2.5 `pic` / `pinyin_pic` / `zhongwen_pic` 的含义（**本皮肤实测，非公开文档**）

> ⚠️ 这一条与 `9IME` 的命名相反，必须记录清楚。

`9IME` 把 `pinyin_pic` / `zhongwen_pic` 当作 `preedit_highlight` /
`candidate_highlight`。**对 Color-P 这是错的。**

Color-P 的 `Scheme_H2` 同时给出了 `pic`-less 的分离背景：
- `pinyin_pic=skin2_1.png` → 实测 70×28，**纯粉色圆角横条**（无白底）
- `zhongwen_pic=skin1_2.png` → 实测 100×29，**粉边 + 白底圆角矩形**

而 `Scheme_H1` 只有一个 `pic=skin2.png`（84×56），
实测几何为：上方粉色斜切区 + 中段白区 + 下方粉边，
正是"拼音区 + 候选区"**合并在同一张图**里的形态。

→ 结论：搜狗 skin.ini 中
- **H1/V1（单背景模式）**：`pic` = 整个候选窗背景（含拼音区与候选区）
- **H2/V2（分离背景模式）**：`pinyin_pic` = 拼音区背景，`zhongwen_pic` = 候选区背景

这把 `9IME` 的解释**推翻**了（`9IME` 只实现了单背景模式，
它的 `pick_scheme` 优先选 `pic`，所以对 Color-P 会走到 `Scheme_H1`，
即用户看到的"两段式"效果它根本渲染不出来）。

> 注：`Scheme_H2` 的 `zhongwen_pic=skin1_2.png` 与 `[StatusBar] pic=skin1_2.png`
> 是**同一张图**。100×29 的粉边白底圆角矩形同时适合做状态栏底和候选区底，
> 这是皮肤作者的有意复用，不是笔误。

### 2.6 StatusBar 按钮

`9IME` 未实现。按 `skin.ini` 字面语义实现：

- `*_display=1` → 该按钮显示
- `*_pos=x,y` → 按钮相对状态栏背景左上角坐标
- 三态图：`<name>`（normal）、`<name>_hover`、`<name>_down`

实测各按钮图尺寸（均为 25 高）：

| 组 | normal 图 | 尺寸 | hover 图 | down 图 |
|---|---|---|---|---|
| cn_en | `cn3.png` / `en3.png` / `a3.png` | 25×25 | `cn2/en2/a2` | 同 normal |
| biaodian | `cn_biaodian3.png` / `en_biaodian3.png` | 22×25 | `*2` | 同 normal |
| quan_ban | `quan3.png` / `ban3.png` | 23×25 | `*2` | 同 normal |
| fan_jian | `jian3.png` / `fan3.png` | 22×25 | `*2` | 同 normal |
| menu | `menu3.png` | 26×25 | `menu2.png` | 同 normal |

`cn_en` 的第三个值 `a3.png` 对应**"中英混输/其他"**状态。

状态栏背景 `skin1_2.png` = 100×29；`menu_pos=73,3` + 26 宽 = 99，恰好贴合右缘。
→ 证明 `*_pos` 语义正确，且状态栏长度是固定的 100。

### 2.7 APPCAST 资源按名查找（构建相关，非 SSF）

`winsparkle/src/settings.h`:
```cpp
ms_appcastURL = GetCustomResource("FeedURL", "APPCAST");
```
`include/winsparkle.h`: "Windows resource named **"FeedURL"** of type "APPCAST""。
→ `FEEDURL` 等宏的**数值无关紧要**，只需互不相同。
我们无法取得 MFC 私有 `afxres.h`，故在 shim 中赋 `0xE100..0xE103` 并加注释。

---

## 3. 与 9IME 的关键设计差异（必须自己实现的部分）

| 能力 | 9IME | 本实现（必需） |
|---|---|---|
| 单背景 H1/V1 | ✅ | ✅ |
| **分离背景 H2/V2** | ❌ 不支持 | ✅ 拼音图与候选图各自九宫格后拼接 |
| StatusBar 按钮 | ❌ | ✅ 三态 + `*_pos` |
| 中英文分别字体 | ❌ 只用 `font_ch` | ✅ `font_ch` + `font_en` 双字体 |
| 首选/普通候选不同色 | 仅高亮色 | ✅ `zhongwen_first_color` vs `zhongwen_color` |
| 辅助文字色 | ❌ | ✅ `comphint_color` |
| 候选右端 ▷/≡ 控件 | ❌ | ⚠️ 见 §5 |
| 动态窗口宽度 | ✅ | ✅ 必须按真实文本测量 |
| 高 DPI | 整体 `ui_scale` | 见 §6 |

---

## 4. 合成与透明（已验证的 Windows 机制）

`9IME` 的 `present()` 给出的可靠流程（我们复用同一思路，但走 Weasel 的 D2D/GDI 体系）：

1. 解码 PNG → 非预乘 RGBA
2. 转**预乘 BGRA**（`UpdateLayeredWindow` + `AC_SRC_ALPHA` 要求预乘）
3. 九宫格 blit 进离屏缓冲
4. 文字用 GDI 画在 DIB 上 → **GDI 会把它碰过的像素 alpha 清零**
5. → 必须做 **alpha 修复**：在文字矩形内，`if (alpha == 0) alpha = 255`
6. `UpdateLayeredWindow(..., ULW_ALPHA)`

Weasel 现状：`WeaselPanel::_LayerUpdate()` 已经用 `UpdateLayeredWindow` +
`AC_SRC_ALPHA`，但 `DoPaint()` 用的是 `CreateCompatibleBitmap`（**无 alpha 通道**）。
→ SSF 路径必须改用 **32bpp top-down `CreateDIBSection`**，
否则 PNG 透明与圆角会变黑边/白底。

---

## 5. 仍未确定 / 纯启发式（**不得当作事实**）

### 5.1 `anchor`
- `Scheme_H1 anchor=4,7`、`H2 anchor=3,4`、`V1 anchor=3,6`、`V2 anchor=3,4`
- **未找到任何公开实现或文档解释它。**
- 处理方式：解析进结构体 `SsfPoint anchor`，渲染器**默认不使用**，
  在 debug 日志中输出原值。留出独立映射点供后续修正。
- 不做 `switch(anchor)` 硬编码猜测。

### 5.2 候选窗右端的 `▽` / `≡` 控件
- `skin.ini` 中**没有任何 section 描述它们**。
- 已核对本皮肤全部 27 个 PNG：无对应素材（`menu2/menu3.png` 属 StatusBar）。
- `zhongwen_pic=skin2_2.png` 右侧 12 列（x=56..67）**全透明**，
  疑似为这些控件预留的位置/或翻页指示器区域，但**无证据**。
- 结论：**高度可能是搜狗程序内置 UI，不属于 SSF 资源**。
  按用户要求：在兼容层中单独实现视觉近似并**显式记录这一点**，
  不冒充 SSF 语义。优先级最低（P4）。

### 5.3 `use_gdip` / `aero` / `glow` / `LargeFontSupport`
- 语义未验证。解析进结构体并记录，渲染层暂不依赖。

### 5.4 `separator`
- 本皮肤未给出。`9IME` 会画一条分隔线；无此字段时不画。

---

## 6. 高 DPI 策略（待定，默认行为必须明确）

`9IME` 用**整体 `ui_scale`**（insets + 字体 + 内容一起缩放），
好处是保持皮肤纵横比、不裁字。

计划：
- **100% DPI：native bitmap 像素模式**（1:1，满足"像素级还原"验收）
- **>100% DPI：整数/浮点统一缩放**（insets + 字体 + 图片一起缩放），
  避免曾出现过的"文字裁切/错位"
- 两种模式通过配置显式选择，默认自动按 DPI 切换。

---

## 7. 构建环境（已打通，**x64 与 x86 均编译成功**）

详见 `docs/build-weasel.md`（待写）与 `weasel-src/build-weasel.ps1`。要点：

- VS2022 Community **17.14**，MSVC **14.44**，Windows SDK **10.0.26100**
- `BOOST_ROOT = D:\boost182`（官方 1.82 源码），Boost 静态库用
  `D:\boost182\build-boost-libs.ps1` **直接用 cl.exe 编译**，不经 b2。
  b2 **不可用**：其 msvc 工具集把 setup 脚本写死为
  `<MSVC>/bin/Hostx64/vcvarsall.bat`，VS2022 布局下不存在，
  于是 `msvc-setup.nup` 永不生成，所有目标被 skip，产出 0 个 .lib。
  尝试过并失败的绕过：`<setup>` 特性、自定义版本标签（`14.3vs2022`）、
  user-config.jam 里显式给出 cl/link/lib/rc 路径。均已记录，勿重复尝试。
- `rime.lib`：从现有 `rime.dll` 用 `dumpbin /exports` + `lib /def` 生成。
  - x64 ← 安装目录的 `rime.dll`（3,524,096 B，723 导出）
  - x86 ← 官方 0.17.4 安装器内提取的 `rime.dll`（3,034,624 B，723 导出）
- `build-support/afxres.h` 垫片。**三个必须遵守的细节**：
  1. 定义必须放在 `APSTUDIO_READONLY_SYMBOLS` 守卫**之外** —— Weasel 的 .rc
     在第 10 行就 `#define APSTUDIO_READONLY_SYMBOLS`，第 15 行才 include
     afxres.h，放进去等于没定义。
  2. 必须 `#include <winres.h>`（真正的 MFC afxres.h 也这么做）。
  3. 必须补 `LANG_CHINESE` / `SUBLANG_CHINESE_SIMPLIFIED` 等语言常量，
     以及 MFC 私有资源 ID `FEEDURL` 等（后者按名查找，数值无关）。
  4. rc.exe 需要能解析 `LANG_*`，故 rcflags 里必须带 Windows SDK 的
     ucrt/shared/um 与 MSVC include 路径，否则报
     `RC2144 PRIMARY LANGUAGE ID not a number` 并级联出一堆假的
     `RC2135 file not found`。
- `.ini` / `.rc` 编码：**`skin.ini` 实测是 UTF-16LE BOM**；
  Weasel 的 `.rc` 也全是 UTF-16LE BOM（rc.exe 原生支持，无需转换）。

**产物（`weasel-src\dist`，均已用 dumpbin 校验机器类型）：**
```
dist\x64\WeaselServer.exe     2,247,168 B  x64
dist\x64\WeaselDeployer.exe     659,456 B  x64
dist\x64\weaselx64.dll        1,084,928 B  x64
dist\x86\WeaselServer.exe     1,903,104 B  x86
dist\x86\WeaselDeployer.exe     557,056 B  x86
dist\x86\weasel.dll             933,888 B  x86
```
> `WeaselSetup.exe` 两个架构下都是同一个 x86 引导程序（246,272 B），
> 这是上游设定，不是构建错误。

**产物路径冲突（已在脚本中处理，勿简化掉）：**
`WeaselSetup.vcxproj` 的 x64 与 Win32 都输出到 `output\WeaselSetup.exe`；
`WeaselTSF` 的 Win32 输出 `output\weasel.dll`，x64 输出
`output\weaselx64.dll`（不冲突）。因此**两个架构不能从同一份最终状态抓取产物**，
必须各自构建后立即按架构后缀归档 —— `build-weasel.ps1` 即如此。

---

## 8. 已完成的代码骨架

`weasel-src\WeaselUI\ssf\`（已加入 `WeaselUI.vcxproj`，显式关闭预编译头，
保持与 `stdafx.h` 解耦）：

| 文件 | 职责 | 状态 |
|---|---|---|
| `SsfSkin.h` | 结构化数据模型（Scheme/NineSlice/Margin4/StatusBar/Skin） | ✅ 完成 |
| `SsfSkin.cpp` | marge→insets 归一化、缺省值、越界钳制 | ✅ 完成 |
| `SsfIniParser.h/.cpp` | 编码探测(UTF-16LE/BE、UTF-8、GBK)、INI 解析、颜色/点/三元组/四元组解析、组装 | ✅ 完成 |
| `SsfImage.h/.cpp` | 纯 RGBA 缓冲、source-over 合成、九宫格 blit、文字 alpha 修复、预乘 BGRA 导出 | ✅ 完成 |
| `SsfImageLoader.h/.cpp` | GDI+ PNG/BMP 解码 + 大小写不敏感缓存 | ✅ 完成 |
| `SsfLayout.h/.cpp` | 窗口尺寸、两段式/单段式背景矩形、候选框、状态栏、命中测试 | ✅ 完成 |
| `SsfRenderer.h/.cpp` | 背景合成（H1/H2/V1/V2）、高亮、文字 run 列表、状态栏三态图标 | ✅ 完成 |

以上**全部编译通过**（x64 + x86）。尚未接入 `WeaselPanel`，因此**行为仍是原版 Weasel**。

### 待办（下一阶段）
1. `SsfLayoutAdapter`：把 `ssf::Layout` 适配成 `weasel::Layout`，接管
   `WeaselPanel` 的几何与命中测试（鼠标交互区域 = 视觉区域）。
2. `WeaselPanel` 分支：SSF 启用时改用 `CreateDIBSection`(32bpp top-down)
   + `SsfRenderer` + 文字 alpha 修复 + `UpdateLayeredWindow`，
   未启用时走原有路径不变。
3. `UIStyle` 增加 SSF 字段并在 `RimeWithWeasel::_UpdateUIStyle` 读取
   `style/ssf_*`（含 `UIStyle::operator!=` 与 boost serialize）。
4. 离屏预览工具 `ssf_preview.exe`。
5. 单元测试（parser / layout / nine-slice）。
6. `docs/ssf-skin.md` + 部署/回滚说明 + 验收包。
