> **来源说明**：本文是 2026-09-12 SSF 兼容层开发阶段的交接文档（原本写给 Codex）。
> 内容为史料，其中的路径与待办已过时；当前状态见仓库根 README.md。
> 保留它是为了记住第 8 节那些真实踩过的坑。

---
# Weasel 搜狗 SSF 皮肤兼容层 — 交接文档（给 Codex）

> 交接时间：2026-09-12
> 状态：**核心渲染层已完成并跑通，皮肤已能在真实输入法中生效。当前任务是打磨 UI 细节。**
>
> 接手前请务必读第 8 节「必须知道的坑」——那里每一条都是本次踩过的真实故障，
> 重犯会浪费数小时。

---

## 1. 任务目标

让 Rime 小狼毫（Weasel）能 **1:1 使用搜狗输入法 SSF 皮肤**。不是用 YAML 模仿颜色，
而是实现一套「Sogou SSF compatibility layer」，直接读取并渲染皮肤素材。

皮肤：**【竹子】Color-P**（`skin_version=0.9`，作者 墨竹）

最终形态：

```
搜狗 .ssf / 解包后的 skin.ini + PNG
        ↓
   SSF Skin Loader  →  SSF Layout Engine  →  SSF Renderer
        ↓
   Weasel Candidate Window（像素级接近）
```

**硬性约束**：SSF 必须是**可选后端**。未启用时 Weasel 原有渲染路径完全不变。

---

## 2. 环境（已就绪，无需重装）

| 组件 | 版本 / 路径 |
|---|---|
| Visual Studio | Community 2022 17.14，`D:\VisualStudio2022` |
| MSVC | 14.44.35207，toolset `v143` |
| Windows SDK | 10.0.26100.0，`D:\Windows Kits\10` |
| Boost | 1.82 源码 `D:\boost182`（静态库已编译好） |
| librime | 1.13.1（头文件 + 从 rime.dll 生成的导入库） |
| Weasel 安装目录 | `D:\Rime\weasel-0.17.4` |
| Rime 用户目录 | `D:\RimeUser`（注册表 `HKCU\Software\Rime\Weasel\RimeUserDir`） |

### 源码树

| 路径 | 内容 |
|---|---|
| `D:\桌面\Harness工作区\皮肤\weasel-src\` | **主工程**：rime/weasel 0.17.4 @ `d73f629` + SSF 层 |
| `D:\桌面\Harness工作区\皮肤\librime-src\` | librime 1.13.1（tag `1.13.1`），仅用于取头文件 |
| `D:\桌面\Harness工作区\皮肤\build-libs\` | `rime_x64.lib` / `rime_x86.lib` 导入库 |
| `D:\桌面\Harness工作区\皮肤\ssfconv\` | Python 参考实现（参数语义佐证） |
| `D:\桌面\Harness工作区\皮肤\9ime\` | **Rust 版 Weasel fork，含 SSF 支持**（关键参考） |
| `D:\桌面\Harness工作区\皮肤\DEPLOY-Color-P\` | 部署包 |
| `D:\桌面\Harness工作区\2026-09-12_15-06-58.png` | **参考截图（搜狗正常效果，目标）** |

---

## 3. 构建与部署

### 构建

```powershell
cd D:\桌面\Harness工作区\皮肤\weasel-src

