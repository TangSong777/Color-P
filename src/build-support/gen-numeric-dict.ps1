# 生成 numeric.dict.yaml：把 0..N 全部登记为词条。
#
# 用途：让「任意数字」能像音节一样参与 Rime 的造句。配合 rime_ice.custom.yaml 里
# speller/alphabet 加入 0-9，以及 keypad_input.lua 里「主键盘数字只选词」的拦截，
# 就能打出 youyijianshiqingshishuo32meiyou26da -> 有一件事情是说32没有26大。
#
# 用法：
#   pwsh -File .\src\build-support\gen-numeric-dict.ps1 -Max 99999 -Out <用户目录>\numeric.dict.yaml
param(
  [int]$Max = 99999,
  [string]$Out = (Join-Path $env:APPDATA 'Rime\numeric.dict.yaml')
)

$ErrorActionPreference = 'Stop'
$enc = New-Object Text.UTF8Encoding($false)
$sb = New-Object System.Text.StringBuilder

foreach ($line in @(
  '# Rime dictionary',
  '# encoding: utf-8',
  '#',
  '# 由 src/build-support/gen-numeric-dict.ps1 生成，请勿手工编辑。',
  '# 让任意数字能作为音节参与造句；范围 0..' + $Max,
  '---',
  'name: numeric',
  'version: "1.0"',
  'sort: by_weight',
  '...',
  ''
)) { [void]$sb.AppendLine($line) }

for ($i = 0; $i -le $Max; $i++) { [void]$sb.AppendLine("$i`t$i`t1000") }

New-Item -ItemType Directory -Force -Path (Split-Path $Out -Parent) | Out-Null
[IO.File]::WriteAllText($Out, $sb.ToString(), $enc)

$size = [math]::Round((Get-Item $Out).Length / 1KB)
Write-Host "生成 $Out  (0..$Max, $size KB)"
