# Color-P · 小狼毫定制输入法

> 一个**个人定制**的 Windows 中文输入法：以 [小狼毫 Weasel](https://github.com/rime/weasel) 为基底，移植搜狗「竹子 Color-P」皮肤，并用 vibe coding 的方式改造输入行为。
>
> **这不是官方小狼毫，也不是搜狗。** 它是基于前两者的个人作品：引擎用 Rime，外观复刻搜狗皮肤，按键语义则是为我自己重写的。

---

## 目录

- [它是什么](#它是什么)
  - [定制层次：哪些是本项目独有，哪些是高度定制](#定制层次哪些是本项目独有哪些是高度定制)
- [本项目独有（上游没有）](#本项目独有上游没有)
  - [1. 搜狗皮肤在 Rime 里真正跑起来（SSF 兼容层）](#1-搜狗皮肤在-rime-里真正跑起来ssf-兼容层)
  - [2. 小键盘的「原义输入 + 混合分段选词」](#2-小键盘的原义输入--混合分段选词)
  - [3. 无独立状态栏的紧凑外观](#3-无独立状态栏的紧凑外观)
  - [4. 英文输入：白名单 + 分层前缀补全](#4-英文输入白名单--分层前缀补全)
  - [5. 退格三档语义](#5-退格三档语义)
  - [6. 关闭时间戳触发码](#6-关闭时间戳触发码)
  - [7. 候选窗稳定性修补](#7-候选窗稳定性修补)
  - [8. 拼音 + 数字 + 型号：混合造句](#8-拼音--数字--型号混合造句)
- [高度定制（上游已有，本项目大改）](#高度定制上游已有本项目大改)
- [关于 Shift 切换中英文](#关于-shift-切换中英文)
- [目录结构](#目录结构)
- [安装与使用](#安装与使用)
- [日常操作](#日常操作)
- [测试](#测试)
- [从源码恢复与构建](#从源码恢复与构建)
- [已知取舍](#已知取舍)
- [致谢与声明](#致谢与声明)

---

## 它是什么

| | 说明 |
|---|---|
| 引擎 | [librime](https://github.com/rime/librime) 1.13.1，**官方发布版二进制**（含 librime-lua / octagram / predict 插件），未做任何改动 |
| 前端 | [Weasel 小狼毫](https://github.com/rime/weasel) 0.17.4（本项目做了大量定制） |
| 外观 | 搜狗「竹子 Color-P」皮肤，**直接复用原图资源**，在 Weasel 内自绘 |
| 方案 | `rime_ice`（雾凇拼音） |
| 开发方式 | vibe coding —— 我描述需求，AI 读代码、改代码、写测试并验证 |

引擎是**上游编译好的 `rime.dll`**，本项目不改动它（原因见 [关于 Shift 切换中英文](#关于-shift-切换中英文)）。所有定制都在 Weasel 前端、配置与皮肤层。

### 定制层次：哪些是本项目独有，哪些是高度定制

读这份 README 前先分清三件事，避免把上游的东西算到本项目头上：

| 层次 | 内容 | 说明 |
|---|---|---|
| 🟢 **本项目独有** | SSF 搜狗皮肤兼容层、小键盘原义输入与混合分段选词、英文白名单与分层前缀补全、退格三档语义、候选窗稳定性修补、时间戳触发码禁用 | **上游 Weasel 和 rime-ice 都没有**，是本项目自己写/自己设计的 |
| 🟡 **高度定制** | Weasel TSF 前端源码、rime-ice 方案配置（处理器与翻译器顺序、按键绑定）、词库启用范围、构建系统、安装器 | **上游已有**，本项目做了大量改动或重写 |
| ⚪ **上游原样** | librime 1.13.1 引擎（`rime.dll`）、rime-ice 词库数据与 Lua 脚本、搜狗 Color-P 皮肤素材、WinSparkle | **一行未改**，直接使用上游产物 |

下面第 3、4 节分别展开前两层；第 3 节只讲**上游没有的东西**。

---

## 本项目独有（上游没有）

### 1. 搜狗皮肤在 Rime 里真正跑起来（SSF 兼容层）

这不是"照着皮肤调个配色"，而是**写了一个搜狗 SSF 皮肤的解析与渲染层**：

- 直接读取搜狗的 `skin.ini` 与原始 PNG，不经过任何转换
- 支持 9 宫格拉伸、逐像素 alpha 合成、皮肤自带的字体与配色
- 状态栏、中英提示、标号样式全部按皮肤定义绘制
- 关掉这个后端（`style/ssf_enabled: false`）就完整回落到 Weasel 原生配色渲染，不改任何其他行为

原图资源的 27 个文件全部保留在 `assets/Color-P/`。

### 2. 小键盘的「原义输入 + 混合分段选词」

这是本定制最独特的设计，**搜狗和原版小狼毫都没有**。

物理小键盘的数字与运算符**不选词、不翻页**，而是作为字面量进入输入栏，并且**可以和拼音共存、分段选词**：

```
输入 woshi   →  选「我是」
按小键盘 4   →  输入栏变成「我是4」
继续打 yang  →  「我是4yang」，此时 yang 仍是可选的拼音段
```

- 混合态按 `Enter` 提交**原始拼写 + 数字 + 符号**，而不是提交候选
- 没有拼音时单独按小键盘数字**不弹候选框**，直接上屏
- 只有**主键盘顶排**数字才是选词键
- 有候选时主键盘 `-` / `=` 翻页
- **无候选框时标点也直接上屏**，形态完全由中英模式决定（中文 `，。`，英文交给宿主）；
  有候选框时标点作为字面量进入混合输入，与该段拼音一起提交
- **小数点用小键盘的 `KP_Decimal`**，它始终输出 ASCII `.`，因此 `4.5` 不受中文标点影响

适用场景：账号、型号、代码片段、带数字的口语 —— 不用切模式就能连续输入。

### 3. 无独立状态栏的紧凑外观

- 候选窗自身承载全部状态，`ssf_status_bar: false`
- 中英切换提示是**贴光标的皮肤图标**（`cn2.png` / `a2.png`），最长 2 秒后自动消失
- 英文用 Arial、中文与序号用宋体，字号与皮肤原设计一致
- 界面按钮只作状态指示：左键不切换模式，避免图标与实际状态不同步

### 4. 英文输入：白名单 + 分层前缀补全

上游 rime-ice 把英文当作拼音方案的**次翻译器**，打 `hello` / `world` 时英文单词会占满候选。
本项目把它改成三层，实测数据如下：

| 层 | 机制 | 行为 |
|---|---|---|
| 纯英文单词 | `table_translator@melt_eng` | **已移除**。`hello` → 合理了哦、`world` → 我热烈的，不出英文 |
| 中英混排词 | `table_translator@cn_en` | **保留**。词库 1001 条全部含中文（`X光`、`T恤`、`红Buff`、`AD钙奶`），无一条纯英文 |
| 指定英文名词 | `custom_phrase` + `custom_phrase_complete` | 40 个白名单词，支持前缀补全 |

白名单（72 个，按规范大小写写入、用小写编码输入）：

```
AI/模型    ChatGPT  Gemini  DeepSeek  OpenCode  Anthropic  OpenAI  Claude  Copilot
           Qwen  Kimi  Grok
厂商/平台   MicroSoft  Google  Windows  Ubuntu  Linux  Debian  Docker  Vim  VMware
           VPN  NVIDIA  Intel  AMD  GitHub  GitLab  VSCode  PowerShell  WinScp
语言/格式   Python  Java  Rust  JavaScript  HDMI  SSD  SATA  CPU  GPU  RAM  NVMe
软件/游戏   Minecraft  Undertale  CDDA  Clash  MATLAB  PyCharm  Visual Studio
           Multisim  SolidWorks  AutoCAD  STM32CubeMX  STM32CubeIDE  Keil
           Arduino  PlatformIO  Blender  Chrome  Geek  Flash  OBS
           PowerPoint  Excel  Word  XMind  Obsidian  Notion
工具/其他   Listary  PixPin  ZeroTier  Wallpaper  Desktop  Patina
```

**三条编码上的限制**（都是 Rime 输入机制决定的，不是笔误）：

| 情况 | 处理 | 原因 |
|---|---|---|
| 词里带空格（`Visual Studio`、`PlatformIO`） | 编码连写：`visualstudio`、`platformio` | 空格是上屏键，不能出现在编码里 |
| 词里带数字（`STM32CubeMX`） | 编码去掉数字：`stmcubemx`、`stmcubeide` | 主键盘数字是**选词键**，不能作为拼写字符输入 |
| 只想打词尾（`cubemx`） | 打不出来 | 补全只匹配**前缀**，`stm` 可以，`cubemx` 不行 |

**前缀补全为什么拆两层**：`custom_phrase` 的权重是 99，若直接打开它的 `enable_completion`，
补全候选同样带 99，单字母输入会被英文吃光（实测 `g` → Gemini/GitHub/GitLab/Google/GPU，
一个中文都没有；`c` → 「成」被挤到第 5）。所以拆成：

| 翻译器 | 词库 | 权重 | 补全 | 效果 |
|---|---|---|---|---|
| `custom_phrase` | 同一份 | **99** | 关 | 精确码 `cpu` → **CPU** 首位 |
| `custom_phrase_complete` | 同一份 | **0.9** | 开 | 前缀 `chat` → **ChatGPT ~gpt**、`ubun` → Ubuntu、`stm` → STM32CubeIDE/CubeMX |

实测的边界行为：

- 单字母：`c`→成、`w`→我、`g`→个、`a`→啊（中文优先）；只有 `v` 出英文（`v` 无中文单字候选）
- 两字母常用简拼：`wo`/`li`/`de`/`ji`/`da`/`ge`/`sh`/`ch`/`zh` 全部中文优先
- 非法拼音组合才出英文：`gi`→GitHub、`ja`→Java、`obs`→OBS、`stm`→STM32 系列

**必须小写输入**：大写走方案的「大写字母造词」通道（`zuogeDNAjiance` → 做个DNA检测），
所以打 `Bing` 得到的是 `B` 而不是这个词条。

### 5. 退格三档语义

| 按键 | 行为 |
|---|---|
| `BackSpace` | 删一个字符 |
| `Shift+BackSpace` | 删一整个拼音音节 |
| `Ctrl+BackSpace` | **清空整个候选框**，并连同候选窗与 Rime 会话一起清理；没有组合时交还宿主 |

`Ctrl+BackSpace` 这一档是本项目在 TSF 层做的（上游只把 Ctrl+退格绑成删音节）。

### 6. 关闭时间戳触发码

rime-ice 默认把 `ts` 设为 Unix 时间戳的触发码，而 `ts` 同时是 `tangsong` 等词的超级简拼。
打 `ts` 时那串十位数字（`quality=100`）会压过「唐宋 / 同时」。

本项目把 `date_translator/timestamp` 置空关闭。**注意不能只删该项** —— 脚本写的是
`config:get_string(...) or 'ts'`，取不到值会回退成 `ts`；必须显式给空串。
其余日期、时间、星期、ISO 8601、中英日期触发码不受影响。

### 7. 候选窗稳定性修补

| 改动 | 原因 |
|---|---|
| 切走输入法时**不再销毁候选面板**，切回只重建窗口 | 原逻辑每次切走都丢掉已解码的皮肤与字体资源，切回首次按键要同步重建（实测 220 ms，系统换页后达 0.5–2 s） |
| 候选窗**未取得光标锚点前绝不显示** | 新建窗口默认在屏幕原点，会先闪一下左上角再跳到正确位置 |
| **切换回小狼毫必定是中文** | 新建会话即强制中文，避免残留英文状态 |
| **标点形态完全由中英模式决定** | 中文模式一律中文标点（`,`→`，`、`.`→`。`，数字后面也不例外），英文模式交给宿主。**打小数点用小键盘的 `KP_Decimal`**，它输出 ASCII `.`，所以 `4.5` 正常 |
| **光标移开即清理组合** | 光标移开时若宿主只收走组合窗口，会留下「Rime 仍 composing、TSF 已无组合」的中间态：回到输入框时先前的字母还在却选不动，再打字又从零开始。现在隐藏 UI 即终止会话，并在按键前校验会话不变量 |

### 8. 拼音 + 数字 + 型号：混合造句

这是本项目最"反直觉"的一处设计：**输入串里可以同时出现拼音和数字**，由 Rime 自己的
语言模型拼成一句，而不是各段分别上屏。

```
esp32bistm32haoyong                  →  ESP32比STM32好用
youyijianshiqingshishuo32meiyou26da  →  有一件事情是说32没有26大
wojuedezhebukeneng                   →  我觉得这不可能
arduinoqishiyejiunayang              →  Arduino其实也就那样
```

实现要同时做三件事：

| 组成 | 作用 |
|---|---|
| `speller/alphabet` 加入 `0-9` | 数字才能作为**拼写字符**进入 Rime 的输入串 |
| `numeric.dict.yaml`（0–99999 全部登记为词条） | 让**任意数字**都能当作一个音节参与造句 |
| `mixed_tokens.dict.yaml`（72 个白名单词 + ESP32 / STM32） | 让这些词能像音节一样参与造句 |

> **为什么白名单词要同时进词条库**：`custom_phrase` 里的条目只作为独立候选出现，
> **不参与 Rime 的造句**。要让 `arduinoqishiyejiunayang` 出「Arduino其实也就那样」，
> Arduino 必须还是一条词典词条（`mixed_tokens.dict.yaml`）。
> 两个文件都由脚本从 `custom_phrase.txt` 派生，改白名单后重跑即可：
>
> ```powershell
> pwsh -File .\src\build-support\gen-mixed-tokens.ps1
> ```
**关键权衡：数字的归属按"按键来源"区分**

| 你的操作 | 行为 |
|---|---|
| **小键盘**数字 `KP_0`–`KP_9` | 作为拼写字符输入，参与造句 |
| **主键盘**数字 `1`–`9`（`0` 表示第 10 个） | **只用于选词**，绝不进入输入串 |

为什么必须这样区分：数字一旦进 `alphabet`，主键盘数字就会被 `speller` 当成拼写字符吃掉，
1-9 选词随之失效。所以 `keypad_input.lua` 在按键入口就拦下主键盘数字 —— 普通拼音组合下
代为调用 `context:select(index)`（原生 selector 已经收不到数字），虚拟尾巴模式下走原有的
buffer 合并路径。

小键盘运算符（`+ - * /`）与普通标点**仍走虚拟尾巴**，因此「拼音 + 符号」的原有行为不变。

`numeric.dict.yaml` 可用脚本重新生成（改范围或换机器时）：

```powershell
pwsh -File .\src\build-support\gen-numeric-dict.ps1 -Max 99999 -Out $env:APPDATA\Rime\numeric.dict.yaml
```

打开这两个词条文件后，`rime_ice.dict.yaml` 的 `import_tables` 需要把它们放在最前
（权重最高），仓库里的 `rime-config/rime_ice.dict.yaml` 已按此写法提供。

---
## 高度定制（上游已有，本项目大改）

这一层与上一层的区别是：**上游本来就有的东西**，但本项目改了它。

### 1. Weasel TSF 前端

| 改动 | 说明 |
|---|---|
| 全局低层键鼠钩子 | 早期曾用两个钩子做「输入框外裸 Shift 切中英」，实测该路径从未生效（前提要求 TSF 无活动会话，条件过窄），**已连同状态机与死代码全部移除** —— 现在代码里零钩子 |
| 候选窗生命周期 | 上述「切走不销毁面板」「无锚点不显示」「光标移开即清理组合」都落在 `WeaselTSF` / `WeaselUI` |
| 退格 | `Ctrl+BackSpace` 在按键处理里拦截并终止会话 |
| 皮肤渲染接入 | 候选窗渲染改为可切换的 SSF 后端 |

### 2. rime-ice 方案配置

方案的**处理器、翻译器、按键绑定**都被本项目的 `rime_ice.custom.yaml` 完整覆盖：

```yaml
engine/processors:   # 首位插入本项目的小键盘处理器
  - lua_processor@*keypad_input      # ★ 独有
  - ascii_composer / recognizer / key_binder / speller
  - punctuator / selector / navigator / express_editor

engine/translators:  # 去掉 melt_eng，加入 custom_phrase_complete
```

其他覆盖：`key_binder`（`-`/`=` 翻页、`Tab`/`Alt+方向` 移光标、`Ctrl+Shift+3/4` 切换）、
`date_translator/timestamp` 置空、`custom_phrase` 双翻译器分层。

注意：覆盖 `processors` 时**没有**保留上游的 `lua_processor@*select_character`（以词定字），
因此 `[` `]` 归标点使用、以词定字不可用 —— 这是有意取舍。

### 3. 词库启用范围

`rime_ice.dict.yaml` 的 `import_tables` 中，本项目的做法是**启用大字表**：

```yaml
- cn_dicts/8105     # 8,757 条
- cn_dicts/41448    # ★ 本项目启用：46,019 条（上游默认注释掉）
```

实测因果（真实 librime 探针，同一份用户目录只切换该开关）：
`龘 靐 麤 鱻 猋` 在关闭时全部 MISSING，启用后全部 FOUND；`鑫`（8105 内）两种配置都有。

### 4. 构建系统与安装器

| 部分 | 定制内容 |
|---|---|
| 构建 | `weasel.props` 由脚本自动生成（vswhere 探测 VS、按目录名探测 MSVC/SDK），克隆后可直接编译 |
| 工具链 | 全部脚本通过 `build-support/Find-Toolchain.ps1` 探测，不写死本机路径 |
| 安装器 | 自写 Inno Setup：内嵌小狼毫官方安装器完成 TSF 注册，再覆盖本项目产物；含配置改写验证装置 |

---

## 关于 Shift 切换中英文

**这一条完全来自小狼毫的引擎本体，本项目没有改动它。**

Shift 切中英是 **librime 引擎内置**的行为，由 `ascii_composer` 齿轮负责：

```yaml
# default.yaml（方案默认配置）
ascii_composer:
  switch_key:
    Shift_L: commit_code      # 左 Shift 切中英
    Shift_R: commit_code      # 右 Shift 切中英
    Caps_Lock: noop
```

### 为什么只有短按才切换

引擎对该按键设了 **500 毫秒的时长上限** —— 这是 librime 刻意设计的，不是 bug：

```cpp
// librime 源码 ascii_composer.cc
} else if (!(shift_key_pressed_ || ctrl_key_pressed_)) {   // Shift 按下
  shift_key_pressed_ = true;
  // will not toggle unless the toggle key is released shortly
  const auto toggle_duration_limit = std::chrono::milliseconds(500);
  toggle_expired_ = now + toggle_duration_limit;
}
...
// Shift 抬起：只有"还没超时"才切换
if (((is_shift && shift_key_pressed_) || ...) && now < toggle_expired_) {
  ToggleAsciiModeWithKey(ch);
}
```

源码里那句注释就是设计意图：**"除非切换键很快松开，否则不切换"**。它防的是把 Shift 当修饰键用之后误切模式 —— 比如 `Shift`+字母 打大写、`Shift`+方向键 选字，松手时不应该切换中英。

### 实际表现

| 操作 | 是否切换 | 原因 |
|---|---|---|
| 输入框内 **短按** `Shift`（500 ms 内松开） | **切换** | 未超时，引擎执行切换 |
| 输入框内 **长按** `Shift`（超过 500 ms） | **不切换** | 已超时，引擎有意忽略 |
| `Shift` 期间按了别的键（如 `Shift+A` 打大写） | **不切换** | 引擎在遇到其它键时会清掉待命状态 |
| **输入框外**（桌面、文件管理器等） | **不切换** | 没有 TSF 会话，按键不会进入 Rime 引擎 |

### 本项目做了什么

**什么都没做。** 这个 500 毫秒阈值是编译期常量：

- 它**不可通过配置修改** —— `ascii_composer` 的配置项只读 `good_old_caps_lock` 与 `switch_key`，时长写在代码里
- 要改它只能**重新编译 librime** 并替换 `rime.dll`（即替换整个输入引擎）
- 本项目的 `rime.dll` 是**上游官方发布版**，未做改动

早期版本曾尝试用两个全局低层键鼠钩子在"输入框外"也支持裸 Shift 切换，但实测该路径**从未生效**（其前提要求 TSF 当前没有可编辑会话，条件过窄），且常驻全局钩子有额外开销与风险。**该代码已彻底移除**，现在没有任何键鼠钩子。

想要"长按也能切换"的话，唯一正路是自行编译 librime 并调整那个常量；本项目选择保持引擎原样。

---

## 目录结构

本仓库直接位于小狼毫的**安装目录**下，源码就在这一层的 `src/`，不再额外嵌套一层项目文件夹：

```
D:\Rime\                     # 仓库根 = 小狼毫安装目录
├── src/                     # 定制版 Weasel 源码（上游 0.17.4 + 本定制改动）
│   ├── WeaselTSF/           #   TSF 输入服务：按键、焦点、候选列表、语言栏
│   ├── WeaselUI/            #   候选窗渲染
│   │   └── ssf/             #   ★ 搜狗 SSF 皮肤兼容层（本项目新增）
│   ├── WeaselServer/        #   服务进程与托盘
│   ├── RimeWithWeasel/      #   引擎与 UI 的粘合、模式与选项处理
│   ├── rime/lua/            #   ★ keypad_input.lua（小键盘混合输入，本项目新增）
│   ├── include/             #   公共头
│   ├── build-libs/          #   librime 导入库（编译前提，随仓库提供）
│   ├── build-support/       #   构建/测试辅助脚本
│   └── test/TestSsfSkin/    #   回归测试
├── assets/Color-P/          # 搜狗 Color-P 原始皮肤资源（skin.ini + 27 个图片）
├── rime-config/             # 运行时 Rime 配置（★/☆ 标注在下面说明）
│   ├── weasel.custom.yaml   #   ☆ 启用 SSF 后端、配色、布局
│   ├── default.custom.yaml  #   ☆ 方案列表与快捷键
│   ├── rime_ice.custom.yaml #   ★ 处理器/翻译器覆盖、按键绑定、英文分层、时间戳禁用
│   ├── custom_phrase.txt    #   ★ 72 个英文白名单 + 21 条中文短码
│   ├── numeric.dict.yaml    #   ★ 0-99999 数字词条（任意数字参与造句）
│   ├── mixed_tokens.dict.yaml # ★ 含数字型号（ESP32 / STM32）
│   ├── rime_ice.dict.yaml   #   ★ 词库挂载表（含大字表与上述两个词条）
│   └── lua/keypad_input.lua #   ★ 小键盘混合输入处理器
├── docs/                    # 全部文档
├── reference/               # 参考资料（第三方快照，非本项目代码）
├── weasel-0.17.4/           # 已安装的小狼毫本体（第三方二进制，**不入库**）
└── README.md
```

★ = 本项目独有或核心定制　☆ = 高度定制（在上游配置基础上改写）

`weasel-0.17.4/` 是运行时安装目录，被 `.gitignore` 排除在仓库之外；`src/build-libs/` 里的 librime 导入库体积很小（约 0.3 MB/架构），随仓库提供以便直接编译。

`reference/` 下是**第三方项目的快照**，版权归各自作者，仅作查阅用途。真正属于本项目的改动集中在 `src/`（对照上游 Weasel）与 `rime-config/`。

**注意**：`rime_ice.dict.yaml`（词库挂载表，含本项目启用的大字表）与 `en_dicts/`、`cn_dicts/`
等词库数据来自 rime-ice，随安装器载荷分发，**不在本仓库的 `rime-config/` 里**。手动部署时
若想要大字表，需要自己启用 `cn_dicts/41448`。

---

## 安装与使用

### 方式一：直接安装（推荐，不需要编译）

从 [Releases](https://github.com/TangSong777/Color-P/releases) 下载
`Color-P-Setup-0.17.4.exe`（约 27 MB，**内置全部素材，安装时不需要联网**），
**右键以管理员身份运行**，按向导下一步即可。

安装器会一次做完这些事：

1. 静默运行内嵌的小狼毫官方安装器 —— 由它完成文本服务注册、卸载项与评测数据
2. 用小狼毫 0.17.4 的官方安装器建立运行时，再覆盖上本项目编译的输入法本体
3. 把皮肤装到 `C:\ProgramData\ColorPWeasel\Color-P`
4. 把雾凇拼音（rime-ice）数据与配色、按键配置装进你的 Rime 用户目录
5. 装好小键盘混合输入的 Lua 处理器
6. 重新部署一次，让配置与皮肤立即生效

装完**重启一次**，然后在语言列表里选「小狼毫 / Color-P」即可。

**静默安装**（适合脚本化或重装）：

```powershell
Color-P-Setup-0.17.4.exe /SILENT /NORESTART /SUPPRESSMSGBOXES
```

安装向导需要管理员权限（`PrivilegesRequired=admin`），所以静默安装同样要在**已提权的
终端**里运行。

> **建议先卸载旧版小狼毫。** 若系统里已经装了小狼毫且路径与 `{app}` 不同，安装器会弹窗
> 提醒 —— 继续会在语言列表里注册出两个同名输入法。

安装器**不需要任何外部依赖**：Boost 是静态链接的，`rime.dll` / `WinSparkle.dll` 随包提供，
上游安装器也内嵌在包内。

安装后的布局：

| 内容 | 位置 |
|---|---|
| 输入法本体 | `C:\Program Files\Rime\weasel-0.17.4` |
| 皮肤 | `C:\ProgramData\ColorPWeasel\Color-P` |
| Rime 配置与词库 | 你的 Rime 用户目录（默认 `%APPDATA%\Rime`） |

> 皮肤之所以不放在 `Program Files`，是因为 Windows Search 等 AppContainer
> 宿主读不到那里，会导致在文件资源管理器里没有候选窗外观。`ProgramData` 三方都能读。

**已经装过小狼毫的话**：安装器会检测到并提示你先卸载旧版 ——
否则系统里会注册出两个输入法，语言列表出现重复项。

卸载走「设置 → 应用」，或安装目录下的卸载程序；它会先调小狼毫自带卸载器
注销文本服务，再删文件。

### 方式二：手动部署（已有小狼毫，只想换皮肤与配置）

1. 把 `assets/Color-P/` 复制到一个固定目录，例如 `C:\ProgramData\ColorPWeasel\Color-P`
   （放到 `ProgramData` 而非用户目录，是为了让 Windows Search 等 AppContainer 应用也能读到皮肤）
2. 把 `rime-config/` 下的文件复制到小狼毫的用户目录（托盘菜单「用户文件夹」可打开）
3. 把 `rime-config/lua/keypad_input.lua` 放到用户目录的 `lua\` 子目录
4. 确认 `weasel.custom.yaml` 里的 `style/ssf_skin` 指向第 1 步的目录
5. 托盘菜单 →「重新部署」

---

## 日常操作

| 操作 | 效果 |
|---|---|
| 输入框内裸 `Shift` | 切换中英文（**小狼毫引擎本体内置，仅短按生效**，详见 [上文](#关于-shift-切换中英文)） |
| 切回小狼毫 | 必定是中文 |
| 主键盘顶排 `1`–`9` | 选词 |
| 有候选时 `-` / `=` | 前后翻页 |
| `Enter` | 提交原始拼写（混合态提交拼写+数字+符号） |
| `BackSpace` | 删一个字符 |
| **`Shift+BackSpace`** | **删一整个拼音音节** |
| **`Ctrl+BackSpace`** | **清空整个候选框的输入**，连同候选窗与 Rime 会话一起清理；没有组合时交还宿主（普通文本框里仍是“删前一个词”） |
| **小键盘**数字 `KP_0`–`KP_9` | 输入数字，参与拼写与造句（`woshi`+KP4 → 我是4） |
| **主键盘**数字 `1`–`9`（`0`=第 10 个） | **仅选词**，绝不进入输入串 |
| 小键盘运算符 `+ - * /` | 按原义进入虚拟尾巴，可参与混合提交 |
| 英文白名单词（小写） | 如 `chatgpt`、`cpu`、`vscode`；支持前缀 `chat` → ChatGPT |
| 语言栏左键 | 无动作（仅状态指示）；右键打开菜单 |

---

## 测试

全部测试都可独立运行，不需要管理员权限：

```powershell
# SSF 皮肤解析与布局（237 项）
pwsh -NoProfile -File .\src\build-ssf-tests.ps1 -Run

# 小键盘/数字混合输入 Lua 处理器（90 项，需要 Python + lupa）
python .\src\test\TestSsfSkin\test_keypad.py .\rime-config\lua\keypad_input.lua

# 候选窗生命周期与输入法切换回归（11 项断言）
pwsh -NoProfile -File .\src\build-panel-bench.ps1 -Run

# 宿主兼容与 TSF 身份判定
cmd /c .\src\build-support\test-host.cmd && .\src\dist\host-regression.exe

# TSF 输入法身份探针
cmd /c .\src\build-support\build-tsf-probe.cmd

# 安装器逻辑验证（配置改写、边界输入、临时文件提取）
pwsh -File .\src\installer\harness\run-harness.ps1
```

### 字典覆盖度探针（回答「某个字能不能打出来」）

`.table.bin` 是 Marisa 压缩结构，明文搜索不可靠（连「鑫」都搜不到），
所以判断某字是否存在必须走**真实输入链路**：

```powershell
# 构建
cmd /c .\src\build-support\build-dict-probe.cmd

# 跑内置的 6 项断言（龘 靐 麤 鱻 猋 + 对照项 鑫）
.\src\dist\dict-probe.exe <用户目录副本> <安装目录\data>

# 查询任意输入得到的候选（排查抢位/补全用）
.\src\dist\dict-probe.exe <用户目录副本> <安装目录\data> --query chat --limit 5

# 在指定目录重新部署后测试（对照实验用）
.\src\dist\dict-probe.exe <用户目录副本> <安装目录\data> --deploy
```

探针操作的是**用户目录副本**（复制时排除 `*.userdb` 与 `sync`，避免与运行中的服务抢锁）。

---

## 从源码恢复与构建

本仓库是**自包含**的：只要有它，就能从零重建输入法本体 —— 已用"克隆到全新目录后
直接编译"实测验证过，不需要联网、不需要任何手工前置文件。

```powershell
git clone https://github.com/TangSong777/Color-P.git
cd Color-P
pwsh -NoProfile -File .\src\build-weasel.ps1 -Both
```

（约 2.5 分钟，产出 x64 与 x86 两套二进制。）

### 编译

```powershell
# 需要 Boost 1.82+ 与 Visual Studio 2022；librime 头文件与导入库、WinSparkle 导入库
# 都已随仓库提供，克隆下来即可直接编译，不需要联网或任何手工前置文件。
pwsh -NoProfile -File .\src\build-weasel.ps1 -Both     # x64 + Win32
pwsh -NoProfile -File .\src\build-weasel.ps1          # 只构建 x64
```

`weasel.props`（上游要求你从模板复制并手填本机路径的那份）现在由脚本**自动生成**：
VS 安装位置用 `vswhere` 探测，MSVC 工具集与 Windows SDK 版本按目录名探测。
如果你的环境比较特殊，可用这些环境变量覆盖后加 `-RegenerateProps` 重新生成：

| 环境变量 | 含义 | 默认 |
|---|---|---|
| `WEASEL_BOOST_ROOT` | Boost 源码目录 | `D:\boost182` |
| `WEASEL_VS_ROOT` | Visual Studio 安装目录 | `vswhere` 探测 |
| `WEASEL_SDK_ROOT` | Windows SDK 根 | `D:\Windows Kits\10` |
| `WEASEL_MSVC_ROOT` | MSVC 工具集目录 | 目录名探测 |
| `WEASEL_SDK_VER` | Windows SDK 版本 | 目录名探测 |

随仓库提供的编译前提（约 0.6 MB + 三个头文件）：

| 位置 | 内容 | 来源 |
|---|---|---|
| `src\build-libs\` | librime 的 `rime_x64.lib` / `rime_x86.lib` | 取自随小狼毫分发的 `rime.dll` 的导出表 |
| `src\include\rime_api.h` 等 | librime 公开头（BSD-3） | 随仓库提供，用于离线编译 |
| `src\lib\`、`src\lib64\` | `WinSparkle.lib`（x86 / x64 各一份） | 由对应架构的 `WinSparkle.dll` 导出表生成 |

产物在 `src\dist\x64\` 与 `src\dist\x86\`：
`weaselx64.dll`、`weasel.dll`、`WeaselServer.exe`、`WeaselDeployer.exe`、`WeaselSetup.exe`。

### 部署

需要替换：

- 安装目录 `weasel-0.17.4\` 下的 `WeaselServer.exe`、`weaselx64.dll`、`weasel.dll`
- **`C:\Windows\System32\weasel.dll` 与 `C:\Windows\SysWOW64\weasel.dll`**（需要管理员权限；这两个文件被宿主进程映射，**必须重启才能替换**）

`WeaselServer.exe` 运行时被占用，替换前需先停进程（或安排重启替换）。仓库提供了自动化的部署脚本 `src/DEPLOY-SOURCE-ASSETS.ps1`（会自行提权）。

### 自行制作安装器

```powershell
# 需要 Inno Setup 6：winget install JRSoftware.InnoSetup
# 载荷（上游安装器、皮肤素材、词库）各自从本机取，不进仓库，所以要先有：
#   · 本仓库源码已编译出 src\dist
#   · 本机已部署过小狼毫与皮肤
pwsh -File .\src\installer\build-installer.ps1
```

产物在 `src\installer\output\`。脚本会校验打包数据里**不含个人数据**
（`userdb`、`sync/`、`installation.yaml`、`user.yaml` 一律排除）。

---

## 已知取舍

这些是**已知且有意保留**的行为，不是待修 bug：

| 现象 | 原因 |
|---|---|
| 有候选时 `-` / `=` 完全被吞，首页末页也不输出原字符 | 与 librime 语义一致 |
| `Tab` 在拼音组合中被映射为移动光标，切不出焦点 | 用于在音节间移动 |
| 左右方向键在输入串两端会环绕 | librime 1.13.1 限制（`_no_loop` 类动作名需 ≥ 1.16） |
| 中文模式下主键盘 `.` 输出 `。` | 标点形态完全由中英模式决定；打小数点用 `KP_Decimal`（ASCII `.`），所以 `192.168.1.1` 在中文模式变成 `192。168。1。1` |
| 裸 Shift 切中英只在**输入框内**且**短按（500 ms 内）**生效 | 小狼毫引擎本体既有的 `ascii_composer` 行为，本项目未改（详见 [上文](#关于-shift-切换中英文)） |
| **输入框外无法切中英** | 没有 TSF 会话，按键不进 Rime 引擎。早期用全局钩子补过这条路，实测从未生效，已移除 |
| **只有白名单里的 72 个英文词能出英文** | 有意设计：其余英文（`hello`/`world`/`go`/`code`…）不出候选，见 [第 4 节](#4-英文输入白名单--分层前缀补全) |
| 英文词条**必须小写输入** | 大写走「大写字母造词」通道（`zuogeDNAjiance` → 做个DNA检测） |
| 以词定字（`[` `]`）不可用 | `engine/processors` 被本项目完整覆盖且未保留 `select_character`，`[` `]` 归标点 |
| **计算器 `cC` 里 `+` 无效** | `cC2+2` 无结果，但 `cC2*3`、`cC9/3`、`cC1-1` 正常。原因是本项目的 `keypad_input.lua` 把 `+` 当字面量拦截。**既有问题，未修** |

### 候选位抢占清单（实测）

以下输入会抢占首位或前几位，属方案本身的触发码设计：

| 输入 | 产出 | 占用候选位 |
|---|---|---|
| `sj` | 时间（4 种格式） | 1–4 |
| `xq` | 星期（5 种格式） | 1–5 |
| `rq` | 日期（5 种格式） | 1–5 |
| `dt` | ISO 8601（3 种） | 1–3 |
| `rqzh` / `rqen` | 中文 / 英文日期 | 1–3 |
| `nl` | 农历 | 1 |
| `uuid` | UUID | 1 |
| `d` / `m` / `hm` | 的 / 吗 / 噷（`pin_cand_filter` 置顶） | 1 |
| `u` / `i` / `ig` / `uu` / `uuuu` / `sig` / `umu` | 有 / 一 / 一个 / 又有 / 又双叒叕 / 是一个 / 有没有（`custom_phrase` 短码） | 1 |
| `ts` | ~~时间戳~~ | **已关闭** |

`pin_cand_filter` 配置里 `hm → 后面` 那条**实际无效**（首位仍是 `custom_phrase` 的「噷」，
「后面」只在第 2 位），属上游配置与实现的偏差，本项目未改。

`docs/intuition-audit-20261007.md` 记录了历史审计的依据；其中「全角标点下
`. ^ [ $ { \ |` 输出全角 ASCII」与「组合未完成时切窗口丢弃输入」两条**已修复**，
不再适用。

---

## 致谢与声明

- 输入引擎：[librime](https://github.com/rime/librime)（BSD-3-Clause）
- 前端基底：[Weasel 小狼毫](https://github.com/rime/weasel)（GPL-3.0）
- 拼音方案：[rime-ice 雾凇拼音](https://github.com/iDvel/rime-ice)（MIT）
- 皮肤原作：搜狗「竹子 Color-P」，作者**墨竹**；本仓库仅做小狼毫层面的移植

**本项目是我个人的定制作品**，基于上述开源项目与皮肤资源二次开发。它不是任何一个上游项目的官方版本，也未获得搜狗的任何授权或背书。源代码遵循上游 Weasel 的 GPL-3.0；皮肤图片资源版权归原作者，此处仅作个人学习与自用移植。

如果你想要一个**能被官方支持、适合长期使用**的小狼毫，请使用 [上游 Weasel](https://github.com/rime/weasel)。本仓库的价值在于"搜狗皮 + 小键盘混合输入"这套组合，以及沿途踩过的坑。
