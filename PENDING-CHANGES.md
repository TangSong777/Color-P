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
## 发版时怎么用这份账本

1. 从上到下读完所有条目（不要只读最近的）
2. 按 `类型` 归入 CHANGELOG 的「新增 / 修复 / 变更移除 / 文档」
3. 逐条核对：每条都进了发布说明，没有遗漏
4. 发布后清空本文件（保留标题与说明），并在 `CHANGELOG.md` 顶部记下对应版本