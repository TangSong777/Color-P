# 生成「自定义字词总表」(docs/CUSTOM-WORDS.md)。
#
# 数据来源（唯一事实来源，不手工抄写）：
#   custom_phrase.txt        —— 英文白名单 + 中文短码
#   mixed_tokens.dict.yaml   —— 参与造句的英文词条
#   numeric.dict.yaml        —— 数字词条（只统计范围，不逐条列出）
#
# 用法：
#   pwsh -File .\src\build-support\gen-word-table.ps1 `
#       -CustomPhrase <用户目录>\custom_phrase.txt `
#       -MixedTokens  <用户目录>\mixed_tokens.dict.yaml `
#       -NumericDict  <用户目录>\numeric.dict.yaml `
#       -Out          <仓库>\docs\CUSTOM-WORDS.md
param(
  [string]$CustomPhrase = (Join-Path $env:APPDATA 'Rime\custom_phrase.txt'),
  [string]$MixedTokens  = (Join-Path $env:APPDATA 'Rime\mixed_tokens.dict.yaml'),
  [string]$NumericDict  = (Join-Path $env:APPDATA 'Rime\numeric.dict.yaml'),
  [string]$Out          = (Join-Path (Split-Path $PSScriptRoot -Parent | Split-Path -Parent) 'docs\CUSTOM-WORDS.md')
)

$ErrorActionPreference = 'Stop'
$enc = New-Object Text.UTF8Encoding($false)

foreach ($f in @($CustomPhrase, $MixedTokens)) {
  if (-not (Test-Path $f)) { throw "找不到 $f" }
}

# 分类表：保持与 README 的白名单分组一致；未命中的词条归入「其他」
$categories = [ordered]@{
  'AI/模型'  = @('ChatGPT','Gemini','DeepSeek','OpenCode','Anthropic','OpenAI','Claude',
                 'Copilot','Qwen','Kimi','Grok','prompt')
  '厂商/平台' = @('MicroSoft','Google','Windows','Ubuntu','Linux','Debian','Docker','Vim',
                 'VMware','VPN','NVIDIA','Intel','AMD','GitHub','GitLab','VSCode',
                 'PowerShell','WinScp')
  '语言/格式' = @('Python','Java','Rust','JavaScript','Markdown','English','Chinese','HDMI','SSD','SATA','CPU','GPU','RAM','NVMe')
  '软件/游戏' = @('Minecraft','Undertale','CDDA','Clash','MATLAB','PyCharm','VisualStudio',
                 'Multisim','SolidWorks','AutoCAD','STM32CubeMX','STM32CubeIDE','Keil',
                 'Arduino','PlatformIO','Blender','Chrome','Geek','Flash','OBS',
                 'PowerPoint','Excel','Word','XMind','Obsidian','Notion')
  '工具/其他' = @('Listary','PixPin','ZeroTier','Wallpaper','Desktop','Patina','README')
}

function Get-Category([string]$word) {
  foreach ($name in $categories.Keys) {
    if ($categories[$name] -contains $word) { return $name }
  }
  return '其他'
}

# ---- 读 custom_phrase.txt ----
$rows = [IO.File]::ReadAllLines($CustomPhrase, $enc) |
        Where-Object { $_ -match "`t" -and $_ -notmatch '^#' }

$english = @()
$chinese = @()
foreach ($line in $rows) {
  $p = $line -split "`t"
  $word = $p[0]; $code = $p[1]
  if ($word -match '[^\x00-\x7F]') { $chinese += [pscustomobject]@{ Word = $word; Code = $code } }
  else                             { $english += [pscustomobject]@{ Word = $word; Code = $code } }
}

# ---- 读 mixed_tokens.dict.yaml（词条 / 编码 / 权重）----
$tokens = @()
if (Test-Path $MixedTokens) {
  foreach ($line in ([IO.File]::ReadAllLines($MixedTokens, $enc) | Where-Object { $_ -match "`t" -and $_ -notmatch '^#' })) {
    $p = $line -split "`t"
    $tokens += [pscustomobject]@{ Word = $p[0]; Code = $p[1]; Weight = $(if ($p.Count -gt 2) { $p[2] } else { '' }) }
  }
}
$tokenCodes = @{}
foreach ($t in $tokens) { $tokenCodes[$t.Code] = $true }

