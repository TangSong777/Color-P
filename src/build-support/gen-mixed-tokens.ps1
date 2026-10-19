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

# 含数字型号：编码里含数字，靠 speller/alphabet 的 0-9 才能输入
[void]$sb.AppendLine("ESP32`tesp32`t100000")
[void]$sb.AppendLine("STM32`tstm32`t100000")

foreach ($line in $rows) {
  $p = $line -split "`t"
  [void]$sb.AppendLine("$($p[0])`t$($p[1].ToLower())`t1000")
}

New-Item -ItemType Directory -Force -Path (Split-Path $Out -Parent) | Out-Null
[IO.File]::WriteAllText($Out, $sb.ToString(), $enc)
$n = ($rows | Measure-Object).Count
Write-Host "生成 $Out （白名单 $n 条 + 型号 2 条）"
