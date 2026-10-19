---
name: github-release-discipline
description: Use when maintaining the Color-P GitHub repository — committing, pushing, bumping a version, building the installer, tagging, publishing a release, writing release notes, or rolling one back. Enforces "no automatic sync or version bump" and the exact release/rollback checklist.
---

# 维护 Color-P 的 GitHub（仓库 + 发布）

## 铁律：不做自动同步

用户的明确要求：

- **未经明确指令，不提交、不推送、不升版本、不打标签、不重建发布包。**
- 日常改动（加词、修 bug、改文档、调参数）只做三件事：
  **改本地文件 → 部署验证 → 留在工作区**。
- 项目始终只维护**一个稳定版本**；改动先累积，等指令再一并定稿。
- 用户只是提了个修改建议时，改完、验证完、报告即可 —— **不要顺手 `git commit`**。

只有这几类指令才动 git 与 Release：「更新版本」「发版」「同步仓库」「回滚」。

判断口诀：**内容改动 ≠ 版本改动**。内容改动默认留在本地。

## 每次改动都要记账

改完任何东西（代码、配置、词条、文档、脚本），**立即**在仓库根的
`PENDING-CHANGES.md` 末尾**追加**一条，5 行以内：

```markdown
## YYYY-MM-DD — 一句话标题
- **类型**：新增 / 修复 / 变更 / 文档 / 流程
- **内容**：改了什么（一句话）
- **文件**：动到哪些文件
- **验证**：怎么确认有效（测试、探针、哈希）
- **影响**：是否需要重新部署 / 重启 / 用户可见
```

规则：

- **追加，不改旧条目**（历史是事实，不回头修饰）
- 只写事实，不写推理过程 —— 这个文件是给「发版时的自己」看的，不是日记
- 为什么必须做：改动散落在多轮对话里，等到发版时极易漏记，
  典型后果是「修了 bug 却没写进发布说明」

> 若一次改动本身就是在建立/调整这套流程，也照记一条（自指没关系）。

## 收到「更新版本」指令后的固定顺序

不可跳步、不可提前。

1. **读账本 + 汇总**：
   - 完整读取 `PENDING-CHANGES.md`（**从上到下全读，不要只读最近的**）
   - `git log --oneline <上一个标签>..HEAD`、`git status --porcelain`、`git diff --stat`
   - 两相对照，确保**没有一条改动被漏掉**（尤其是不在账本里的历史提交）
2. **写日志**：按下面的模板补进 `CHANGELOG.md`，逐条核对账本条目都进了发布说明。
3. **统一提交**：把此前累积的所有改动一次性 `git add -A` + commit + push。
4. **改版本号**（两处，必须一致）：
   - `src/installer/Color-P.iss` → `MyAppVersion`、`MyAppVersionQuad`
   - `src/installer/build-installer.ps1` → `$PackageVersion`
5. **打包**：`pwsh -File src\installer\build-installer.ps1`
6. **打标签**：`git tag -a <版本> -m "Color-P <版本>"` + `git push origin <版本>`
7. **发 Release**：exe + `SHA256SUMS.txt`，加 `--latest`
8. **清空账本**：把 `PENDING-CHANGES.md` 复原成只有标题与使用说明，并更新「当前稳定版本」
9. **核验**：见「发布后必查」

### 版本位判定（SemVer `0.0.0`）

| 位 | 条件 | 示例 |
|---|---|---|
| `MAJOR` | 不兼容的大改（引擎、安装布局、配置格式破坏） | 尚未发生 |
| `MINOR` | 新增可感知的能力 | `0.0.6 → 0.1.0` |
| `PATCH` | 修 bug / 调参 / 换词库 | `0.0.1 → 0.0.6` |

一次发布混合多种变更时取**最高位**（新特性 + 修 bug ⇒ 升 MINOR，PATCH 归零）。

### 发布日志模板

```markdown
## x.y.z — YYYY-MM-DD

**类型**：MAJOR / MINOR / PATCH（一句话说明为什么是这个位）

### 新增
- 用户能感知的新能力（怎么用、给例子）

### 修复
- 修了什么、之前错在哪、现在怎样

### 变更 / 移除
- 行为调整、配置改名、词条调整

### 升级影响
- 需要重新部署？需要重启？需要卸载旧版？有哪些不兼容点？

### 验证
- 实测过的命令与结果（测试项数、探针输出、哈希）
```

## 发布后必查（全部通过才算完成）

