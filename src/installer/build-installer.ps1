<#
.SYNOPSIS
  暂存安装器载荷并编译出单文件安装器。

.DESCRIPTION
  本脚本把出处各异的材料汇集到 payload\，再调用 Inno Setup 编译。
  所有材料都不进仓库，每次打包现场取：

    payload\ime\           本项目编译的三个文件（来自 ..\dist）
    payload\updater\       WinSparkle（来自已装的小狼毫目录）
    payload\skin\          搜狗 Color-P 皮肤（来自本机已部署的皮肤目录）
    payload\rime-data\     雾凇拼音数据（来自本机 Rime 用户目录）
    payload\upstream-installer.exe
                           小狼毫官方安装器（优先用本机，缺失则从上游下载）

.PARAMETER SkipCompile
  只暂存载荷，不调用 Inno Setup。

.PARAMETER UpstreamInstaller
  指定官方安装器 exe 的路径。默认先找 ..\build-libs\，再找临时目录，最后下载。

.EXAMPLE
  pwsh -File .\build-installer.ps1
  pwsh -File .\build-installer.ps1 -SkipCompile
#>
[CmdletBinding()]
param(
  [switch]$SkipCompile,
  [string]$UpstreamInstaller,
  [string]$WeaselRoot,
  [string]$SkinDir,
  [string]$RimeUserDir
)

$ErrorActionPreference = 'Stop'

$Root        = $PSScriptRoot
$SrcRoot     = Split-Path $Root -Parent          # ..\src
$Payload     = Join-Path $Root 'payload'
$UpstreamVer = '0.17.4'
# Color-P 自身的版本号：与上游小狼毫版本解耦（上游版本只决定安装目录与内嵌官方安装器）。
$PackageVersion = '0.1.3'

function Say([string]$m) { Write-Host "  $m" }
function Head([string]$m) { Write-Host "`n=== $m ===" -ForegroundColor Cyan }

# ---------------------------------------------------------------- 已装的小狼毫
# 原先这里写死 D:\Rime\weasel-0.17.4。改为：命令行参数 > 注册表 > 常见位置。
# 注册表项正是小狼毫自己写的安装位置，比猜目录可靠。
if (-not $WeaselRoot) {
  foreach ($hive in @('HKLM:\SOFTWARE\Rime\Weasel', 'HKLM:\SOFTWARE\WOW6432Node\Rime\Weasel')) {
    $v = (Get-ItemProperty $hive -Name WeaselRoot -ErrorAction SilentlyContinue).WeaselRoot
    if ($v -and (Test-Path (Join-Path $v 'WeaselServer.exe'))) { $WeaselRoot = $v; break }
  }
}
if (-not $WeaselRoot) {
  $cands = @("$env:ProgramFiles\Rime\weasel-$UpstreamVer",
             "${env:ProgramFiles(x86)}\Rime\weasel-$UpstreamVer",
             "D:\Rime\weasel-$UpstreamVer", "C:\Rime\weasel-$UpstreamVer")
  $WeaselRoot = $cands | Where-Object { Test-Path (Join-Path $_ 'WeaselServer.exe') } | Select-Object -First 1
}
if (-not $WeaselRoot) {
  throw "找不到已安装的小狼毫。用 -WeaselRoot 指定其目录（需含 WeaselServer.exe）。"
}

# ---------------------------------------------------------------- 皮肤目录
if (-not $SkinDir) {
  foreach ($c in @("$env:ProgramData\ColorPWeasel\Color-P",
                   "$env:LOCALAPPDATA\ColorPWeasel\Color-P",
                   "D:\Rime\ColorPWeasel\Color-P")) {
    if ($c -and (Test-Path (Join-Path $c 'skin.ini'))) { $SkinDir = $c; break }
  }
}
if (-not $SkinDir) {
  throw "找不到已部署的 Color-P 皮肤（需含 skin.ini）。用 -SkinDir 指定。"
}

# ---------------------------------------------------------------- Rime 用户目录
if (-not $RimeUserDir) {
  $RimeUserDir = (Get-ItemProperty 'HKCU:\Software\Rime\Weasel' -ErrorAction SilentlyContinue).RimeUserDir
  if (-not $RimeUserDir) { $RimeUserDir = Join-Path $env:APPDATA 'Rime' }
}

