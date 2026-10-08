<#
.SYNOPSIS
  运行安装器核心逻辑验证装置，返回可用作 CI 判据的退出码。

.DESCRIPTION
  主安装器要写 Program Files、注册 TSF，需要管理员权限；本装置在
  %ProgramData% 之外的自建目录里跑同一套算法，不需要管理员，也不碰系统状态。

  被验证的是安装器里最容易写错、错了又难发现的部分：
    · weasel.custom.yaml 的按行改写（UTF-8 中文注释必须逐字节保留）
    · 缺字段 / 仅 LF / 无尾换行 等边界输入
    · ExtractTemporaryFile 能否取出 dontcopy 标记的文件

  退出码：0 全部通过；1 有失败；2 装置本身出错（编译失败等）。

.EXAMPLE
  pwsh -File .\run-harness.ps1
#>
[CmdletBinding()]
param(
  [string]$WorkRoot = 'C:\ColorP-harness'
)

$ErrorActionPreference = 'Stop'
$Here = $PSScriptRoot

# 装置里的路径是编译期 #define，用 /D 覆盖成调用方指定的位置
$isccCandidates = @(
  "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
  "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
  "$env:ProgramFiles\Inno Setup 6\ISCC.exe"
)
$iscc = $isccCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) {
  Write-Error '找不到 ISCC.exe。安装 Inno Setup 6：winget install JRSoftware.InnoSetup'
  exit 2
}

Write-Host "== 编译验证装置 ==" -ForegroundColor Cyan
& $iscc "/DHarnessRoot=$WorkRoot" (Join-Path $Here 'Harness.iss') | Out-Null
if ($LASTEXITCODE -ne 0) { Write-Error "装置编译失败（exit $LASTEXITCODE）"; exit 2 }

$exe = Join-Path $Here 'output\Color-P-Harness.exe'
if (-not (Test-Path $exe)) { Write-Error "找不到装置 $exe"; exit 2 }

Write-Host "== 运行（工作目录 $WorkRoot）==" -ForegroundColor Cyan
if (Test-Path $WorkRoot) { Remove-Item $WorkRoot -Recurse -Force }

$p = Start-Process -FilePath $exe -PassThru -WindowStyle Hidden `
     -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART'
if (-not $p.WaitForExit(60000)) {
  Stop-Process -Id $p.Id -Force
  Write-Error '装置超时未退出'
  exit 2
}

$result = Join-Path $WorkRoot 'result.txt'
if (-not (Test-Path $result)) {
  Write-Error "装置没写出结果文件 $result（Setup 退出码 $($p.ExitCode)）"
  exit 2
}

# 装置以系统 ANSI 写出（Inno 的行为），本机代码页读回
$text = [Text.Encoding]::GetEncoding([Globalization.CultureInfo]::CurrentCulture.TextInfo.ANSICodePage).
        GetString([IO.File]::ReadAllBytes($result))
Write-Host $text.Trim()

$fail = ([regex]::Matches($text, '\[FAIL\]')).Count
if ($fail -gt 0) {
  Write-Host "== 有 $fail 项失败 ==" -ForegroundColor Red
  exit 1
}
Write-Host '== 全部通过 ==' -ForegroundColor Green
exit 0
