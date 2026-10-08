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
| 中文下数字分隔符**不再被改写** | `123.4` 曾被写成 `123。4`、`192.168.1.1` 曾变成 `192。168。1。1` —— Lua 处理器抢在 Rime 的 `digit_separators` 之前提交了标点 |

---

## 目录结构

```
Color-P/
├── src/                     # 定制版 Weasel 源码（上游 0.17.4 + 本定制改动）
│   ├── WeaselTSF/           #   TSF 输入服务：按键、焦点、候选列表、语言栏
│   ├── WeaselUI/            #   候选窗渲染
│   │   └── ssf/             #   ★ 搜狗 SSF 皮肤兼容层（本项目新增）
│   ├── WeaselServer/        #   服务进程、托盘、空闲 Shift 钩子
│   ├── RimeWithWeasel/      #   引擎与 UI 的粘合、模式与选项处理
│   ├── rime/lua/            #   ★ keypad_input.lua（小键盘混合输入，本项目新增）
│   ├── include/             #   公共头（含 WeaselTsfIdentity.h 等）
│   ├── test/TestSsfSkin/    #   回归测试与探针
│   └── docs/                #   设计与修复记录
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
├── docs/                    # 本仓库文档
│   ├── switch-latency-fix-20261007.md
│   ├── intuition-audit-20261007.md
│   └── comparison-with-weasel-and-sogou.md
└── README.md
```

`reference/` 下是**第三方项目的快照**，版权归各自作者，仅作查阅用途。真正属于本项目的改动集中在 `src/`（对照上游 Weasel）与 `rime-config/`。

---

## 安装与使用

### 前置

- Windows 10/11
- 已安装 [小狼毫 Weasel 0.17.4](https://github.com/rime/weasel/releases)
- Visual Studio 2022（自行编译时需要）

### 快速使用（不编译）

1. 把 `assets/Color-P/` 复制到一个固定目录，例如 `C:\ProgramData\ColorPWeasel\Color-P`
2. 把 `rime-config/` 下的文件复制到小狼毫的用户目录（托盘菜单「用户文件夹」可打开）
3. 把 `rime-config/lua/keypad_input.lua` 放到用户目录的 `lua\` 子目录
4. 确认 `weasel.custom.yaml` 里的 `style/ssf_skin` 指向第 1 步的目录
5. 托盘菜单 →「重新部署」

### 从源码编译

```powershell
# 需要 Visual Studio 2022、Boost 1.82，以及 librime 的导入库
pwsh -NoProfile -File .\src\build-weasel.ps1 -Both
```

产物在 `dist\x64\` 与 `dist\x86\`。部署时需要同时替换：

- 安装目录的 `WeaselServer.exe`、`weaselx64.dll`、`weasel.dll`
- **`C:\Windows\System32\weasel.dll` 与 `C:\Windows\SysWOW64\weasel.dll`**（需要管理员权限；被宿主进程映射，必须重启才能替换）

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

# 小键盘混合输入 Lua 处理器（636 项）
python .\src\test\TestSsfSkin\test_keypad.py .\rime-config\lua\keypad_input.lua

# 候选窗生命周期与输入法切换回归
pwsh -NoProfile -File .\src\build-panel-bench.ps1 -Run
```

项目在开发过程中修复的三个主要问题（切换延迟、候选窗闪烁、数字分隔符）都留有对应回归，写在 `docs/` 里。

---

## 已知取舍

这些是**已知且有意保留**的行为，不是待修 bug：

- 有候选时 `-` / `=` **完全被吞**，首页末页也不输出原字符（与 librime 语义一致）
- `Tab` 在拼音组合中被映射为移动光标，切不出焦点
- 左右方向键在输入串两端会环绕（受 librime 1.13.1 限制，`_no_loop` 类动作名需 ≥ 1.16）
- 全角标点下 `.` `^` `[` `$` `{` `\` `|` 会输出全角 ASCII 而非配置字符（Lua 层读不到 map/list 形态的配置项）
- 组合未完成时切窗口会丢弃已输入内容

`docs/intuition-audit-20261007.md` 记录了每一条的依据与保留理由。

---

## 致谢与声明

- 输入引擎：[librime](https://github.com/rime/librime)（BSD-3-Clause）
- 前端基底：[Weasel 小狼毫](https://github.com/rime/weasel)（GPL-3.0）
- 拼音方案：[rime-ice 雾凇拼音](https://github.com/iDvel/rime-ice)（MIT）
- 皮肤原作：搜狗「竹子 Color-P」，作者**墨竹**；本仓库仅做小狼毫层面的移植

**本项目是我个人的定制作品**，基于上述开源项目与皮肤资源二次开发。它不是任何一个上游项目的官方版本，也未获得搜狗的任何授权或背书。源代码遵循上游 Weasel 的 GPL-3.0；皮肤图片资源版权归原作者，此处仅作个人学习与自用移植。

如果你想要一个**能被官方支持、适合长期使用**的小狼毫，请使用 [上游 Weasel](https://github.com/rime/weasel)。本仓库的价值在于"搜狗皮 + 小键盘混合输入"这套组合，以及沿途踩过的坑。