# ---------------------------------------------------------------- 目录骨架
Head '准备目录'
foreach ($d in 'ime', 'skin', 'updater', 'rime-data') {
  New-Item -ItemType Directory -Force -Path (Join-Path $Payload $d) | Out-Null
}
Say "payload = $Payload"

# ---------------------------------------------------------------- 1. 输入法本体
Head '1/5 输入法本体（本项目编译产物）'
$built = @{
  'WeaselServer.exe' = Join-Path $SrcRoot 'dist\x64\WeaselServer.exe'
  'weaselx64.dll'    = Join-Path $SrcRoot 'dist\x64\weaselx64.dll'
  'weasel.dll'       = Join-Path $SrcRoot 'dist\x86\weasel.dll'
}
foreach ($k in $built.Keys) {
  if (-not (Test-Path $built[$k])) {
    throw "缺少编译产物 $($built[$k])。先运行 ..\build-weasel.ps1 -Both"
  }
  Copy-Item $built[$k] (Join-Path $Payload "ime\$k") -Force
  Say "$k  <- $($built[$k])"
}

# 与已部署版本对比，提醒是否忘了重新部署
foreach ($pair in @(
    @{ n = 'WeaselServer.exe'; inst = Join-Path $WeaselRoot 'WeaselServer.exe' },
    @{ n = 'weaselx64.dll';    inst = Join-Path $WeaselRoot 'weaselx64.dll' },
    @{ n = 'weasel.dll';       inst = Join-Path $WeaselRoot 'weasel.dll' })) {
  if (Test-Path $pair.inst) {
    $a = (Get-FileHash (Join-Path $Payload "ime\$($pair.n)") -Algorithm SHA256).Hash
    $b = (Get-FileHash $pair.inst -Algorithm SHA256).Hash
    if ($a -ne $b) { Write-Warning "$($pair.n)：安装器将提供刚编译的版本，与已部署的不同（正常，前提是构建比部署新）" }
  }
}

# ---------------------------------------------------------------- 2. WinSparkle
Head '2/5 自动更新组件 WinSparkle'
$ws = Join-Path $WeaselRoot 'WinSparkle.dll'
if (Test-Path $ws) {
  Copy-Item $ws (Join-Path $Payload 'updater\WinSparkle.dll') -Force
  Say "WinSparkle.dll  <- $ws"
} else {
  Write-Warning "找不到 $ws；安装器的 WinSparkle 组件会缺失（该组件是可选项）"
}

# ---------------------------------------------------------------- 3. 皮肤
Head '3/5 搜狗 Color-P 皮肤'
if (-not (Test-Path (Join-Path $SkinDir 'skin.ini'))) {
  throw "皮肤目录无效（缺 skin.ini）：$SkinDir。皮肤素材版权归原作者，仓库不收录，必须从本机已部署的皮肤取。"
}
Copy-Item (Join-Path $SkinDir '*') (Join-Path $Payload 'skin') -Force -Recurse
$n = (Get-ChildItem (Join-Path $Payload 'skin') -File).Count
Say "$n 个文件  <- $SkinDir"

# ---------------------------------------------------------------- 4. rime-ice
Head '4/5 雾凇拼音数据'
if (-not (Test-Path $RimeUserDir)) { throw "Rime 用户目录不存在：$RimeUserDir" }
foreach ($x in 'cn_dicts', 'en_dicts', 'lua', 'opencc') {
  $p = Join-Path $RimeUserDir $x
  if (Test-Path $p) {
    Copy-Item $p (Join-Path $Payload 'rime-data') -Recurse -Force
    Say "$x\"
  } else {
    Write-Warning "缺少 $p"
  }
}
# 顶层配置文件；排除个人数据（installation.yaml / user.yaml 含安装 ID 与偏好）
Get-ChildItem $RimeUserDir -File |
  Where-Object { $_.Name -notmatch '^(installation|user)\.yaml$' } |
  ForEach-Object { Copy-Item $_.FullName (Join-Path $Payload 'rime-data') -Force }
Say '顶层 *.yaml / *.txt'