pwsh -File build-weasel.ps1 -Both   # x64 + x86
pwsh -File build-weasel.ps1         # 仅 x64
pwsh -File build-ssf-preview.ps1    # 离屏预览工具
pwsh -File build-ssf-tests.ps1 -Run # 单元测试（232 项）
```

产物在 `dist\x64\` 和 `dist\x86\`。

### 部署（快速迭代，不用重装）

```powershell
$install='D:\Rime\weasel-0.17.4'
Get-Process WeaselServer -EA SilentlyContinue | Stop-Process -Force
Start-Sleep 2
Copy-Item .\dist\x64\WeaselServer.exe   "$install\WeaselServer.exe"   -Force
Copy-Item .\dist\x64\weaselx64.dll      "$install\weaselx64.dll"      -Force
Copy-Item .\dist\x64\WeaselDeployer.exe "$install\WeaselDeployer.exe" -Force
Start-Process "$install\WeaselServer.exe" -WorkingDirectory $install
```

> `System32\weasel.dll` 是 TSF 文本服务，**只在重装时需要同步**。日常迭代改
> `WeaselServer.exe` 就够了（候选窗由它绘制）。

---

## 4. 架构与文件清单

```
WeaselUI/ssf/                      ← SSF 兼容层全部代码
  SsfSkin.h/.cpp                   结构化数据模型 + marge→insets 归一化
  SsfIniParser.h/.cpp              编码探测 + INI 解析 + 值解析器
  SsfImage.h/.cpp                  纯 RGBA 缓冲 + 合成 + 九宫格 + alpha 修复
  SsfImageLoader.h/.cpp            GDI+ PNG/BMP 解码 + 缓存
  SsfLayout.h/.cpp                 纯几何：窗口尺寸、条带、候选框、命中测试
  SsfLayoutAdapter.h/.cpp          实现 weasel::Layout 接口（几何与命中测试同源）
  SsfRenderer.h/.cpp               背景合成 + 高亮 + 文字 run 列表
