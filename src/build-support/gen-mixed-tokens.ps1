# 生成 mixed_tokens.dict.yaml：把「英文白名单」变成可参与造句的词典词条。
#
# 为什么需要：custom_phrase 里的条目只作为独立候选，**不参与 Rime 的造句**。
# 想让 arduinoqishiyejiunayang 出「Arduino其实也就那样」，Arduino 必须是一条
# 词典词条（与 ESP32/STM32 同理）。
#
# 输入：custom_phrase.txt（取其纯英文条目）
# 输出：mixed_tokens.dict.yaml（词<Tab>小写编码<Tab>权重）
#
# 用法：
#   pwsh -File .\src\build-support\gen-mixed-tokens.ps1 `
#       -CustomPhrase <用户目录>\custom_phrase.txt -Out <用户目录>\mixed_tokens.dict.yaml
param(
  [string]$CustomPhrase = (Join-Path $env:APPDATA 'Rime\custom_phrase.txt'),
  [string]$Out = (Join-Path $env:APPDATA 'Rime\mixed_tokens.dict.yaml')
)

$ErrorActionPreference = 'Stop'
$enc = New-Object Text.UTF8Encoding($false)

if (-not (Test-Path $CustomPhrase)) { throw "找不到 $CustomPhrase" }

$rows = [IO.File]::ReadAllLines($CustomPhrase, $enc) |
        Where-Object { $_ -match "`t" -and $_ -notmatch '^#' } |
        Where-Object { $_ -notmatch '[^\x00-\x7F]' }   # 只取纯英文条目

$sb = New-Object System.Text.StringBuilder
foreach ($line in @(
  '# Rime dictionary',
  '# encoding: utf-8',
  '#',
  '# 由 src/build-support/gen-mixed-tokens.ps1 生成，请勿手工编辑。',
  '# 内容：英文白名单（来自 custom_phrase.txt）+ 含数字型号。',
  '# 作用：让这些词能像音节一样参与 Rime 造句。',
  '---',
  'name: mixed_tokens',
  'version: "1.0"',
  'sort: by_weight',
  '...',
  ''
)) { [void]$sb.AppendLine($line) }

# 额外词条：不属于白名单，但必须在重新生成时保留
#   ESP32/STM32  : 含数字型号，靠 speller/alphabet 的 0-9 输入
#   DASH/USCORE  : 内部占位符，对应 - 与 _（由 dash_filter 显示回真实字符）
#   杨寒淋       : 专名，权重给高，保证造句时压过同音词
$extra = @(
  @('ESP32', 'esp32', '100000'),
  @('STM32', 'stm32', '100000'),
  @('DASH', 'vvv', '1000'),
  @('USCORE', 'vvu', '1000'),
  @('杨寒淋', 'yanghanlin', '100000')
)
foreach ($e in $extra) { [void]$sb.AppendLine("$($e[0])`t$($e[1])`t$($e[2])") }

foreach ($line in $rows) {
  $p = $line -split "`t"
  [void]$sb.AppendLine("$($p[0])`t$($p[1].ToLower())`t1000")
}

New-Item -ItemType Directory -Force -Path (Split-Path $Out -Parent) | Out-Null
[IO.File]::WriteAllText($Out, $sb.ToString(), $enc)
$n = ($rows | Measure-Object).Count
Write-Host "生成 $Out （白名单 $n 条 + 型号 2 条）"