# ---- 读 numeric.dict.yaml 的范围 ----
$numMin = $null; $numMax = $null; $numCount = 0
if (Test-Path $NumericDict) {
  $numLines = [IO.File]::ReadAllLines($NumericDict, $enc) | Where-Object { $_ -match '^\d+\t' }
  $numCount = ($numLines | Measure-Object).Count
  $nums = $numLines | ForEach-Object { [int]($_ -split "`t")[0] }
  if ($numCount) { $numMin = ($nums | Measure-Object -Minimum).Minimum; $numMax = ($nums | Measure-Object -Maximum).Maximum }
}

$now = Get-Date -Format 'yyyy-MM-dd HH:mm'
$sb = New-Object System.Text.StringBuilder

function Add-Line([string]$s = '') { [void]$sb.AppendLine($s) }

Add-Line '# 自定义字词总表'
Add-Line ''
Add-Line '> 本文件由 `src/build-support/gen-word-table.ps1` 从实际词条文件生成，**请勿手工编辑**。'
Add-Line '>'
Add-Line "> 生成时间：$now"
Add-Line ''
Add-Line '## 概览'
Add-Line ''
Add-Line '| 类别 | 数量 | 输入方式 | 参与造句 |'
Add-Line '|---|---:|---|---|'
Add-Line "| 英文白名单 | $($english.Count) | 全小写编码 | ✅（同时也是词典词条） |"
Add-Line "| 中文短码 | $($chinese.Count) | 单/双字母 | — |"
if ($numCount) {
  Add-Line "| 数字词条 | $numCount（$numMin–$numMax） | **小键盘**数字 | ✅ |"
}
Add-Line "| **合计自维护词条** | $($english.Count + $chinese.Count) | — | — |"
Add-Line ''
Add-Line '> 数字词条由脚本批量生成，不逐条列出；范围可改（见文末）。'
Add-Line ''

# ---- 英文白名单 ----
Add-Line "## 英文白名单（$($english.Count) 条）"
Add-Line ''
Add-Line '按分类排列。编码一律小写；**输入时必须小写**（大写走方案的「大写字母造词」通道）。'
Add-Line ''
Add-Line '| # | 词条 | 编码 | 分类 | 参与造句 |'
Add-Line '|---:|---|---|---|:---:|'
$i = 0
$ordered = $english | Sort-Object @{ Expression = { $categories.Keys.IndexOf((Get-Category $_.Word)) } }, Word
foreach ($e in $ordered) {
  $i++
  $join = if ($tokenCodes.ContainsKey($e.Code)) { '✅' } else { '—' }
  Add-Line "| $i | $($e.Word) | ``$($e.Code)`` | $(Get-Category $e.Word) | $join |"
}
Add-Line ''

# ---- 型号词条（有编码但不在 custom_phrase 里的）----
$extraTokens = $tokens | Where-Object { -not ($english | Where-Object { $_.Code -eq $_.Code -and $_.Word -eq $_.Word }) }
$extraTokens = @($tokens | Where-Object {
  $tok = $_
  $tok.Code -notin @('vvv','vvu') -and
  -not ($english | Where-Object { $_.Word -eq $tok.Word -and $_.Code -eq $tok.Code })
})
if ($extraTokens.Count) {
  Add-Line "## 其它参与造句的词条（$($extraTokens.Count) 条）"
  Add-Line ''
  Add-Line '数字需用**小键盘**输入；它们同样参与造句。'
  Add-Line ''
  Add-Line '| # | 词条 | 编码 | 权重 |'
  Add-Line '|---:|---|---|---:|'
  $j = 0
  foreach ($t in $extraTokens) {
    $j++
    Add-Line "| $j | $($t.Word) | ``$($t.Code)`` | $($t.Weight) |"
  }
  Add-Line ''
}

