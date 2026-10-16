# Build a deployable Weasel (小狼毫) from this source tree.
#
# Background
# ----------
# Weasel's own build.bat assumes it can build Boost and librime from source via
# b2 / CMake. On this machine both are impractical:
#
#   * Boost 1.82's msvc toolset hard-codes its setup script to
#     <MSVC>/bin/Hostx64/vcvarsall.bat, which does not exist in a VS 2022
#     layout. As a result "msvc-setup.nup" is never generated and b2 skips
#     every target while reporting success. See build-boost-libs.ps1.
#   * Building librime from source needs CMake + its own Boost + ninja.
#
# Instead we reuse the librime that ships with the installed Weasel (runtime
# 1.13.1) and synthesise an import library from it, and we compile the handful
# of Boost libraries Weasel links directly with cl.exe.
#
# Prerequisites produced by the sibling scripts (see docs/build-weasel.md):
#   D:\boost182\stage\lib      libboost_*-vc143-mt-s-x64-1_82.lib   (x64)
#   D:\boost182\stage32\lib    libboost_*-vc143-mt-s-x32-1_82.lib   (x86)
#   <weasel>\lib64\rime.lib    x64 import library
#   <weasel>\lib\rime.lib      x86 import library
#
# Platform library-directory conflict
# -----------------------------------
# WeaselServer.vcxproj links `$(SolutionDir)\lib` on Win32 and
# `$(SolutionDir)\lib64` on x64, while BOTH platforms emit an import library
# literally named "rime.lib". Whichever was staged last wins for the other
# platform, so the two platforms cannot be built from one staging state.
# This script therefore stages the correct rime.lib immediately before each
# platform's build, and copies the resulting binaries out before switching.
#
# Usage:
#   pwsh -File build-weasel.ps1            # x64 only
#   pwsh -File build-weasel.ps1 -Both      # x64 then Win32