ssf_preview/ssf_preview.cpp        离屏渲染工具
test/TestSsfSkin/TestSsfSkin.cpp   232 项单元测试
build-support/afxres.h             MFC-free 垫片（见第 8 节）
docs/ssf-skin.md                   格式规范 + 逆向证据链（**必读**）
docs/build-weasel.md               构建说明 + 所有陷阱
```

### 被修改的上游文件

| 文件 | 改动 |
|---|---|
| `include/WeaselIPCData.h` | `UIStyle` 新增 `ssf_enabled` / `ssf_skin` / `ssf_status_bar`；同步改 `operator!=` 和 boost `serialize` |
| `RimeWithWeasel/RimeWithWeasel.cpp` | 新增 `_LoadSsfSkinSettings()`；在 `_LoadSchemaSpecificSettings` 里重新应用（见 bug 3） |
| `WeaselServer/WeaselServerApp.cpp` | `Initialize()` 之后补一次 `m_ui.Refresh()`（见 bug 1）；诊断日志 |
| `WeaselUI/WeaselPanel.h/.cpp` | SSF 分支：`_SsfReloadSkin` / `_SsfInitFonts` / `_SsfMeasure` / `_SsfDoPaint` / `_SsfDrawText` |
| `WeaselUI/WeaselUI.vcxproj` | 登记 ssf/ 源文件（**显式关闭预编译头**，保持与 stdafx 解耦） |
| `include/WeaselUtility.h` | `DebugStream` 支持镜像到文件（环境变量 `WEASEL_DEBUG_LOG`） |

---

## 5. 已确认的 skin.ini 语义（有证据）

| 参数 | 语义 | 证据 |
|---|---|---|
| `layout_horizontal=x,a,b` | `[未用mode, 左边框, 右边框]` 九宫格边界 | 9IME `skin.rs` + `blit_nine()` |
| `layout_vertical=x,a,b` | `[未用mode, 上边框, 下边框]` | 同上 |
| `*_marge=t,b,l,r` | **`[top, bottom, left, right]`**（不是 left,top,right,bottom！） | 9IME + ssfconv 注释 |
| 颜色 | **BGR 字节序**：`0x6e6cff` → RGB**(255,108,110)** | **与 skin2.png 实测像素完全吻合 —— 决定性证据** |
| `pinyin_pic` / `zhongwen_pic` | **拼音区/候选区背景图**，不是高亮图 | 实测 skin2_1.png 是纯粉圆角条、skin1_2.png 是粉边白底框 |
| `*_pos=x,y` | 按钮相对状态栏背景的坐标 | `menu_pos=73,3` + 26px = 99，正好贴 100px 背景右缘 |
| `skin.ini` 编码 | **UTF-16LE BOM**（Color-P 实测） | 字节级检查 |

> ⚠️ **推翻了 9IME 对 `pinyin_pic` 的解释** —— 它当高亮图，所以渲染不出两段式效果。
> 这正是之前移植失败的原因。

**仍未确定（当前不用，只记录）**：`anchor` 两个整数无任何公开依据；`layout_*[0]`
的 mode 字段含义未知；`use_gdip`/`aero`/`glow`/`LargeFontSupport` 未验证。
详见 `docs/ssf-skin.md` §3.3。

---

## 6. 已修复的 5 个真实 bug（含根因）

这些是**症状隐蔽、排查耗时**的 bug，改动都有注释说明。**不要回退它们。**

### bug 1 — 面板在配置读取之前构造

`WeaselServerApp::Run()` 顺序是 `m_ui.Create()`（构造候选窗面板）→
`m_handler->Initialize()`（读 weasel.yaml）。面板构造时抓到空的 `ssf_skin`。

**修复**：`Initialize()` 之后调用 `m_ui.Refresh()` 一次。

### bug 2 — `skin.ini` 被解码两次

我先读成 UTF-16，又把字节交给 `ParseSkinIni()`，而它内部**自己会探测编码**。
BOM 已剥掉 → UTF-16LE 字节（ASCII + NUL）没过严格 UTF-8 校验 → **掉进 GBK 分支**
→ 字符全乱 → 皮肤解析出空值（`skin_name=''` `font_ch=''`）却报告成功。

**修复**：`ReadRawFile()` 读原始字节直接交给 `ParseSkinIni()`，只解码一次。

### bug 3 — SSF 设置被 schema 重载清空

`_LoadSchemaSpecificSettings()` 里 `m_ui->style() = m_base_style;` 整体覆盖，然后
用**schema 配置**重读 —— 而 schema 里没有 `style/ssf_*`。

**修复**：`_LoadSsfSkinSettings()` 独立出来，在该重置之后从**全局配置**重新应用。

### bug 4 — 32 位 DLL 污染 x64 安装目录

我从官方安装器解包取文件，但**安装器是 32 位 NSIS**，解出的是 32 位载荷。
`rime.dll`(3,034,624B→应 3,524,096B) 和 `WinSparkle.dll`(1,930,240B→应 2,797,056B)
都是 x86，而 `WeaselServer.exe` 是 x64 且直接导入它们。

症状：`0xC0000142` / `0xC000007B`，**错误码不说是哪个文件**。

**修复**：`INSTALL-ALL.ps1` 第 5 步有**架构门禁**：解析每个 x64 二进制导入表，
任何本地依赖架构不对就中止。自写 PE 解析器（不依赖 dumpbin），已与 dumpbin 逐项比对一致。

### bug 5 — 拼音区与候选区之间有多余缝隙

我按 9IME 把 `gap` 算成 `pinyin_marge.bottom + zhongwen_marge.top = 6+10 = 16`，
但**这两个 margin 已经是各自条带的内部上下留白**（条带高度已包含它们），
再额外加一次就是双倍间距 → 渲染出 20px 完全透明的带。

**修复**：`SsfSkin.cpp` 里 `s.gap = s.has_separator ? 1 : 0;`
两张图本就该紧贴（skin2_1.png 是粉条，skin1_2.png 开头是候选区的粉色上边框）。

---

## 7. 当前 UI 状态与待办

### 当前渲染结果（`ssf_preview.exe` 实测）

```
window         : 758x111
pinyin_text    : (8,6)-(308,28)   baseline=22
candidate_bg   : (0,36)-(758,77)
  cand[0] box=(8,36)-(201,77)    label=(8,46)-(28,68)  text=(30,46)-(201,68)
  cand[1] box=(207,36)-(400,77)
  cand[2] box=(406,36)-(599,77)
  cand[3] box=(605,36)-(684,77)
  cand[4] box=(690,36)-(750,77)
