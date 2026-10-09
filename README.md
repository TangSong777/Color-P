# Color-P · 小狼毫定制输入法

> 一个**个人定制**的 Windows 中文输入法：以 [小狼毫 Weasel](https://github.com/rime/weasel) 为基底，移植搜狗「竹子 Color-P」皮肤，并用 vibe coding 的方式改造输入行为。
>
> **这不是官方小狼毫，也不是搜狗。** 它是基于前两者的个人作品：引擎用 Rime，外观复刻搜狗皮肤，按键语义则是为我自己重写的。

---

## 它是什么

| | 说明 |
|---|---|
| 引擎 | [librime](https://github.com/rime/librime) 1.13.1（未修改上游） |
| 前端 | [Weasel 小狼毫](https://github.com/rime/weasel) 0.17.4（大量定制） |
| 外观 | 搜狗「竹子 Color-P」皮肤，**直接复用原图资源**，在 Weasel 内自绘 |
| 方案 | `rime_ice`（雾凇拼音） |
| 开发方式 | vibe coding —— 我描述需求，AI 读代码、改代码、写测试并验证 |

---

## 独特之处

### 1. 搜狗皮肤在 Rime 里真正跑起来（SSF 兼容层）

这不是"照着皮肤调个配色"，而是**写了一个搜狗 SSF 皮肤的解析与渲染层**：

- 直接读取搜狗的 `skin.ini` 与原始 PNG，不经过任何转换
- 支持 9 宫格拉伸、逐像素 alpha 合成、皮肤自带的字体与配色
- 状态栏、中英提示、标号样式全部按皮肤定义绘制
- 关掉这个后端（`style/ssf_enabled: false`）就完整回落到 Weasel 原生配色渲染，不改任何其他行为

原图资源的 27 个文件全部保留在 `assets/Color-P/`。

### 2. 物理小键盘的「原义输入 + 混合分段选词」

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

### 4. 为稳定性做的定制

| 改动 | 原因 |
|---|---|
| 切走输入法时**不再销毁候选面板**，切回只重建窗口 | 原逻辑每次切走都丢掉已解码的皮肤与字体资源，切回首次按键要同步重建（实测 220 ms，系统换页后达 0.5–2 s） |
| 候选窗**未取得光标锚点前绝不显示** | 新建窗口默认在屏幕原点，会先闪一下左上角再跳到正确位置 |
| 裸 Shift 只在**小狼毫确实是当前输入法**时生效 | 低层键盘钩子是全局的，否则在美式键盘下按 Shift 也会改小狼毫的中英状态 |
| **切换回小狼毫必定是中文** | 新建会话即强制中文，避免残留英文状态 |
| **标点形态完全由中英模式决定** | 中文模式一律中文标点（`,`→`，`、`.`→`。`，数字后面也不例外），英文模式交给宿主。**打小数点用小键盘的 `KP_Decimal`**，它输出 ASCII `.`，所以 `4.5` 正常 |
| **光标移开即清理组合** | 光标移开时若宿主只收走组合窗口，会留下「Rime 仍 composing、TSF 已无组合」的中间态：回到输入框时先前的字母还在却选不动，再打字又从零开始。现在隐藏 UI 即终止会话，并在按键前校验会话不变量 |

---

## 目录结构

本仓库直接位于小狼毫的**安装目录** `D:\Rime` 下，源码就在这一层的 `src/`，不再额外嵌套一层项目文件夹：

```
D:\Rime\                     # 仓库根 = 小狼毫安装目录
├── src/                     # 定制版 Weasel 源码（上游 0.17.4 + 本定制改动）
│   ├── WeaselTSF/           #   TSF 输入服务：按键、焦点、候选列表、语言栏
│   ├── WeaselUI/            #   候选窗渲染
│   │   └── ssf/             #   ★ 搜狗 SSF 皮肤兼容层（本项目新增）
│   ├── WeaselServer/        #   服务进程、托盘、空闲 Shift 钩子
│   ├── RimeWithWeasel/      #   引擎与 UI 的粘合、模式与选项处理
│   ├── rime/lua/            #   ★ keypad_input.lua（小键盘混合输入，本项目新增）
│   ├── include/             #   公共头（含 WeaselTsfIdentity.h 等）
│   ├── build-libs/          #   librime 导入库（编译前提，随仓库提供）
│   ├── build-support/       #   构建/测试辅助脚本
│   ├── test/TestSsfSkin/    #   回归测试与探针
│   └── rime/lua/            #   ★ keypad_input.lua（小键盘混合输入，本项目新增）
├── assets/Color-P/          # 搜狗 Color-P 原始皮肤资源（skin.ini + 27 个图片）
├── rime-config/             # 运行时 Rime 配置
│   ├── weasel.custom.yaml   #   启用 SSF 后端、配色、布局
│   ├── default.custom.yaml  #   Shift 切中英、快捷键、方案列表
│   ├── rime_ice.custom.yaml #   ★ 处理器顺序与小键盘/翻页键绑定
│   └── lua/keypad_input.lua #   ★ 小键盘混合输入处理器
├── reference/               # 参考资料（第三方快照，非本项目代码）
│   ├── librime-reference/   #   librime 源码，用于确认上游行为
│   ├── ssfconv / 9ime/      #   SSF 转换与另一输入法的参考实现
│   └── notes/               #   SSF 格式逆向笔记与预览图
├── docs/                    # 全部文档（唯一一份，不再在 src/ 下重复）
│   ├── ssf-skin.md                        # SSF 兼容层的设计与逆向记录
│   ├── build-weasel.md                    # 构建说明
│   ├── switch-latency-fix-20261007.md     # 切换输入法延迟修复
│   ├── intuition-audit-20261007.md        # 行为审计（含已知取舍的依据）
│   ├── comparison-with-weasel-and-sogou.md
│   └── refactor-*.md / literal-commit-ui.md / host-compatibility-*.md
├── weasel-0.17.4/           # 已安装的小狼毫本体（第三方二进制，**不入库**）
└── README.md
```

`weasel-0.17.4/` 是运行时安装目录，被 `.gitignore` 排除在仓库之外；`src/build-libs/` 里的 librime 导入库体积很小（约 0.3 MB/架构），随仓库提供以便直接编译。

`reference/` 下是**第三方项目的快照**，版权归各自作者，仅作查阅用途。真正属于本项目的改动集中在 `src/`（对照上游 Weasel）与 `rime-config/`。

---

## 安装与使用

### 方式一：直接安装（推荐，不需要编译）

从 [Releases](https://github.com/TangSong777/Color-P/releases) 下载
`Color-P-Setup-0.17.4.exe`，**右键以管理员身份运行**，按向导下一步即可。

安装器会一次做完这些事：

1. 静默运行内嵌的小狼毫官方安装器 —— 由它完成文本服务注册、卸载项与评测数据
2. 用小狼毫 0.17.4 的官方安装器建立运行时，再覆盖上本项目编译的输入法本体
3. 把皮肤装到 `C:\ProgramData\ColorPWeasel\Color-P`
4. 把雾凇拼音（rime-ice）数据与配色、按键配置装进你的 Rime 用户目录
5. 装好小键盘混合输入的 Lua 处理器
6. 重新部署一次，让配置与皮肤立即生效

装完**重启一次**，然后在语言列表里选「小狼毫 / Color-P」即可。

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

### 从源码编译

```powershell
# 需要 Boost 1.82 与 Visual Studio 2022；librime 与 WinSparkle 的导入库已随仓库提供
pwsh -NoProfile -File .\src\build-weasel.ps1 -Both     # x64 + Win32
pwsh -NoProfile -File .\src\build-weasel.ps1          # 只构建 x64
```

编译前提（都已在仓库里，约 0.6 MB）：

| 位置 | 内容 | 来源 |
|---|---|---|
| `src\build-libs\` | librime 的 `rime_x64.lib` / `rime_x86.lib` | 取自随小狼毫分发的 `rime.dll` |
| `src\lib\`、`src\lib64\` | `WinSparkle.lib`（x86 / x64 各一份） | 由对应架构的 `WinSparkle.dll` 导出表生成 |

`winsparkle.h` 里有 `#pragma comment(lib, "WinSparkle.lib")`，两个架构的库同名，靠
`lib\`（Win32）与 `lib64\`（x64）两个库目录区分。x64 那份取自安装目录的 64 位
`WinSparkle.dll`；x86 那份取自官方安装包解包出的 32 位同名 DLL（官方 NSIS 安装包是 32 位载荷）。

产物在 `src\dist\x64\` 与 `src\dist\x86\`。部署时需要同时替换：

- 安装目录 `D:\Rime\weasel-0.17.4\` 下的 `WeaselServer.exe`、`weaselx64.dll`、`weasel.dll`
- **`C:\Windows\System32\weasel.dll` 与 `C:\Windows\SysWOW64\weasel.dll`**（需要管理员权限；这两个文件被宿主进程映射，必须重启才能替换）

`WeaselServer.exe` 运行时被占用，替换需要先停进程（或排重启替换）。

### 日常操作

| 操作 | 效果 |
|---|---|
| 裸 `Shift` | 切换中英文 |
| 切回小狼毫 | 必定是中文 |
| 主键盘顶排 `1`–`9` | 选词 |
| 有候选时 `-` / `=` | 前后翻页 |
| `Enter` | 提交原始拼写（混合态提交拼写+数字+符号） |
| 小键盘数字/运算符 | 按原义输入，可参与混合分段选词 |
| 语言栏左键 | 无动作（仅状态指示）；右键打开菜单 |

---

## 测试

```powershell
# SSF 皮肤解析与布局（237 项）
pwsh -NoProfile -File .\src\build-ssf-tests.ps1 -Run

# 小键盘混合输入 Lua 处理器（636 项，需要 Python + lupa）
python .\src\test\TestSsfSkin\test_keypad.py .\rime-config\lua\keypad_input.lua

# 候选窗生命周期与输入法切换回归（11 项断言）
pwsh -NoProfile -File .\src\build-panel-bench.ps1 -Run

# 宿主兼容与 TSF 身份判定
cmd /c .\src\build-support\test-host.cmd && .\src\dist\host-regression.exe
```

安装器本身另有一套逻辑验证装置，不需要管理员权限即可跑：

```powershell
pwsh -File .\src\installer\harness\run-harness.ps1
```

它验证 `weasel.custom.yaml` 的按行改写（UTF-8 中文注释必须逐字节保留）、缺字段 /
仅 LF / 无尾换行等边界输入、以及 `ExtractTemporaryFile` 能否取出 `dontcopy` 文件。

`src/test/TestSsfSkin/probes/` 下还有两个直接对真实 librime 的行为探针
（`KeypadCaseProbe` 按场景逐个校验小键盘与标点形态），是排查标点问题的依据。

---

## 已知取舍

这些是**已知且有意保留**的行为，不是待修 bug：

- 有候选时 `-` / `=` **完全被吞**，首页末页也不输出原字符（与 librime 语义一致）
- `Tab` 在拼音组合中被映射为移动光标，切不出焦点
- 左右方向键在输入串两端会环绕（受 librime 1.13.1 限制，`_no_loop` 类动作名需 ≥ 1.16）
- **中文模式下主键盘 `.` 输出 `。` 而不是小数点**：标点形态完全由中英模式决定，
  要打小数点请用小键盘的 `KP_Decimal`（它输出 ASCII `.`）。所以 `192.168.1.1`
  在中文模式下会变成 `192。168。1。1`

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