param(
  [switch]$Both,
  [switch]$RegenerateProps,
  [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'

$Root      = $PSScriptRoot
# 工具链位置统一由 build-support\Find-Toolchain.ps1 探测（支持环境变量覆盖），
# 脚本里不再写死本机的 VS / SDK / Boost 路径。
. (Join-Path $Root 'build-support\Find-Toolchain.ps1')
$MsBuild   = Get-MsBuildPath
# librime 的导入库随仓库提供，放在源码树的 build-libs 下，脚本因此可搬运。
$LibsX64   = Join-Path $Root 'build-libs\rime_x64.lib'
$LibsX86   = Join-Path $Root 'build-libs\rime_x86.lib'
$DistDir   = Join-Path $Root 'dist'

# ---------------------------------------------------------------------------
# 生成 weasel.props
# ---------------------------------------------------------------------------
# 各个 .vcxproj 里写的是 <Import Project="..\weasel.props" />，也就是**仓库根的
# 上一层**。上游的做法是让你从 weasel.props.template 复制一份、填好本机路径、
# 放在那里 —— 于是仓库不自包含：换台机器克隆下来必然报
#   error MSB4019: 找不到导入的项目 "...\weasel.props"
# 这里改成由脚本生成到 .vcxproj 真正导入的位置（仓库根的上一层），路径自动探测。
# 已经存在就不覆盖（便于手工定制）；想强制重生成加 -RegenerateProps。
#
# 可用环境变量覆盖探测结果：WEASEL_BOOST_ROOT、WEASEL_SDK_ROOT、
# WEASEL_MSVC_ROOT、WEASEL_SDK_VER。
function Resolve-WeaselProps {
  param([string]$PlatformToolset = 'v143')
  # 位置以 .vcxproj 的 Import 实际解析结果为准。各工程文件写的是
  # <Import Project="..\weasel.props" />，MSBuild 最终在 src\ 下找它
  # （见 docs/build-weasel.md 与实测报错路径），所以生成到 $Root。
  $propsPath = Join-Path $Root 'weasel.props'
  if ((Test-Path $propsPath) -and -not $RegenerateProps) {
    Write-Host "  == weasel.props 已存在，沿用 ($propsPath)"
    return $propsPath
  }

  # 全部走共享探测模块（环境变量覆盖优先，其次 vswhere，最后常见目录）。
  $boost   = Get-BoostRoot
  $sdkRoot = Get-SdkRoot
  $sdkVer  = Get-SdkVersion
  $msvcRoot = Get-MsvcRoot

  $template = Join-Path $Root 'weasel.props.in'
  if (-not (Test-Path $template)) { throw "缺少生成模板: $template" }
  $xml = Get-Content $template -Raw
  $xml = $xml.Replace('@BOOST_ROOT@', $boost)
  $xml = $xml.Replace('@PLATFORM_TOOLSET@', $PlatformToolset)
  $xml = $xml.Replace('@SDK_ROOT@', $sdkRoot)
  $xml = $xml.Replace('@SDK_VER@', $sdkVer)
  $xml = $xml.Replace('@MSVC_ROOT@', $msvcRoot)

  New-Item -ItemType Directory -Force -Path (Split-Path $propsPath -Parent) | Out-Null
  Set-Content -Path $propsPath -Value $xml -Encoding UTF8
  Write-Host "  -> 生成 weasel.props ($propsPath)"
  Write-Host "     BOOST_ROOT=$boost"
  Write-Host "     MSVC=$msvcRoot"
  Write-Host "     SDK=$sdkRoot\$sdkVer"
  return $propsPath
}

if (-not (Test-Path $MsBuild)) { throw "MSBuild not found: $MsBuild" }
New-Item -ItemType Directory -Force -Path $DistDir | Out-Null
Resolve-WeaselProps | Out-Null

function Invoke-WeaselBuild {
  param([string]$Platform)

  Write-Host "=== Building Weasel $Configuration / $Platform ==="

  # 各 Weasel 工程按 $(BOOST_ROOT)\stage\lib 解析 Boost 库（不带平台子目录），
  # 而两个平台都会产出同名的 rime.lib（分别落在 lib/ 与 lib64/）。所以正确的
  # rime.lib 必须在每个平台构建前临时摆放；两者无法共存。Boost 则相反：x64 与
  # x86 的存档靠文件名里的 "-x64-"/"-x32-" 区分，可以并存于同一目录，由 MSVC 的
  # auto-link pragma 自行选择。
  $boostRoot    = Get-BoostRoot
  $boostStage   = Get-BoostStageDir x64
  $boostStage32 = Get-BoostStageDir x86
  # stage32 的库要与 stage 合并摆放（见上方说明）。
  Get-ChildItem "$boostStage32\*.lib" -ErrorAction SilentlyContinue |
    ForEach-Object { if (-not (Test-Path (Join-Path $boostStage $_.Name))) { throw "Boost library missing" } }

  if ($Platform -eq 'x64') {
    if (-not (Test-Path $LibsX64)) { throw "missing x64 rime import lib: $LibsX64" }
    Copy-Item $LibsX64 (Join-Path $Root 'lib64\rime.lib') -Force
    Write-Host "  rime import: $LibsX64"
  } else {
    if (-not (Test-Path $LibsX86)) { throw "missing x86 rime import lib: $LibsX86 (see docs/build-weasel.md)" }
    Copy-Item $LibsX86 (Join-Path $Root 'lib\rime.lib') -Force
    Write-Host "  rime import: $LibsX86"
  }
  Write-Host "  boost stage : $boostStage"

  $msbuildArgs = @(
    'weasel.sln',
    '/t:Build',
    "/p:Configuration=$Configuration",
    "/p:Platform=$Platform",
    "/p:WEASEL_ROOT=$Root",
    "/p:BOOST_ROOT=$boostRoot",
    '/v:minimal', '/nologo', '/nr:false', '/m:1'
  )
  # MSBuild constructs its tool Path. Inheriting upper-case PATH as well can
  # crash the .NET Framework tool launcher with a duplicate environment key.
  $start = [Diagnostics.ProcessStartInfo]::new()
  $start.FileName = $MsBuild
  foreach ($arg in $msbuildArgs) { $start.ArgumentList.Add($arg) }
  $start.UseShellExecute = $false
  $start.RedirectStandardOutput = $true
  $start.RedirectStandardError = $true
  # ProcessStartInfo does not inherit PowerShell's Set-Location. Build from the
  # source tree so MSBuild can resolve weasel.sln regardless of the caller.
  $start.WorkingDirectory = $Root
  [void]$start.Environment.Remove('PATH')
  [void]$start.Environment.Remove('Path')
  $proc = [Diagnostics.Process]::Start($start)
  $stdout = $proc.StandardOutput.ReadToEndAsync()
  $stderr = $proc.StandardError.ReadToEndAsync()
  $proc.WaitForExit()
  $out = ($stdout.Result + $stderr.Result) -split "`r?`n"
  $LASTEXITCODE = $proc.ExitCode
  $out | Set-Content (Join-Path $DistDir "build-$Platform.log")
  $errors = $out | Select-String -Pattern ': error |: fatal error '
  if ($LASTEXITCODE -ne 0 -or $errors) {
    $errors | Select-Object -First 25 | ForEach-Object { Write-Host $_ }
    throw "build failed for $Platform"
  }

  # Collect this platform's artifacts immediately, before the next platform's
  # build can overwrite them.
  #
  # Where each project writes its output (verified against the .vcxproj files):
  #   WeaselServer / WeaselDeployer : x64 -> output\,     Win32 -> output\Win32\
  #   WeaselTSF                     : x64 -> output\weaselx64.dll
  #                                   Win32 -> output\weasel.dll
  #   WeaselSetup                   : BOTH -> output\WeaselSetup.exe  (collides!)
  #
  # Because of the collision the two platforms cannot both be harvested from a
  # single final state, hence the copy-immediately approach and the arch suffix.
  $x64 = ($Platform -eq 'x64')
  $outDir = if ($x64) { 'output' } else { 'output\Win32' }
  $suffix = if ($x64) { 'x64' } else { 'x86' }

  $plan = @(
    @{ src = "$outDir\WeaselServer.exe";   dst = 'WeaselServer.exe' },
    @{ src = "$outDir\WeaselDeployer.exe"; dst = 'WeaselDeployer.exe' },
    @{ src = if ($x64) { 'output\weaselx64.dll' } else { 'output\weasel.dll' }
       dst = if ($x64) { 'weaselx64.dll' } else { 'weasel.dll' } },
    @{ src = 'output\WeaselSetup.exe'; dst = "WeaselSetup.$suffix.exe" }
  )

  $stale = @()
  foreach ($item in $plan) {
    $src = Join-Path $Root $item.src
    if (-not (Test-Path $src)) {
      Write-Warning "  expected artifact missing: $($item.src)"
      continue
    }
    # Architecture-namespaced copies, so a later build of the other platform
    # cannot clobber them.
    $archived = Join-Path $DistDir "$suffix\$($item.dst)"
    New-Item -ItemType Directory -Force -Path (Split-Path $archived) | Out-Null

    # MSBuild keeps an existing executable when a project is skipped as
    # up-to-date, reporting success without rewriting the file.  Copying that
    # silently republishes an old binary under a fresh build, which is how a
    # fix that only touches WeaselServer can appear "built" while dist\ still
    # holds the previous revision.  Compare before overwriting and say so.
    if ((Test-Path $archived) -and
        ((Get-FileHash $src -Algorithm SHA256).Hash -eq
         (Get-FileHash $archived -Algorithm SHA256).Hash)) {
      Write-Host ("  == dist\{0}\{1}  unchanged (source {2})" -f $suffix,
                  $item.dst, $item.src)
      $stale += $item.src
      continue
    }

    Copy-Item $src $archived -Force
    Write-Host ("  -> dist\{0}\{1}  ({2:N0} bytes)" -f $suffix, $item.dst,
                (Get-Item $src).Length)
  }

  if ($stale.Count -eq $plan.Count) {
    Write-Host ''
    Write-Host ("NOTE: nothing in $Platform changed. If you expected a rebuilt")
    Write-Host "      binary, check that the project owning your edit is in this"
    Write-Host "      platform's build graph (see the .vcxproj referencing it)."
  }
}

Invoke-WeaselBuild -Platform 'x64'
if ($Both) { Invoke-WeaselBuild -Platform 'Win32' }

Write-Host ''
Write-Host "Artifacts in $DistDir"
Get-ChildItem $DistDir | Select-Object Length, Name | Format-Table -AutoSize