# 强制校验：不留任何个人数据
$leak = @(Get-ChildItem (Join-Path $Payload 'rime-data') -Recurse -ErrorAction SilentlyContinue |
  Where-Object { $_.Name -match 'userdb|^installation\.yaml$|^user\.yaml$|^sync$' })
if ($leak.Count) { throw "打包的数据里混入了个人数据：$($leak[0].FullName)" }
Say '✅ 已确认不含个人数据（无 userdb / sync / installation.yaml / user.yaml）'

# ---------------------------------------------------------------- 5. 官方安装器
Head '5/5 小狼毫官方安装器'
$dest = Join-Path $Payload 'upstream-installer.exe'
$candidates = @()
if ($UpstreamInstaller) { $candidates += $UpstreamInstaller }
$candidates += (Join-Path $SrcRoot "build-libs\weasel-$UpstreamVer.0-installer.exe")
$candidates += (Join-Path $env:TEMP "weasel-base\weasel-$UpstreamVer.0-installer.exe")

$found = $candidates | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if ($found) {
  Copy-Item $found $dest -Force
  Say "取自 $found"
} else {
  $gh = Get-Command gh -ErrorAction SilentlyContinue
  if (-not $gh) {
    $ghPath = Join-Path $env:ProgramFiles 'GitHub CLI\gh.exe'
    if (Test-Path $ghPath) { $gh = Get-Item $ghPath }
  }
  if ($gh) {
    Say "本机没有，改用 gh 从上游下载 $UpstreamVer ..."
    & $gh.Source release download $UpstreamVer --repo rime/weasel `
        --pattern "weasel-$UpstreamVer.0-installer.exe" --dir (Join-Path $env:TEMP 'weasel-base') --clobber
    Copy-Item (Join-Path $env:TEMP "weasel-base\weasel-$UpstreamVer.0-installer.exe") $dest -Force
  } else {
    throw "找不到官方安装器，也没有 gh 可用。请手动下载 https://github.com/rime/weasel/releases/tag/$UpstreamVer 的 weasel-$UpstreamVer.0-installer.exe 并用 -UpstreamInstaller 指定。"
  }
}
Say ("大小 {0:N1} MB" -f ((Get-Item $dest).Length / 1MB))

# ---------------------------------------------------------------- 编译
if ($SkipCompile) {
  Head '已跳过编译（-SkipCompile）'
  exit 0
}

Head '调用 Inno Setup 编译'
# ISCC.exe 的查找顺序：先问注册表（Inno Setup 官方安装器会写下 InstallLocation，
# 这也是唯一能覆盖"装在自定义位置"的办法），再试 PATH，最后才是几个常见目录。
# 早先只猜固定目录，装在别处就得手改脚本。
function Find-Iscc {
  foreach ($hive in @('HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1',
                      'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1')) {
    try {
      $loc = (Get-ItemProperty -Path $hive -Name InstallLocation -ErrorAction Stop).InstallLocation
      if ($loc) {
        $c = Join-Path $loc 'ISCC.exe'
        if (Test-Path $c) { return $c }
      }
    } catch { }
  }
  $onPath = Get-Command ISCC.exe -ErrorAction SilentlyContinue
  if ($onPath) { return $onPath.Source }
  foreach ($c in @("$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
                   "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
                   "$env:ProgramFiles\Inno Setup 6\ISCC.exe")) {
    if ($c -and (Test-Path $c)) { return $c }
  }
  return $null
}
$iscc = Find-Iscc
if (-not $iscc) { throw "找不到 ISCC.exe。安装 Inno Setup 6：winget install JRSoftware.InnoSetup" }
Say "ISCC = $iscc"

& $iscc (Join-Path $Root 'Color-P.iss')
if ($LASTEXITCODE -ne 0) { throw "Inno Setup 编译失败（exit $LASTEXITCODE）" }

$out = Join-Path $Root "output\Color-P-Setup-$PackageVersion.exe"
if (Test-Path $out) {
  $h = (Get-FileHash $out -Algorithm SHA256).Hash
  Head '完成'
  Say ("产物   {0}" -f $out)
  Say ("大小   {0:N2} MB" -f ((Get-Item $out).Length / 1MB))
  Say ("SHA256 {0}" -f $h)
} else {
  throw '编译结束但找不到输出文件'
}