# ---- 中文短码（按编码分组）----
$groups = [ordered]@{}
foreach ($c in $chinese) {
  if (-not $groups.Contains($c.Code)) { $groups[$c.Code] = @() }
  $groups[$c.Code] += $c.Word
}
Add-Line "## 中文短码（$($chinese.Count) 条 / $($groups.Count) 个编码）"
Add-Line ''
Add-Line '同一编码下按候选顺序排列（第一条优先级最高）。'
Add-Line ''
Add-Line '| # | 编码 | 词条（按候选顺序） | 说明 |'
Add-Line '|---:|---|---|---|'
$k = 0
foreach ($code in $groups.Keys) {
  $k++
  $words = ($groups[$code] | ForEach-Object { "「$_」" }) -join ' '
  $note = switch ($code) {
    'hm'  { '语气词' }
    'm'   { '语气词' }
    'u'   { '高频单字，u+数字 可调序' }
    'i'   { '高频单字，i+数字 可调序' }
    'uuuu'{ '网络梗「又双叒叕」' }
    default { '常用简拼' }
  }
  Add-Line "| $k | ``$code`` | $words | $note |"
}
Add-Line ''

# ---- 数字 ----
if ($numCount) {
  Add-Line '## 数字词条'
  Add-Line ''
  Add-Line "由 ``numeric.dict.yaml`` 覆盖 **$numMin–$numMax**，共 $numCount 条，权重统一 1000。"
  Add-Line ''
  Add-Line '它们的作用是让数字能像音节一样参与造句：'
  Add-Line ''
  Add-Line '```'
  Add-Line 'youyijianshiqingshishuo32meiyou26da  →  有一件事情是说32没有26大'
  Add-Line '```'
  Add-Line ''
  Add-Line '需要更大范围时重新生成（会增大词库和部署时间）：'
  Add-Line ''
  Add-Line '```powershell'
  Add-Line 'pwsh -File .\src\build-support\gen-numeric-dict.ps1 -Max 999999'
  Add-Line '```'
  Add-Line ''
}

# ---- 使用规则 ----
Add-Line '## 使用规则'
Add-Line ''
Add-Line '| 规则 | 说明 |'
Add-Line '|---|---|'
Add-Line '| 编码一律小写 | 打 `CPU` 得到的是大写字母造词，不是词条 |'
Add-Line '| 编码与词条都不含空格 | `visualstudio`、`platformio` |'
Add-Line '| 含数字的词编码去数字 | `STM32CubeMX` → `stmcubemx`；数字编码本身要用小键盘 |'
Add-Line '| 主键盘数字只选词 | 1–9（0 = 第 10 个）永远是选词，不会进入编码 |'
Add-Line '| 小键盘数字才输入数字 | `KP_0`–`KP_9` 参与拼写与造句 |'
Add-Line '| 英文词条可参与造句 | 如 `arduinoqishiyejiunayang` → 「Arduino其实也就那样」 |'
Add-Line ''

# ---- 维护方式 ----
Add-Line '## 如何增删词条'
Add-Line ''
Add-Line '| 想改什么 | 改哪里 | 之后做什么 |'
Add-Line '|---|---|---|'
Add-Line '| 英文词 / 中文短码 | `custom_phrase.txt`（`词条<Tab>编码`） | 重新部署 |'
Add-Line '| 让某个英文词参与造句 | `mixed_tokens.dict.yaml` 加一行 | 重新部署 |'
Add-Line '| 数字范围 | 重跑 `gen-numeric-dict.ps1` | 重新部署 |'
Add-Line '| 本表格 | 重跑 `gen-word-table.ps1` | 提交 |'
Add-Line ''
Add-Line '---'
Add-Line ''
Add-Line '返回 [README](../README.md)'

New-Item -ItemType Directory -Force -Path (Split-Path $Out -Parent) | Out-Null
[IO.File]::WriteAllText($Out, $sb.ToString(), $enc)

Write-Host "生成 $Out"
Write-Host "  英文 $($english.Count) 条 / 中文 $($chinese.Count) 条 / 型号 $($extraTokens.Count) 条 / 数字 $numCount 条"