highlight=(8,36)-(201,77)
```

### 参考截图（搜狗正常效果）的像素结构

文件：`D:\桌面\Harness工作区\2026-09-12_15-06-58.png`（905×122，**我读不了图，以下是程序化提取**）

```
y=0        灰色细线   x=5..750
y=2..32    白底 + 深色文字  x=17..517
y=33..60   粉色带  x=317..875（其中 x=318..820 是大片空白粉）
y=43..55   该粉带内左侧有深色文字
y=62..90   白底 + 稀疏深色文字
y=91..92   粉色带  x=64..880
y=94..121  白底 + 密集深色文字  x=50..720
```

### 用户指出的 5 个问题（原话）

| # | 问题 | 状态 |
|---|---|---|
| 1 | 拼音区和候选区上下有缝隙或错位 | **部分修复**，见待办 A |
| 2 | 圆角、边框或背景图拉伸变形 | **待办 B** |
| 3 | 文字被裁切、重叠或超出背景 | **待办 C** |
| 4 | 状态栏没显示 / 位置不对 / 按钮不对 | 用户明确说**不需要状态栏**，已在配置里关闭 |
| 5 | 其它（用户未补充文字说明） | — |

### 待办 A — 残留透明带（最高优先级）

`SsfImage::CropTransparentBorders()` **已实现但尚未接线**。原因分析：

Color-P 的图**上下有全透明行**（`skin2.png` 上 6 行、下 2 行）。
`BlitNineSlice` 的边缘区是**按比例缩放**的，于是这些透明行被放大，
在目标里画出透明带 → 就是"缝隙"。

当前实测：拼音条 y=2..32 粉色，然后 **y=33..37 有 5 行透明**，候选区 y=38 才开始。

**需要做两件事**：

**(1) 接线裁剪**。在 `SsfImageStore::Get()`（`SsfImageLoader.cpp`）加载后调用
`CropTransparentBorders()`，并把返回的 `removed` 偏移记录下来，供渲染器调整九宫格边界：

```cpp
Rect removed = img->CropTransparentBorders();
// 九宫格边界必须同步收缩，否则切分点不再对应原作者设计的位置
ns.left -= removed.left;  ns.right -= removed.right;
ns.top  -= removed.top;   ns.bottom -= removed.bottom;
```

同时 `ssf_preview.exe` 里给 `LayoutOptions::native_*_bg` 填的尺寸要用**裁剪后**的
（当前用原图尺寸 70×28 / 100×29，会撑高条带）。

**(2) 修 `BlitNineSlice` 的边缘映射**（`SsfImage.cpp` 约 156-203 行）。
当前实现：

```cpp
// 边缘区按比例缩放
blit(l, 0, smw, ct, dx + cl, dy, dmw, ct);   // top：沿 x 缩放
```

当**目标边缘比源边缘大**时应改为**钳位**（保持像素原样、不插值放大），
否则透明/半透明像素会被拉伸成可见色带。建议映射函数：

```cpp
// scale = false 时用钳位（居中放置源、超出部分取边界像素）
int map(int d, int dtotal, int stotal, bool allow_scale) {
  if (stotal <= 0 || dtotal <= 0) return 0;
  if (dtotal == stotal) return d;
  if (!allow_scale && dtotal > stotal) {
    int off = (dtotal - stotal) / 2;
    int s = d - off;
    return (s < 0) ? 0 : ((s >= stotal) ? stotal - 1 : s);
  }
  return (int)((long long)d * stotal / dtotal);   // 缩放
}
```

**同时修正角块为严格 1:1**（当前 `blit(0,0,cl,ct, dx,dy, cl,ct)` 逻辑正确，但如果
后面改 `cl/ct` 要注意别让角块跟着缩放）。

改完用 `ssf_preview.exe` 验证：**从 y=0 到 y=window.cy-1 不应出现连续 ≥2 行全透明**
（除非 `separator` 明确要求）。

### 待办 B — 圆角/边框变形

九宫格边界值来自 skin.ini，但**实测不满足 `left+right == width`**：
`skin2_1.png` 宽 70，边界却是 13 和 27。所以不能假设"恰好切三段"。
当前实现会 `min(border, sw/2)` 夹紧并对中间区按比例映射。

需要在待办 A 的裁剪+钳位改完后**重新用参考截图核对**：
- 左右圆角是否保持完整圆弧（不被压扁）
- 中段粉色是否连续无接缝
- `skin1_2.png` 的粉色边框粗细是否与参考一致

### 待办 C — 文字裁切/重叠

当前 `Measurer::LineHeight()` 用拉丁探针 `L"Ag"` 测量（`SsfLayoutAdapter.cpp`），
而 `Ascent()` 来自 `IDWriteTextLayout::GetLineMetrics` 的 `lm.baseline`。
**两者可能不一致** → `cand_text_top + ascent` 算出的 baseline 与
`DrawTextLayoutAt(box.top)` 实际绘制位置可能错位。

建议核对方式：
1. 在 `ssf_preview.exe` 输出里对比 `pinyin_text` / `cand[].text` 的 y 范围与实际
   背景条带 y 范围，确认文字没有超出条带。
2. 检查 `SsfRenderer::Compose()` 里 `text_rects` 的 padding 是否足够
   （当前 ±2/+4）。GDI/DirectWrite 反锯齿会画出测量框外，padding 不足会让
   alpha 修复漏掉 → 文字出现"空洞"。

### 待办 D — 候选间距调优

`cand_gap = 6`（`SsfLayout.cpp` 第 91 行附近）是**推导值**，skin.ini 不提供。
`label_gap = 2` 同理。

对比数据：
- 参考截图里候选文字密集，间距看起来比当前更小
- 当前 `cand[0].box` 宽 193px，但它的 label+text 只占 193px（无额外空白），
  说明间距只来自 `cand_gap=6`

建议：调小 `cand_gap` 到 3-4 试，用预览工具和参考截图对比。

### 待办 E — 候选窗右端控件（可选，优先级低）

参考截图右侧 x=817..875 有 `▽` / `≡` 状控件。**skin.ini 没有任何 section 描述它们，
27 个 PNG 也没有对应素材** → 高度确认是**搜狗程序内置 UI，不属于 SSF 资源**。

当前有占位钩子 `LayoutOptions::trailing_controls_width`（默认 0，即不绘制）。
若要做视觉近似，请**明确标注这不是 SSF 语义**。

---

## 8. 必须知道的坑（每条都是真实故障）

### 编码 / 文本

1. **`skin.ini` 是 UTF-16LE BOM**。必须只解码一次 —— `ParseSkinIni()` 内部会探测
   UTF-16LE/BE、UTF-8、GBK。**不要把已解码的文本再当字节传进去**（bug 2）。
2. **搜狗颜色是 BGR**。`0x6e6cff` 是 RGB(255,108,110)，不是蓝色。
3. **用户 `weasel.custom.yaml` 必须无 BOM 写 UTF-8**。有 BOM 会让 librime
   看不见开头的 `patch:` 键，配置静默失效。用
   `[System.IO.File]::WriteAllBytes($p, (New-Object System.Text.UTF8Encoding($false)).GetBytes($t))`。

### 代码

4. **`min`/`max` 是宏**。`WeaselUI/stdafx.h` 刻意没定义 `WIN32_LEAN_AND_MEAN`，
   所以 `windows.h` 的 `min`/`max` 宏是活的。SSF 源码里用 **`(std::max)(a,b)`**
   括号形式（Weasel 既有惯例）。在 SSF 头文件里定义 `NOMINMAX` **不够**（若该
   头文件在 `windows.h` 之后才被包含）。
5. **`gdiplus.h` 需要先 `#include <objidl.h>`**（它声明了接收 `IStream` 的方法）。
6. **`ID2D1DCRenderTarget::BeginDraw()` 返回 `void`**，不能包 `SUCCEEDED()`。
7. **`Layout` 既是类型名又是成员函数名**（`weasel::Layout::layout`）。所以 SSF 的
   布局结果类型取名 **`SsfLayoutResult`**。
