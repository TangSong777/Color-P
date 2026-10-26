# 待发布改动（自 0.1.4 起）

> **用途**：每次改动都在本文件**末尾追加**一条。更新版本号时，完整读取本文件，
> 按「新增 / 修复 / 变更 / 文档」汇总进 `CHANGELOG.md` 与 Release 说明，然后清空本文件。
>
> **为什么要有这个文件**：改动分散在多轮对话里，等到发版时容易漏记
> （尤其是「修了 bug 却没写说明」）。账本只记录事实，不依赖对话上下文。
>
> **写法**：一条 5 行以内，只写「改了什么 / 动到哪 / 怎么验证 / 影响」，
> 不写推理过程。**追加，不改旧条目**。

当前稳定版本：`0.1.4`
最后更新：2026-10-23

---

## 2026-10-23 — README 新增权重分层与迁移章节

- **类型**：文档
- **内容**：新增 `### 权重分层`（四处权重的来源与调法：custom_phrase 99、
  补全 0.9、mixed_tokens 1000/100000、numeric 1000、import_tables 顺序）与
  `### 迁移整套配置`（两种场景、必带文件清单、不必带的文件、迁移后校验命令）；
  目录新增「换台机器 / 搬到别的输入法」「调整候选排序（权重）」两条入口
- **文件**：`README.md`
- **验证**：锚点全部有效、代码块 38 个配对；`gen-word-table.ps1` 四个参数实测可用；
  `dict-probe` 实测 `chatgpt` → ChatGPT（1 条）、`g` → 个/过/国；
  补记一处准确性修正：含数字的示例必须用小键盘，probe 一次性传数字串会得到不同分段
- **影响**：纯文档；不影响安装包内容与版本号

---
## 2026-10-23 — 改用 snapshot 分支模型

- **类型**：流程
- **内容**：新增 `snapshot` 工作分支，日常改动提交推送至此；`master` 只接受
  `git merge --ff-only snapshot`，保持为稳定版且内容永远等于某个 snapshot 提交。
  明确「提交 ≠ 发布」：提交只进 snapshot，只有「更新版本」才动 master / 版本号 / tag / Release。
  发版顺序调整为「bump → 打包验证 → 才合并 master → 才打 tag」，打包失败不污染稳定版
- **文件**：`.dsh/skills/github-release-discipline/SKILL.md`（含用户级副本）、`README.md`
- **验证**：snapshot 分支已建立并推送（起点 `9d7eeab` = 当时 master）；
  两份 skill 哈希一致；README 新增「分支模型」小节、锚点全部有效、代码块配对
- **影响**：流程变更；不改版本号、不影响安装包

---
## 2026-10-24 — 英文白名单 +3 词与大小写双份候选

- **类型**：新增
- **内容**：
  ① 白名单 80 → **83**：`Sunshine`、`Moonlight`、`ffmpeg`（编码为对应小写）
  ② 新增 `lua/lowercase_candidate_filter.lua`：纯字母的英文候选后紧跟一个全小写版本
  （`github` → `GitHub | github`、`githu` 补全 → `GitHub | github`）。
  只处理纯 ASCII 字母，型号（ESP32）、中英混排（X光）、本就全小写的词（ffmpeg）不生成影子候选
- **文件**：`custom_phrase.txt`、`mixed_tokens.dict.yaml`、`lua/lowercase_candidate_filter.lua`、
  `rime_ice.custom.yaml`（`engine/filters/+` 注册，排在 dedupe 之后）、
  `docs/CUSTOM-WORDS.md`、`README.md`、`gen-word-table.ps1`（分类表）
- **验证**：`github` → GitHub|github、`vscode` → VSCode|vscode、`v` → Vim|vim|VisualStudio|visualstudio；
  `githu` → GitHub ~b | github ~b；`ffmpeg` 仅一条；`esp32`/`X光`/`sj` 不受影响；
  选词正常（`ni1`→你、`ni5`→拟、`github1`→GitHub）；造句 `chatgptqishi…` → ChatGPT其实也就那样