| 检查 | 方法 | 期望 |
|---|---|---|
| 账本已清空 | 读 `PENDING-CHANGES.md` | 只剩标题与说明，版本号已更新 |
| 账本无遗漏 | 每条都已进 CHANGELOG | 一一对应 |
| 版本号两处一致 | 读 `.iss` 与 `build-installer.ps1` | 相同 |
| exe 版本资源 | `(Get-Item $exe).VersionInfo` | FileVersion `x.y.z.0`、ProductVersion `x.y.z` |
| 载荷是最新数据 | 查 `src/installer/payload/rime-data` | 配置/词库/Lua 与 `D:\RimeUser` 哈希一致 |
| 资产哈希 | 下载回来重算 vs `SHA256SUMS.txt` | 一致 |
| 标签指向真实提交 | `git ls-remote --tags origin` | 每个版本各自的 SHA，不是同一个 |
| Latest | `gh release list` | 新版本标 `Latest` |
| README/词表 | `docs/CUSTOM-WORDS.md` 重生成 | 与词条文件一致 |

## 回滚一个版本

1. `gh release delete <版本> --repo TangSong777/Color-P --yes --cleanup-tag`
2. `git tag -d <版本>`
3. 版本号改回上一版（两处）
4. 用**当前内容**重建上一版安装包
5. `gh release upload <上一版> <exe> SHA256SUMS.txt --clobber`
6. `CHANGELOG.md`：删除被回滚版本的段落，把有效内容并入上一版
7. `README.md`：版本表与下载文件名改回
8. 提交推送（回滚属于用户明确要求，可以同步）
9. 在回复里提示：**上一版资产哈希已变**，已下载的人会对不上

## 本项目的具体坐标

| 项 | 值 |
|---|---|
| 仓库 | `TangSong777/Color-P`（master） |
| 本地仓库 | `D:\Rime`（同时是小狼毫安装目录） |
| 实时配置 | `D:\RimeUser` |
| 安装器 | `src\installer\Color-P.iss` + `build-installer.ps1` |
| 产物 | `src\installer\output\Color-P-Setup-<版本>.exe` |
| 词表文档 | `docs/CUSTOM-WORDS.md`（脚本生成） |
| 版本日志 | `CHANGELOG.md` |
| 二进制 | `src\dist\x64\WeaselServer.exe`、`weaselx64.dll`、`x86\weasel.dll` |

常用命令：

```powershell
# 打包（会从实时 RimeUser 重新暂存 payload）
pwsh -File D:\Rime\src\installer\build-installer.ps1 -WeaselRoot D:\Rime\weasel-0.17.4

# 发布
gh release create <版本> <exe> SHA256SUMS.txt --repo TangSong777/Color-P `
    --title "Color-P <版本>" --notes-file <notes.md> --latest

# 替换资产
gh release upload <版本> <exe> SHA256SUMS.txt --repo TangSong777/Color-P --clobber
```

## 已知坑（都踩过）

1. **`gh release edit --tag` 会压平历史**：它把新标签指向默认分支 HEAD，多个历史版本会指向同一提交。
   正确做法是用原始 SHA 修正：`git tag -f <新标签> <原SHA>` + `git push origin refs/tags/<新标签> --force`。
   重命名后必须核对 `git ls-remote --tags` 的 SHA 各不相同。
2. **`gh` 需要仓库上下文**：不在仓库目录时必须带 `--repo TangSong777/Color-P`。
3. **不要「按行切分再重组」改 README**：文件混用 CRLF/LF，会整段吞掉内容。
   用精确的字符串替换或编辑工具。
4. **`.NET` 相对路径按进程 CWD 解析**：`[IO.File]` 一律用绝对路径。
5. **PowerShell here-string 里的反引号/长中文串**易被转义或编码破坏；长文本用文件写入，别塞进 `-Command`。
6. **`.cmd` 必须纯 ASCII + CRLF**；含中文的 `.ps1` 需要 UTF-8 BOM。
7. **打包前核对 `dist` 与已部署二进制哈希**：不一致会产出与线上不同的包。
8. **payload 不提交**（`.gitignore`），每次打包现场从 `D:\RimeUser` 暂存；确认不含 `userdb` / `sync` / `installation.yaml`。

## 交付前的自检问句

- 用户这次说「改内容」还是「更新版本」？
- 这次改动是否已经追加到 `PENDING-CHANGES.md`？
- 如果只是改内容：我有没有误提交、误推送、误升版本？
- 如果是发版：账本是否**从头到尾**读过？每条都进发布说明了吗？
- 标签、资产哈希、Latest 三项都核过了吗？