8. **SSF 源文件显式关闭预编译头**（vcxproj 里 `<PrecompiledHeader>NotUsing</...>`），
   保持与 `stdafx.h` 解耦，便于单独编译到预览工具和测试。

### 构建

9. **`.rc` 文件是 UTF-16LE BOM**，rc.exe 原生支持，**不要转换**。
10. **`build-support/afxres.h` 的三个要点**（详见 `docs/build-weasel.md` §3）：
    - 定义必须放在 `#ifndef APSTUDIO_READONLY_SYMBOLS` 守卫**之外** ——
      Weasel 的 .rc 在第 10 行就定义了该宏，第 15 行才 include afxres.h
    - 必须 `#include <winres.h>`（提供 `LANG_CHINESE` 等）
    - 所有值必须是**裸数字字面量**，`(-1)` 会被 rc.exe 拒绝
11. **不要用 b2 编 Boost**。Boost 1.82 的 msvc 工具集把 setup 脚本写死为
    `<MSVC>/bin/Hostx64/vcvarsall.bat`（VS2022 下不存在），导致 `msvc-setup.nup`
    永不生成、所有目标被 skip、产出 **0 个 .lib 且不报错**。
    用 `build-boost-libs.ps1` 直接驱动 cl.exe。
12. **两架构产物路径冲突**：`WeaselSetup.exe` 的 x64 与 Win32 输出到同一路径；
    `rime.lib` 两架构同名（`lib\` vs `lib64\`）。`build-weasel.ps1` 按架构立即归档，
    不要"简化"掉。
13. **链接时对象文件路径要内联**，不要用响应文件 —— 工作区路径含中文，
    ASCII 响应文件会把中文变成 `?`，link.exe 报 `LNK4001` + `mainCRTStartup` 未解析。

### 运行时

14. **`System32\weasel.dll` 是 TSF 文本服务**，被 `explorer.exe` 占用无法覆盖。
    重装后若版本不一致，必须**重启电脑**（`MoveFileEx` + `DELAY_UNTIL_REBOOT` 已排队）。
15. **`WeaselServer` 锁着 `<schema>.userdb`**。任何复制/删除 `D:\RimeUser` 的操作
    都必须先停它。
16. **环境变量需要重新登录**才会被新进程继承。测试时用
    `Start-Process -Environment` 或显式 `$env:XXX=...` 后再启动。

### 配置

17. **`default.custom.yaml` 必须指定 `schema_list`**。为空时 librime 回落到共享
    `data/default.yaml` 的第一个方案 `luna_pinyin`（**繁体**）。用户的方案是
    `rime_ice`（简体）。
18. **`_LoadSchemaSpecificSettings()` 会把 `UIStyle` 整体重置**。任何"全局"的
    style 设置都必须在它之后从全局配置重新应用（bug 3 的教训）。

---

## 9. 诊断工具（**非常有用，先看这个**）

服务端代码里有分级日志。**用环境变量开启，不用重编译**：

```powershell
$env:WEASEL_SSF_LOGGING='1'
$env:WEASEL_SSF_LOG="$env:TEMP\weasel-ssf.log"      # 面板侧日志（UTF-16LE）
$env:WEASEL_DEBUG_LOG="$env:TEMP\weasel-debug.log"  # 配置侧日志 + DebugStream 镜像
Get-Process WeaselServer -EA SilentlyContinue | Stop-Process -Force
Start-Sleep 2
Start-Process 'D:\Rime\weasel-0.17.4\WeaselServer.exe' -WorkingDirectory 'D:\Rime\weasel-0.17.4'
```

健康的启动日志应长这样：

```
[SSF] _UpdateUIStyle enter init=1 style.ssf_skin(before)=''
[SSF] global settings: skin='Color-P' enabled=1 status_bar=0 style_addr=0x...
[SSF] _UpdateUIStyle exit init=1 style.ssf_skin='Color-P' enabled=1
[SSF] m_base_style captured: skin='Color-P' enabled=1
[SSF] Run: after m_handler->Initialize skin='Color-P'
[SSF] Run: after m_ui.Refresh skin='Color-P'
        ↓ 面板侧 ↓