- **影响**：需重新部署；英文候选数量翻倍（预期行为）；未改版本号

---

## 2026-10-24 — 宿主兼容：候选窗不显示的定位与修复

- **类型**：修复
- **内容**：Geek Uninstaller 搜索栏里能打字、能选词，但候选窗不显示。经提权监控确认
  该宿主是**正常 64 位 TSF 路径**（`weasel.dll`/`msctf.dll` 均已加载），
  `Show` 诊断日志显示 20 次 `show=1 composing=1 kbDisabled=0` 但 **`posValid=0`**
  —— 宿主不提供 `TfContextView`，`_SetCompositionPosition` 从未被调用，位置永远无效。
  三处修改：① CUAS 分支不再直接 return，改用 `GetForegroundCaretRect` 兜底定位；
  ② `_ShowUI` 从「无条件隐藏」改为「组合活跃且非抑制期才显示」（避免某些宿主在
  doc focus 变化时把窗口藏掉后不再重绘）；③ `CCandidateList::Show` 加开关式诊断日志
  （`WEASEL_SHOW_LOG=1` 时写 `%TEMP%\weasel-show.log`）
- **文件**：`src/WeaselTSF/Composition.cpp`、`CandidateList.cpp`、`CandidateList.h`
- **验证**：编译 x64/x86 通过；host-regression / panel-bench / SSF 237 项通过；
  诊断日志成功定位到 `posValid=0` 这一根因
- **影响**：已部署（System32 `1C75FEC53D1D`、SysWOW64 `1585470CE7ED`）；
  **Geek 的候选窗仍未显示**，宿主侧未提供文本布局信息，兜底定位未生效，暂不继续追

---
## 2026-10-24 — Shift+Enter 与 Enter 一致（去掉自动分隔符）

- **类型**：修复
- **内容**：`Shift+Enter` 此前没有绑定，落到 Rime 默认路径，会把输入串连同自动插入的
  音节分隔符一起上屏（`nihaowoshiyanghanlin` → `ni'hao'wo'shi'yanghanlin`）。
  在 `key_binder/bindings` 增加 `{accept: "Shift+Return", send: Return, when: composing}`，
  让它走 `commit_raw_input`，与 `Enter` 完全一致。手动输入的 `'`（如 `xi'an`）仍保留
- **文件**：`rime_ice.custom.yaml`（`key_binder/bindings`）、`README.md`
  （按键总表、输入示例、已知限制）；同时回滚上一轮误加的「双单引号上屏」
- **验证**：`nihaowoshiyanghanlin{Shift+Return}` → `nihaowoshiyanghanlin`（无分隔符）；
  `nihao{Shift+Return}` → `nihao`；`esp32{Shift+Return}` → `esp32`；
  与 `{Return}` 结果完全一致；`nihao{space}` → 你好、`ni1` → 你 回归正常
- **影响**：需重新部署；未改版本号

---
## 2026-10-26 — 抑制误触单字母触发英文补全

- **类型**：修复
- **内容**：误触 `u` / `i` 会让整句被英文吃掉（`uqnali` → `Ubuntu去哪里`，
  本意 `qunali` → 去那里）。根因是 `u` 是 `ubuntu` 的**合法首音节**，
  参与造句后成为该输入串的唯一解析，降权无效。处理：
  ① `gen-mixed-tokens.ps1` 跳过首字母为元音（a/e/i/o/u/v）的英文词，
  这些词**不参与造句**，只保留精确码与前缀补全（`ubuntu` / `ubun` 仍出 Ubuntu）；
  ② 新增 `lua/short_prefix_filter.lua`，单字母音节下丢弃开头有 ≥2 个英文字母的候选
  （`u` → Ubuntu、`i` → Intel 不再出现；`x` → X光 保留）
