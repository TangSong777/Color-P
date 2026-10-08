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
  [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'

$Root      = $PSScriptRoot
$MsBuild   = 'D:\VisualStudio2022\MSBuild\Current\Bin\MSBuild.exe'
$LibsX64   = 'D:\桌面\Harness工作区\皮肤\build-libs\rime_x64.lib'
$LibsX86   = 'D:\桌面\Harness工作区\皮肤\build-libs\rime_x86.lib'
$DistDir   = Join-Path $Root 'dist'

if (-not (Test-Path $MsBuild)) { throw "MSBuild not found: $MsBuild" }
New-Item -ItemType Directory -Force -Path $DistDir | Out-Null

function Invoke-WeaselBuild {
  param([string]$Platform)

  Write-Host "=== Building Weasel $Configuration / $Platform ==="

  # Every Weasel project resolves Boost libraries as $(BOOST_ROOT)\stage\lib
  # with no per-platform subdirectory, and both platforms emit an import library
  # literally named "rime.lib" (into lib/ vs lib64/). So the correct rime.lib
  # must be staged immediately before each platform build; the two cannot
  # coexist. Boost is easier: the x64 and x86 archives differ by the
  # "-x64-"/"-x32-" token in the filename, so both sets can sit side by side in
  # one directory and MSVC's auto-link pragma selects the right one.
  $boostStage   = 'D:\boost182\stage\lib'
  $boostStage32 = 'D:\boost182\stage32\lib'

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
    '/p:BOOST_ROOT=D:\boost182',
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