refresh: before _InitFontRes skin='Color-P' addr=0x...
reload: enabled=1 skin='Color-P' loaded=0 same=0
user data dir (registry/AppData) = 'D:\RimeUser'
[SSF] loaded skin '【竹子】Color-P' from D:\RimeUser\Color-P
[SSF] font_size=14 font_ch=汉仪细中圆简 font_en=Arial
createLayout: SSF active
```

日志文件是 **UTF-16LE**，读的时候要指定编码：
`Get-Content $path -Encoding Unicode`

另有 `WeaselUI/WeaselPanel.cpp` 里的 `WEASEL_SSF_TRACE_PAINT` 编译开关，
打开后每次绘制都会打印字段值（排查"谁清空了 style"时用过）。

### 离屏预览工具

```powershell
ssf_preview.exe D:\RimeUser\Color-P --out preview.png
ssf_preview.exe D:\RimeUser\Color-P --inspect          # 打印解析结果+缺图
ssf_preview.exe D:\RimeUser\Color-P --debug            # 打印所有矩形
ssf_preview.exe D:\RimeUser\Color-P --scheme H1|H2|V1|V2 --out x.png
ssf_preview.exe D:\RimeUser\Color-P --preedit "ni" --candidates "你好|你号" --out y.png
```

**重要**：预览工具用 **GDI** 测量文字，实时输入法用 **DirectWrite**，
advance width 可能差 1px。几何调参用预览即可，最终排版要用真实输入法核对。

### 单元测试

```powershell
pwsh -File build-ssf-tests.ps1 -Run     # 232 checks，当前全绿
```

---

## 10. 验收标准

用户会在**真实输入法**里验收（他之前已经能正常出字，皮肤也生效了）：

1. 输入长拼音，候选窗出现
2. 上方**粉红拼音条**（`(255,108,110)`）显示拼音
3. 下方候选区：**粉边框 + 白底**（`(248,248,246)`），圆角完整
4. 拼音区与候选区**紧贴，无透明缝隙**
5. **首选候选与其它候选颜色不同**（首选 `#3C3C3C` 深灰，其余 `#FF6C6E` 粉红）
6. 候选变长时窗口变宽，**左右两端图不被拉伸变形**
7. 文字不裁切、不重叠、不超出背景
8. 鼠标点击候选位置与视觉位置一致（已由 `SsfLayoutAdapter` 保证同源）
9. 关闭 SSF（`style/ssf_enabled: false`）后恢复原版 Weasel