- **文件**：`src/build-support/gen-mixed-tokens.ps1`、`mixed_tokens.dict.yaml`、
  `lua/short_prefix_filter.lua`、`rime_ice.custom.yaml`（`engine/filters/+`）、
  `docs/CUSTOM-WORDS.md`、`README.md`（已知限制）
- **验证**：`uqnali`/`iqnali` 不再出英文；`u` → 有/又/由、`i` → 一/以/已；
  `ubuntu`/`ubun`/`intel`/`vscode`/`vscod` 精确码与补全完好；
  `chat`/`githu`/`markdow` 补全正常；`qunali` → 去哪里、`nihao` → 你好 回归正常。
  **代价**：这 18 个词不再参与造句（`ubuntuhqishi…` 不再出「Ubuntu其实也就那样」）
- **影响**：需重新部署；mixed_tokens 由 85 条减为 70 条；未改版本号

---
## 2026-10-26 — 误触纠正：u + 声母自动交换

- **类型**：新增
- **内容**：手快把 `u` 与声母打反时自动交换：`u` 后面紧跟 `j/q/x/y` 时改为
  `qu/ju/xu/yu`，从而正常参与分词与造句（`uqnali` → `qunali` → 去那里）。
  拼音里 `uj/uq/ux/uy` 都不是合法音节，所以该组合只可能是打反了，不会误伤。
  单独输入 `u`（中文短码「有/又/由」）不受影响
- **文件**：`lua/keypad_input.lua`（`func` 入口拦截）、`README.md`（新增「误触纠正」小节）
- **验证**：`uqnali` → 去哪里、`uq` → 去|取|区、`uj` → 巨|举|居、`ux` → 许|续|需、
  `uy` → 与|于|语；`u` → 有|又|由、`un`/`ui`/`uan` 不交换；
  `qunali`/`nihao`/`ni1`/`woshi`+KP4/`nihao-woshi` 回归正常；
  Lua 100 项、SSF 237 项、panel-bench、host-regression 通过
- **影响**：需重新部署；未改版本号

---
## 2026-10-26 — 误触纠正扩展到 74 种颠倒组合

- **类型**：新增
- **内容**：上一轮只处理 `u` + 声母（`uq → qu`）。本轮从 `cn_dicts/8105` 提取全部
  414 个合法音节，枚举相邻字母交换，找出「变体非法、原音节合法」的一对一映射，
  共 **74 条**（元音 + 辅音的两字母组合，如 `ab → ba`、`ia → ai`、`vl → lv`）。
  用 `speller/algebra` 的 `derive` 给正确音节派生颠倒写法，**不改写输入串**，
  因此不影响继续输入（`baba` → 爸爸、`jintian` → 今天）
- **文件**：`rime_ice.custom.yaml`（新增 `speller/algebra/+` 74 条）、`README.md`
- **验证**：`ab` → 把|吧|八、`ia` → 爱|艾、`ij` → 级|集|几、`ib` → 比|必、
  `uq/uj/ux/uy` → 去/巨/许/与、`oa` → 傲|奥、`ow` → 我|握、`vl` → 绿|律、
  `vn` → 女；`uqnali` → 去哪里；正常输入 `nihao`/`zhongguo`/`jintian`/`baba`/
  `aiqing`/`qunali` 全部不受影响；选词、`-` 造句、英文补全、计算器均正常；
  部署耗时 5.3 → 9.5 秒；Lua 100 项、SSF 237 项、panel-bench、host-regression 通过
- **影响**：需重新部署；prism 3187 KB；未改版本号

---
## 发版时怎么用这份账本

1. 从上到下读完所有条目（不要只读最近的）
2. 按 `类型` 归入 CHANGELOG 的「新增 / 修复 / 变更移除 / 文档」
3. 逐条核对：每条都进了发布说明，没有遗漏
4. 发布后清空本文件（保留标题与说明），并在 `CHANGELOG.md` 顶部记下对应版本