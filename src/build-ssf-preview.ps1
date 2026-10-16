# Build the offscreen SSF skin preview tool.
#
# The tool deliberately compiles only the platform-free SSF sources plus the
# GDI+ image loader, so it needs neither ATL/WTL (WeaselUI's precompiled header
# stack) nor Boost/WinSparkle/librime. That keeps the edit-compile-look loop
# short when tuning skin geometry.
#
# Usage: pwsh -File build-ssf-preview.ps1
# Output: dist\ssf_preview.exe

$ErrorActionPreference = 'Stop'

$Root    = $PSScriptRoot
# 工具链位置由 build-support\Find-Toolchain.ps1 探测（可用环境变量覆盖）。
. (Join-Path $Root 'build-support\Find-Toolchain.ps1')
$VcVars  = Get-VcVarsPath x64
$SdkInc  = Get-SdkIncludeDir
$SdkLib  = Get-SdkLibDir x64
$MsvcInc = Join-Path (Get-MsvcRoot) 'include'
$MsvcLib = Join-Path (Get-MsvcRoot) 'lib\x64'
$OutDir  = Join-Path $Root 'dist'
$ObjDir  = Join-Path $Root 'msbuild\ssf_preview'

if (-not (Test-Path $VcVars)) { throw "vcvars64.bat not found: $VcVars" }
New-Item -ItemType Directory -Force -Path $OutDir, $ObjDir | Out-Null

$sources = @(
  'ssf_preview\ssf_preview.cpp',
  'WeaselUI\ssf\SsfImage.cpp',
  'WeaselUI\ssf\SsfImageLoader.cpp',
  'WeaselUI\ssf\SsfIniParser.cpp',
  'WeaselUI\ssf\SsfLayout.cpp',
  'WeaselUI\ssf\SsfRenderer.cpp',
  'WeaselUI\ssf\SsfSkin.cpp'
)

# NOMINMAX: the tool does not include WeaselUI's stdafx.h, but the SSF headers
# guard for it anyway; defining it here keeps the command line honest.
$defines = @('NOMINMAX', 'WIN32_LEAN_AND_MEAN', '_UNICODE', 'UNICODE', 'NDEBUG')

$flags = @(
  '/nologo', '/c', '/TP', '/MT', '/O2', '/Oi', '/EHsc', '/GR',
  '/Zc:wchar_t', '/Zc:forScope', '/Zc:inline', '/GS', '/Gy', '/fp:precise',
  '/Gd', '/utf-8', '/W3', '/wd4996',
  "/I`"$Root`"",
  "/I`"$Root\WeaselUI`"",
  "/I`"$Root\WeaselUI\ssf`"",
  "/I`"$SdkInc\ucrt`"",
  "/I`"$SdkInc\shared`"",
  "/I`"$SdkInc\um`"",
  "/I`"$MsvcInc`""
)

$objs = @()
$fail = @()

foreach ($src in $sources) {
  $full = Join-Path $Root $src
  if (-not (Test-Path $full)) { $fail += "missing source: $src"; continue }
  $obj = Join-Path $ObjDir (($src -replace '[/\\]', '_') -replace '\.cpp$', '.obj')
  $objs += $obj
  $cmd = "cl.exe " + ($flags -join ' ') + " " +
         (($defines | ForEach-Object { "/D$_" }) -join ' ') +
         " /Fo`"$obj`" `"$full`""
  $out = cmd /c "call `"$VcVars`" >nul 2>&1 && $cmd" 2>&1
  if ($LASTEXITCODE -ne 0) {
    $fail += $src
    Write-Host "FAILED: $src"
    $out | Select-Object -Last 15 | ForEach-Object { Write-Host "  $_" }
  } else {
    Write-Host "ok: $src"
  }
}

if ($fail.Count) {
  Write-Host ''
  Write-Host 'Build failed:'
  $fail | ForEach-Object { Write-Host "  $_" }
  exit 1
}

$exe = Join-Path $OutDir 'ssf_preview.exe'

# Object paths are passed inline rather than through a link.exe response file.
# The checkout lives under a path with non-ASCII characters, and a response file
# written as ASCII turns those into '?', after which link.exe silently sees no
# object files at all (LNK4001 + unresolved mainCRTStartup). There are only
# seven objects, so inline arguments are well under the command-line limit.
$objArgs = ($objs | ForEach-Object { "`"$_`"" }) -join ' '

$link = "link.exe /nologo /OUT:`"$exe`" /SUBSYSTEM:CONSOLE /MACHINE:X64 " +
        "/LIBPATH:`"$SdkLib\ucrt\x64`" " +
        "/LIBPATH:`"$SdkLib\um\x64`" " +
        "/LIBPATH:`"$MsvcLib`" " +
        "$objArgs gdiplus.lib user32.lib gdi32.lib ole32.lib"
$out = cmd /c "call `"$VcVars`" >nul 2>&1 && $link" 2>&1
if ($LASTEXITCODE -ne 0) {
  Write-Host 'Link failed:'
  $out | Select-Object -Last 20 | ForEach-Object { Write-Host "  $_" }
  exit 1
}

Write-Host ''
Write-Host "Built $exe ($((Get-Item $exe).Length) bytes)"