**不要状态栏**（用户明确要求，配置里已 `ssf_status_bar: false`）。

---

## 11. 参考素材与备份

| 路径 | 内容 |
|---|---|
| `D:\RimeUser\Color-P\` | 皮肤（skin.ini + 26 PNG），**实测可用的真实素材** |
| `D:\桌面\Harness工作区\皮肤\Color-P-resources\` | 皮肤原始副本 |
| `D:\桌面\Harness工作区\2026-09-12_15-06-58.png` | **参考截图（搜狗正常效果，目标）** |
| `D:\桌面\Harness工作区\皮肤\CURRENT.png` | 当前渲染结果（用于对比） |
| `D:\RimeUser-backup-20260912-143215\` | 用户最完整配置（167 文件，含 rime_ice 词库） |
| `D:\RimeUser-backup-20260912\` | 更早备份（140 文件） |
| `D:\RimeUser-before-restore-20260912-150031\` | 恢复操作前的状态 |

**参考截图对比方法**：两边都是 PNG，用 PIL 提取行/列主色和文字包围盒，
按同样的坐标体系对比。参考图 905×122，当前渲染 758×111 —— **注意宽度不同**，
可能是参考图包含了更多候选或更长文字，需要先确认可比性。

---

## 12. 建议的第一步

1. 读 `docs/ssf-skin.md`（逆向证据链）和本文第 8 节（坑）
2. 跑 `build-ssf-tests.ps1 -Run` 确认环境正常（应 232 全绿）
3. 跑 `ssf_preview.exe D:\RimeUser\Color-P --debug` 看当前几何
4. **做待办 A**（透明带）—— 这是用户最明显的抱怨，且根因已完全定位
5. 用参考截图逐项核对 B/C/D

---

## 13. 本次会话未完成的事（诚实记录）

- **待办 A 只做了一半**：`SsfImage::CropTransparentBorders()` 已实现（`SsfImage.cpp`
  约 210-245 行）但**尚未在任何地方调用**。边缘映射的钳位修改也还没写。
- **没有做像素级 diff**：我没有能力读图（本次会话模型是纯文本），
  所以所有视觉判断都是程序化提取像素得出的，**缺少人眼确认**。
  请接手者务必亲自看一遍参考截图和 `CURRENT.png`。
- **待办 E（右端控件）** 未实现，已判定不属 SSF 资源。
- **`anchor` 未实现**，且没有任何依据，当前只在日志里记录数值。
- **高 DPI 未实测**：代码里 `scale` 会整体缩放，但没有在 125%/150%/200% 下验证过。
