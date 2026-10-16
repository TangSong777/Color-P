# Build the standalone panel-recreate benchmark.
#
# Compiles the WeaselUI sources (including the SSF renderer) together with
# test\TestSsfSkin\PanelRecreateBench.cpp and links a console executable.  This
# deliberately does NOT go through weasel.sln: the benchmark is a measurement
# tool, so it must not be able to break the product build.
#
# Usage: pwsh -File build-panel-bench.ps1 [-Run]

param([switch]$Run)

$ErrorActionPreference = 'Stop'

$Root    = $PSScriptRoot
# 工具链位置由 build-support\Find-Toolchain.ps1 探测（可用环境变量覆盖）。
# 原先这里写死了本机的 MSVC 版本号目录，换台机器就会指错。
. (Join-Path $Root 'build-support\Find-Toolchain.ps1')
$VcVars  = Get-VcVarsPath x64
$SdkInc  = Get-SdkIncludeDir
$SdkLib  = Get-SdkLibDir x64
$Msvc    = Get-MsvcRoot
$OutDir  = Join-Path $Root 'dist'
$ObjDir  = Join-Path $Root 'msbuild\panel_bench'
$Exe     = Join-Path $OutDir 'panel-bench.exe'

if (-not (Test-Path $VcVars)) { throw "vcvars64.bat not found: $VcVars" }
New-Item -ItemType Directory -Force -Path $OutDir, $ObjDir | Out-Null

$sources = @(
  'test\TestSsfSkin\PanelRecreateBench.cpp',
  'WeaselUI\WeaselUI.cpp',
  'WeaselUI\WeaselPanel.cpp',
  'WeaselUI\DirectWriteResources.cpp',
  'WeaselUI\Layout.cpp',
  'WeaselUI\StandardLayout.cpp',
  'WeaselUI\HorizontalLayout.cpp',
  'WeaselUI\VerticalLayout.cpp',
  'WeaselUI\VHorizontalLayout.cpp',
  'WeaselUI\FullScreenLayout.cpp',
  'WeaselUI\GdiplusBlur.cpp',
  'WeaselUI\ssf\SsfImage.cpp',
  'WeaselUI\ssf\SsfImageLoader.cpp',
  'WeaselUI\ssf\SsfIniParser.cpp',
  'WeaselUI\ssf\SsfLayout.cpp',
  'WeaselUI\ssf\SsfLayoutAdapter.cpp',
  'WeaselUI\ssf\SsfRenderer.cpp',
  'WeaselUI\ssf\SsfSkin.cpp'
)

foreach ($src in $sources) {
  if (-not (Test-Path (Join-Path $Root $src))) { throw "missing source: $src" }
}

# Do not define NOMINMAX: the WeaselUI layout sources use the Windows min/max
# macros, exactly as the product build leaves them available.
$defines = @('WIN32', 'NDEBUG', '_LIB', '_UNICODE', 'UNICODE')

$includes = @(
  "$Root", "$Root\include", "$Root\WeaselUI", "$Root\WeaselUI\ssf",
  'D:\boost182',
  "$SdkInc\ucrt", "$SdkInc\shared", "$SdkInc\um", "$SdkInc\winrt",
  "$Msvc\include", "$Msvc\atlmfc\include"
)

$flags = @(
  '/nologo', '/c', '/TP', '/std:c++17', '/MT', '/O2', '/Oi', '/EHsc', '/GR',
  '/Zc:wchar_t', '/Zc:forScope', '/Zc:inline', '/GS', '/Gy', '/fp:precise',
  '/Gd', '/utf-8', '/W3', '/wd4996', '/wd4267', '/bigobj', '/Y-'
)

# cl refuses a /Fo directory with multiple sources, so each file is compiled on
# its own into the shared object directory.
$incArgs = ($includes | ForEach-Object { "/I`"$_`"" }) -join ' '
$defArgs = ($defines | ForEach-Object { "/D$_" }) -join ' '
$prefix  = "cl.exe " + ($flags -join ' ') + " $incArgs $defArgs"

$failed = @()
foreach ($src in $sources) {
  $full = Join-Path $Root $src
  $base = [System.IO.Path]::GetFileNameWithoutExtension($full)
  $obj  = Join-Path $ObjDir "$base.obj"
  $out  = cmd /c "call `"$VcVars`" >nul 2>&1 && $prefix /Fo`"$obj`" `"$full`"" 2>&1
  if ($LASTEXITCODE -ne 0) {
    $failed += $src
    Write-Host "FAILED: $src"
    $out | Select-String -Pattern 'error' | Select-Object -First 8 |
      ForEach-Object { Write-Host "  $_" }
  } else {
    Write-Host "ok: $src"
  }
}

$out = $null
if ($failed.Count) {
  Write-Host ''
  Write-Host 'Compile failed:'
  $failed | ForEach-Object { Write-Host "  $_" }
  exit 1
}
Write-Host "compiled $($sources.Count) sources"

$objs = Get-ChildItem $ObjDir -Filter '*.obj' | ForEach-Object { "`"$($_.FullName)`"" }
if (-not $objs) { throw "no object files in $ObjDir" }
$objArgs = $objs -join ' '

$libs = 'user32.lib gdi32.lib gdiplus.lib dwrite.lib d2d1.lib ole32.lib ' +
        'oleaut32.lib advapi32.lib shlwapi.lib comctl32.lib shell32.lib uuid.lib'
$link = "link.exe /nologo /OUT:`"$Exe`" /SUBSYSTEM:CONSOLE /MACHINE:X64 " +
        "/LIBPATH:`"$SdkLib\ucrt\x64`" /LIBPATH:`"$SdkLib\um\x64`" " +
        "/LIBPATH:`"$Msvc\lib\x64`" /LIBPATH:`"$Msvc\atlmfc\lib\x64`" " +
        "$objArgs $libs"
$out = cmd /c "call `"$VcVars`" >nul 2>&1 && $link" 2>&1
$out | Set-Content (Join-Path $OutDir 'panel-bench-link.log')
if ($LASTEXITCODE -ne 0) {
  Write-Host 'Link failed:'
  $out | Select-Object -Last 25 | ForEach-Object { Write-Host "  $_" }
  exit 1
}

Write-Host "Built $Exe"

if ($Run) {
  Write-Host ''
  & $Exe
  exit $LASTEXITCODE
}
