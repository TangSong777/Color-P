# Color-P · 小狼毫定制输入法

Color-P 是一个个人定制的 Windows 中文输入法发行版：以 [Weasel 小狼毫](https://github.com/rime/weasel) 0.17.4 为前端、[librime](https://github.com/rime/librime) 1.13.1 为引擎、[rime-ice 雾凇拼音](https://github.com/iDvel/rime-ice) 为方案，移植搜狗「竹子 Color-P」皮肤，并针对数字、型号与中英混排重做了输入行为。

> 本项目不是小狼毫或搜狗的官方版本，不受其支持。引擎与词库保持上游原样，改动集中在前端、配置与皮肤层。

| | |
|---|---|
| 当前版本 | `0.1.2` |
| 上游版本 | Weasel `0.17.4` / librime `1.13.1` |
| 安装包 | `Color-P-Setup-0.1.2.exe`（约 27 MB，离线） |
| 许可证 | GPL-3.0（代码）· 皮肤素材版权归原作者 |

**目录**

[定制范围](#定制范围) · [功能](#功能) · [实现要点](#实现要点) · [安装](#安装) · [使用](#使用) · [配置与词条](#配置与词条) · [项目结构](#项目结构) · [构建](#构建) · [测试](#测试) · [扩展](#扩展) · [版本与发布](#版本与发布) · [已知限制](#已知限制) · [致谢与许可](#致谢与许可)

## 定制范围

读懂本项目，先分清哪些是上游的、哪些是本项目的：

| 层次 | 内容 |
|---|---|
| **本项目独有** | 搜狗 SSF 皮肤兼容层、小键盘原义输入与混合输入、英文白名单与分层补全、退格三档、候选窗生命周期修补、时间戳触发码禁用 |
| **高度定制** | Weasel TSF 前端、rime-ice 方案配置（处理器与翻译器顺序、按键绑定）、词库启用范围、构建系统与安装器 |
| **上游原样** | librime 引擎（`rime.dll`）、rime-ice 词库与 Lua 脚本、Color-P 皮肤素材、WinSparkle |

## 功能

### 皮肤原生渲染

不做「照着配色调一调」，而是实现了一套搜狗 SSF 皮肤的解析与绘制层：直接读取 `skin.ini` 与原始 PNG，支持九宫格拉伸、逐像素 alpha 合成、皮肤自带字体与配色。状态栏关闭，中英提示以贴光标的皮肤图标呈现，2 秒后自动消失。

关掉 SSF 后端（`style/ssf_enabled: false`）即完整回落到 Weasel 原生渲染，不影响其他行为。

### 数字与符号

物理小键盘的数字与运算符**不选词、不翻页**，而是作为输入内容进入候选框，并能与拼音共存：

```
woshi        →  选「我是」
小键盘 4     →  候选框变为「我是4」
继续打 yang  →  「我是4yang」，yang 仍是可选的拼音段
```

- 主键盘顶排数字始终是选词键；**只认当前页可见的编号**（每页 5 个 → `1`–`5`，`6`–`9` 与 `0` 不参与）
- `=` 有候选时翻下一页；`-` 已翻页时翻回上一页
- 混合态按 `Enter` 提交原始拼写（字母 + 数字 + 符号）
- 无候选框时小键盘数字与标点直接上屏；有候选框时标点作为字面量参与混合输入
- 小数点使用小键盘 `KP_Decimal`，始终输出 ASCII `.`，因此 `192.168.1.1` 不受中文标点影响

#### `-` 与 `_`：仅限候选框内的 ASCII 例外

中文模式下普通标点一律输出中文形态（`,`→`，`、`.`→`。`），但**在候选框内**这两个键例外：

| 按键 | 状态 | 行为 |
|---|---|---|
| `-` | 组合中、仍在第 1 页 | 输入 `-`，作为字面量参与造句 |
| `-` | 组合中、已翻页 | 翻回上一页 |
| `Shift+-` | **组合中** | 输入单个 `_`（ASCII 下划线），作为字面量参与造句 |
| `Shift+-` | **未组合** | 按中文标点输出 `——`（与普通标点一致） |

```
候选框内：nihao-woshiyanghanlin  →  你好-我是杨寒淋
候选框内：nihao_woshiyanghanlin  →  你好_我是杨寒淋
候选框外：Shift+-                →  ——
```

也就是说，`-` 与 `_` 只有在**组成候选**时才作为字面量、不转中文标点；一旦没有候选框，仍遵循中文标点规则。

### 混合造句

输入串中可以同时出现拼音、数字与型号，由 Rime 的语言模型直接组成一句：

```
esp32bistm32haoyong                  →  ESP32比STM32好用
youyijianshiqingshishuo32meiyou26da  →  有一件事情是说32没有26大
arduinoqishiyejiunayang              →  Arduino其实也就那样
```

实现由三部分组成：

| 组成 | 作用 |
|---|---|
| `speller/alphabet` 含 `0-9` | 数字可作为拼写字符进入输入串 |
| `numeric.dict.yaml`（0–99999） | 任意数字都能作为音节参与造句 |
| `mixed_tokens.dict.yaml` | 白名单词与 ESP32 / STM32 可参与造句 |

数字的来源按按键区分：**小键盘数字参与拼写，主键盘数字只用于选词**。

### 英文词条

上游把英文作为次翻译器，打 `hello` / `world` 会出英文单词。本项目改为三层：

| 层 | 机制 | 行为 |
|---|---|---|
| 纯英文单词 | `table_translator@melt_eng` | 已移除。`hello` → 合理了哦，不出英文 |
| 中英混排词 | `table_translator@cn_en` | 保留。词库 1001 条全部含中文（`X光`、`T恤`、`红Buff`） |
| 指定名词 | `custom_phrase` + `custom_phrase_complete` | 72 个白名单词，精确命中 + 前缀补全 |

白名单为科技厂商、开发工具、语言与常用软件，按规范大小写写入，**用小写编码输入**。完整清单（编码、分类、是否参与造句）见 **[自定义字词总表](docs/CUSTOM-WORDS.md)**。

编码遵循两条约束：不含空格（`visualstudio`、`platformio`）；含数字的词去掉数字作编码（`stmcubemx`），因为主键盘数字是选词键。

前缀补全拆成两个翻译器，是为了避免高权重补全挤占单字母输入：`custom_phrase` 权重 99、关闭补全，负责精确码；`custom_phrase_complete` 权重 0.9、开启补全，负责 `chat` → `ChatGPT`。

### 退格三档

| 按键 | 行为 |
|---|---|
| `BackSpace` | 删除一个字符 |
| `Shift+BackSpace` | 删除一个完整拼音音节 |
| `Ctrl+BackSpace` | 清空整个候选框，并回收候选窗与会话；无组合时交还宿主 |

### 候选窗

| 行为 | 原因 |
|---|---|
| 切走输入法时保留面板，切回只重建窗口 | 避免每次切回都重新解码皮肤与字体（实测 220 ms，换页后可达 0.5–2 s） |
| 未取得光标锚点前不显示 | 避免窗口先在屏幕原点闪一下 |
| 切回小狼毫必定是中文 | 避免残留英文状态 |
| 光标移开即终止会话 | 避免出现「Rime 仍在组合、TSF 已无组合」的中间态 |

## 实现要点

### SSF 兼容层

位于 `src/WeaselUI/ssf/`。解析 `skin.ini` 的布局与配色，按九宫格规则拉伸位图，逐像素合成 alpha，并把候选、编码、提示绘制到面板。皮肤素材原样保留在 `assets/Color-P/`（27 个文件）。

### TSF 前端

`src/WeaselTSF/` 负责按键、焦点、候选列表与语言栏。本项目在此层加入了退格拦截、候选窗生命周期管理与 SSF 渲染接入。**代码中不含任何键鼠钩子** —— 早期为「输入框外切换中英文」实现的全局钩子实测从未生效，已连同状态机一并移除。

### 方案覆盖

`rime_ice.custom.yaml` 完整覆盖处理器与翻译器顺序：

```yaml
engine/processors:
  - lua_processor@*keypad_input      # 本项目：拦主键盘数字、放行小键盘
  - ascii_composer / recognizer / key_binder / speller
  - punctuator / selector / navigator / express_editor

engine/translators:
  # 去掉 melt_eng，加入 custom_phrase_complete
```

其余覆盖包括 `key_binder` 绑定、`date_translator/timestamp` 置空、`custom_phrase` 双翻译器分层。

覆盖处理器列表时未保留上游的 `lua_processor@*select_character`，因此以词定字不可用、`[` `]` 归标点 —— 这是有意取舍。

### 词库范围

`rime_ice.dict.yaml` 的 `import_tables` 中启用大字表：

```yaml
- cn_dicts/8105     # 8,757 条
- cn_dicts/41448    # 本项目启用：46,019 条（上游默认注释）
```

实测对照（同一用户目录只切换该开关）：`龘 靐 麤 鱻 猋` 关闭时全部缺失，启用后全部命中；`鑫` 两种配置都有。

### 时间戳触发码

rime-ice 默认把 `ts` 作为 Unix 时间戳触发码，而它同时是「唐宋」「同时」的简拼。本项目将 `date_translator/timestamp` 置为**空字符串**关闭该触发码 —— 不能只删除该项，脚本取不到值会回退为 `ts`。

### 构建与安装器

- `weasel.props` 由脚本自动生成：`vswhere` 探测 Visual Studio，按目录名探测 MSVC 与 Windows SDK
- 所有脚本经 `build-support/Find-Toolchain.ps1` 探测工具链，不写死本机路径
- 安装器为自写 Inno Setup：内嵌小狼毫官方安装器完成 TSF 注册，再覆盖本项目产物

## 安装

### 安装包

从 [Releases](https://github.com/TangSong777/Color-P/releases/latest) 下载 `Color-P-Setup-0.1.2.exe`，右键以管理员身份运行。

安装器依次完成：

1. 运行内嵌的小狼毫官方安装器，注册文本服务与卸载项
2. 覆盖本项目编译的输入法本体
3. 部署皮肤到 `C:\ProgramData\ColorPWeasel\Color-P`
4. 写入雾凇拼音数据、配色与按键配置到 Rime 用户目录
5. 安装小键盘混合输入的 Lua 处理器
6. 重新部署，使配置与皮肤生效

装完**重启一次**，在语言列表选择「小狼毫 / Color-P」。

静默安装（需在已提权终端中运行）：

```powershell
Color-P-Setup-0.1.2.exe /SILENT /NORESTART /SUPPRESSMSGBOXES
```

安装包不依赖外部组件：Boost 静态链接，`rime.dll` 与 `WinSparkle.dll` 随包提供，上游安装器内嵌。

### 安装布局

| 内容 | 位置 |
|---|---|
| 输入法本体 | `C:\Program Files\Rime\weasel-0.17.4` |
| 皮肤 | `C:\ProgramData\ColorPWeasel\Color-P` |
| Rime 配置与词库 | 用户目录（默认 `%APPDATA%\Rime`） |

皮肤放在 `ProgramData` 而非 `Program Files`，是因为 Windows Search 等 AppContainer 宿主无法读取后者，会导致资源管理器中没有候选窗外观。

若系统已装小狼毫且路径不同，安装器会提示先卸载旧版，否则语言列表会出现两个同名输入法。

卸载通过「设置 → 应用」或安装目录下的卸载程序完成：它会先调用小狼毫自带卸载器注销文本服务，再删除文件。

### 手动部署

已有小狼毫、只需更换皮肤与配置时：

1. 复制 `assets/Color-P/` 到固定目录，例如 `C:\ProgramData\ColorPWeasel\Color-P`
2. 复制 `rime-config/` 下的文件到 Rime 用户目录（托盘菜单「用户文件夹」）
3. 复制 `rime-config/lua/keypad_input.lua` 到用户目录的 `lua\` 子目录
4. 确认 `weasel.custom.yaml` 的 `style/ssf_skin` 指向第 1 步目录
5. 托盘菜单 →「重新部署」

> 词库数据（`cn_dicts/`、`en_dicts/`）与 `rime_ice.dict.yaml` 来自 rime-ice，随安装包分发，不在 `rime-config/` 内。手动部署时需要自行准备；若要大字表，需在 `import_tables` 中启用 `cn_dicts/41448`。

## 使用

| 操作 | 效果 |
|---|---|
| `Shift`（输入框内短按） | 切换中英文（引擎内置行为） |
| 主键盘 `1`–`5`（当前页可见编号） | 选词；`6`–`9`、`0` 不参与选词 |
| `=` 有候选时 / `-` 已翻页时 | 下一页 / 上一页 |
| `-` 仍在第 1 页 | 输入 `-`，参与造句 |
| `Shift+-`（组合中） | 输入单个 `_`（ASCII），参与造句 |
| `Shift+-`（未组合） | 输出中文标点 `——` |
| `Enter` | 提交原始拼写（混合态为字母 + 数字 + 符号） |
| `BackSpace` / `Shift+BackSpace` / `Ctrl+BackSpace` | 删字符 / 删音节 / 清空候选框 |
| 小键盘 `KP_0`–`KP_9` | 输入数字，参与拼写与造句 |
| 小键盘 `+ - * /` | 按原义进入输入串 |
| 小键盘 `KP_Decimal` | 输出 ASCII `.` |
| 英文白名单（小写） | `chatgpt`、`cpu`、`vscode`；支持前缀 `chat` → `ChatGPT` |
| 语言栏左键 / 右键 | 状态指示 / 打开菜单 |

英文词条**必须小写输入**：大写走方案的「大写字母造词」通道（`zuogeDNAjiance` → 做个DNA检测）。

## 配置与词条

| 文件 | 职责 |
|---|---|
| `weasel.custom.yaml` | 启用 SSF 后端、配色、布局、字体 |
| `default.custom.yaml` | 方案列表与全局快捷键 |
| `rime_ice.custom.yaml` | 处理器/翻译器覆盖、按键绑定、英文分层、时间戳禁用、`speller/alphabet` |
| `custom_phrase.txt` | 英文白名单与中文短码（`词条<Tab>编码`） |
| `mixed_tokens.dict.yaml` | 可参与造句的词条（由白名单脚本派生） |
| `numeric.dict.yaml` | 0–99999 数字词条（脚本生成） |
| `rime_ice.dict.yaml` | 词库挂载表，含大字表与上述两个词条 |
| `lua/keypad_input.lua` | 小键盘与数字混合输入处理器 |

数字词条范围可调（会增大词库与部署时间）：

```powershell
pwsh -File .\src\build-support\gen-numeric-dict.ps1 -Max 99999
```

白名单与 `mixed_tokens` 是同一来源的两份数据：后者由 `gen-mixed-tokens.ps1` 从 `custom_phrase.txt` 派生。只改前者，`custom_phrase` 能出词但**不能造句**。

## 项目结构

仓库直接位于小狼毫安装目录下，源码在同级 `src/`：

```
D:\Rime\                     # 仓库根 = 小狼毫安装目录
├── src/                     # 定制版 Weasel 源码（上游 0.17.4 + 本项目改动）
│   ├── WeaselTSF/           #   TSF 输入服务：按键、焦点、候选列表、语言栏
│   ├── WeaselUI/            #   候选窗渲染
│   │   └── ssf/             #   搜狗 SSF 皮肤兼容层
│   ├── WeaselServer/        #   服务进程与托盘
│   ├── RimeWithWeasel/      #   引擎与 UI 的粘合
│   ├── rime/lua/            #   keypad_input.lua
│   ├── include/             #   公共头
│   ├── build-libs/          #   librime 导入库
│   ├── build-support/       #   构建/测试辅助脚本
│   └── test/TestSsfSkin/    #   回归测试
├── assets/Color-P/          # 皮肤原始资源（skin.ini + 27 个图片）
├── rime-config/             # 运行时配置与词条
├── docs/                    # 文档（CUSTOM-WORDS.md 等）
├── reference/               # 第三方快照，仅作查阅
├── weasel-0.17.4/           # 已安装的小狼毫本体（不入库）
├── CHANGELOG.md             # 版本判定与发布记录
├── PENDING-CHANGES.md       # 待发布改动账本
└── README.md
```

`weasel-0.17.4/` 被 `.gitignore` 排除；`src/build-libs/` 的导入库约 0.3 MB/架构，随仓库提供以便直接编译。`reference/` 下为第三方项目快照，版权归各自作者。

## 构建

仓库自包含：克隆后可直接编译，不需要联网或手工前置文件。

```powershell
git clone https://github.com/TangSong777/Color-P.git
cd Color-P
pwsh -NoProfile -File .\src\build-weasel.ps1 -Both
```

约 2.5 分钟，产出 x64 与 x86 两套二进制。需要 Visual Studio 2022 与 Boost 1.82+；librime 头文件、导入库与 WinSparkle 导入库已随仓库提供。

`weasel.props` 由脚本自动生成。环境特殊时可用下列变量覆盖，并加 `-RegenerateProps` 重新生成：

| 变量 | 含义 | 默认 |
|---|---|---|
| `WEASEL_BOOST_ROOT` | Boost 源码目录 | `D:\boost182` |
| `WEASEL_VS_ROOT` | Visual Studio 目录 | `vswhere` 探测 |
| `WEASEL_SDK_ROOT` | Windows SDK 根 | `D:\Windows Kits\10` |
| `WEASEL_MSVC_ROOT` | MSVC 工具集 | 目录名探测 |
| `WEASEL_SDK_VER` | SDK 版本 | 目录名探测 |

产物位于 `src\dist\x64\` 与 `src\dist\x86\`：`weaselx64.dll`、`weasel.dll`、`WeaselServer.exe`、`WeaselDeployer.exe`、`WeaselSetup.exe`。

### 部署

需要替换：

- 安装目录下的 `WeaselServer.exe`、`weaselx64.dll`、`weasel.dll`
- `C:\Windows\System32\weasel.dll` 与 `C:\Windows\SysWOW64\weasel.dll`（需管理员权限；被宿主映射，**必须重启才能替换**）

`WeaselServer.exe` 运行时被占用，替换前需停止进程。仓库提供部署脚本 `src/DEPLOY-SOURCE-ASSETS.ps1`（自动提权）。

### 打包安装器

```powershell
# 需要 Inno Setup 6：winget install JRSoftware.InnoSetup
pwsh -File .\src\installer\build-installer.ps1
```

载荷（上游安装器、皮肤、词库）各自从本机取，不入仓库，因此需要已编译的 `src\dist` 与已部署的小狼毫和皮肤。脚本会校验打包数据不含 `userdb`、`sync/`、`installation.yaml`、`user.yaml`。

## 测试

全部测试可独立运行，无需管理员权限：

```powershell
# SSF 皮肤解析与布局（237 项）
pwsh -NoProfile -File .\src\build-ssf-tests.ps1 -Run

# 小键盘与数字混合输入（94 项，需要 Python + lupa）
python .\src\test\TestSsfSkin\test_keypad.py .\rime-config\lua\keypad_input.lua

# 候选窗生命周期与输入法切换（11 项）
pwsh -NoProfile -File .\src\build-panel-bench.ps1 -Run

# 宿主兼容与 TSF 身份判定
cmd /c .\src\build-support\test-host.cmd && .\src\dist\host-regression.exe

# 安装器逻辑（配置改写、边界输入、临时文件提取）
pwsh -File .\src\installer\harness\run-harness.ps1
```

### 字典覆盖度探针

`.table.bin` 是 Marisa 压缩结构，明文搜索不可靠，判断某字能否打出必须走真实输入链路：

```powershell
# 构建
cmd /c .\src\build-support\build-dict-probe.cmd

# 内置 6 项断言（龘 靐 麤 鱻 猋 + 对照 鑫）
.\src\dist\dict-probe.exe <用户目录副本> <安装目录\data>

# 查询任意输入的候选
.\src\dist\dict-probe.exe <用户目录副本> <安装目录\data> --query chat --limit 5

# 在指定目录重新部署后测试
.\src\dist\dict-probe.exe <用户目录副本> <安装目录\data> --deploy
```

探针操作**用户目录副本**（复制时排除 `*.userdb` 与 `sync`，避免与运行中的服务抢锁）。

## 扩展

扩展点都在配置与数据层，无需改动 C++。

### 词条

| 目标 | 修改 | 之后 |
|---|---|---|
| 英文词 / 中文短码 | `rime-config/custom_phrase.txt`（`词条<Tab>编码`） | 重新部署 |
| 让英文词参与造句 | `mixed_tokens.dict.yaml` 增加一行 | 重新部署 |
| 调整数字范围 | 重跑 `gen-numeric-dict.ps1` | 重新部署 |
| 更新词表文档 | 重跑 `gen-word-table.ps1` | 提交 `docs/CUSTOM-WORDS.md` |

### 词库

编辑 `rime_ice.dict.yaml` 的 `import_tables`，越靠前权重越高：

```yaml
import_tables:
  - mixed_tokens        # 参与造句的英文/型号
  - numeric             # 数字
  - cn_dicts/8105       # 字表
  - cn_dicts/41448      # 大字表
  - cn_dicts/base       # 基础词库
```

### 处理器与翻译器

1. Lua 脚本放入 `rime-config/lua/`
2. 在 `rime_ice.custom.yaml` 挂载：`lua_processor@*my_processor` / `lua_translator@*my_translator`
3. 处理器顺序即优先级，要拦截按键必须排在 `speller` / `selector` 之前

`keypad_input.lua` 是最完整的示例。

### 皮肤

| 内容 | 位置 |
|---|---|
| 原始素材 | `assets/Color-P/` |
| 启用与参数 | `weasel.custom.yaml` 的 `style/ssf_skin`、`ssf_enabled` |
| 已部署副本 | `C:\ProgramData\ColorPWeasel\Color-P` |

改素材后重新部署即可，无需重编译。

### 可复现约定

1. 脚本不写死机器路径，位置一律探测或由参数传入
2. 版本号两处同步：`Color-P.iss` 的 `MyAppVersion` 与 `build-installer.ps1` 的 `$PackageVersion`
3. 发布前重跑生成脚本，保证文档与数据一致

## 版本与发布

版本号与上游小狼毫版本解耦：上游 `0.17.4` 只决定安装目录与内嵌的官方安装器。

**版本号只在维护者明确指令后更新。** 日常改动只改本地并部署验证，不提交、不推送、不升版本、不打标签、不重建发布包；改动先累积，等指令再统一定稿。

### 版本规则

采用 `MAJOR.MINOR.PATCH`，按实际变更范围判定：

| 位 | 条件 | 示例 |
|---|---|---|
| `MAJOR` | 不兼容的大改 | 尚未发生，留给 `1.0.0` |
| `MINOR` | 新增可感知的能力 | `0.0.6 → 0.1.0` |
| `PATCH` | 修 bug、调参数、换词库 | `0.0.1 → 0.0.6` |

混合变更取最高位。完整记录见 [CHANGELOG.md](CHANGELOG.md)。

### 版本历史

| 版本 | 说明 |
|---|---|
| **0.1.2** | 当前稳定版：候选框内 `-`/`_` 字面量输入、数字选词只认当前页 |
| 0.1.1 | 文档与发布流程更新（输入功能同 0.1.0） |
| 0.1.0 | 混合造句、英文白名单 72 词、大字表 41448、时间戳禁用、退格三档 |
| 0.0.6 | 移除全部键鼠钩子，仓库自包含 |
| 0.0.1 – 0.0.5 | 历史版本 |

### 发布流程

```powershell
# 1. 读账本汇总，写入 CHANGELOG.md
# 2. 改版本号（两处，必须一致）
#    src\installer\Color-P.iss        -> MyAppVersion / MyAppVersionQuad
#    src\installer\build-installer.ps1 -> $PackageVersion
# 3. 打包
pwsh -File .\src\installer\build-installer.ps1
# 4. 打标签并发布
git tag <版本> && git push origin <版本>
gh release create <版本> .\src\installer\output\Color-P-Setup-<版本>.exe `
    .\src\installer\output\SHA256SUMS.txt `
    --title "Color-P <版本>" --notes-file RELEASE-NOTES.md --latest
```

安装包文件名固定为 `Color-P-Setup-<版本>.exe`，附带 `SHA256SUMS.txt`。

## 已知限制

以下为有意保留的行为，不是待修缺陷：

| 现象 | 原因 |
|---|---|
| 输入框外无法切换中英文 | 无 TSF 会话；本项目不含键鼠钩子 |
| 裸 `Shift` 仅在输入框内短按生效 | librime `ascii_composer` 的既有行为，本项目未改动引擎 |
| `-` 与 `_` 是**候选框内**仅有的两个 ASCII 例外 | 有意设计：组成候选时作为字面量、不转中文标点；没有候选框时 `Shift+-` 仍输出 `——` |
| 有候选时 `-` / `=` 被完全吞掉，首页末页也不输出字符 | 与 librime 语义一致 |
| `Tab` 在拼音组合中用于移动光标 | 有意映射 |
| 方向键在输入串两端环绕 | librime 1.13.1 限制（`_no_loop` 需 ≥ 1.16） |
| 中文模式下主键盘 `.` 输出 `。` | 标点形态由中英模式决定；小数点请用小键盘 `KP_Decimal` |
| 仅白名单内的英文词能出英文 | 有意设计，见「英文词条」 |
| 英文必须小写输入 | 大写走「大写字母造词」通道 |
| 以词定字（`[` `]`）不可用 | 未保留 `select_character`，`[` `]` 归标点 |
| 计算器中 `+` 无效（`cC2+2`） | `keypad_input.lua` 将 `+` 作为字面量拦截；既有问题 |

### 候选位占用

部分输入会占据候选首位，属方案触发码设计：

| 输入 | 产出 | 占用 |
|---|---|---|
| `sj` / `xq` / `rq` | 时间 / 星期 / 日期 | 1–5 |
| `dt` / `rqzh` / `rqen` | ISO 8601 / 中文日期 / 英文日期 | 1–3 |
| `nl` / `uuid` | 农历 / UUID | 1 |
| `d` / `m` / `hm` | 的 / 吗 / 噷（`pin_cand_filter` 置顶） | 1 |
| `u` / `i` / `ig` / `uu` / `uuuu` / `sig` / `umu` | 短码词条 | 1 |
| `ts` | 时间戳 | 已关闭 |

`pin_cand_filter` 中 `hm → 后面` 一条实际无效（首位仍是「噷」），属上游配置与实现的偏差。

## 致谢与许可

- 输入引擎：[librime](https://github.com/rime/librime)（BSD-3-Clause）
- 前端基底：[Weasel 小狼毫](https://github.com/rime/weasel)（GPL-3.0）
- 拼音方案：[rime-ice 雾凇拼音](https://github.com/iDvel/rime-ice)（MIT）
- 皮肤原作：搜狗「竹子 Color-P」，作者**墨竹**；本仓库仅做小狼毫层面的移植

本项目为个人定制作品，基于上述开源项目与皮肤资源二次开发，不是任何上游项目的官方版本，也未获得搜狗授权或背书。源代码遵循上游 Weasel 的 GPL-3.0；皮肤图片资源版权归原作者，仅作个人学习与自用移植。

若需要官方支持、适合长期使用的小狼毫，请使用 [上游 Weasel](https://github.com/rime/weasel)。
