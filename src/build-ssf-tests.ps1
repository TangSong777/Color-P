# Build and run the SSF compatibility layer unit tests.
#
# Compiles only the platform-free SSF sources, so no ATL/WTL/Boost/librime is
# needed -- the tests run in a fraction of a second and can be re-run freely
# while tuning the parser or the layout engine.
#
# Usage: pwsh -File build-ssf-tests.ps1 [-Run]

param([switch]$Run)

$ErrorActionPreference = 'Stop'

$Root    = $PSScriptRoot
$VcVars  = 'D:\VisualStudio2022\VC\Auxiliary\Build\vcvars64.bat'
$SdkInc  = 'D:\Windows Kits\10\Include\10.0.26100.0'
$MsvcInc  = 'D:\VisualStudio2022\VC\Tools\MSVC\14.44.35207\include'
$OutDir  = Join-Path $Root 'dist'
$ObjDir  = Join-Path $Root 'msbuild\ssf_tests'

if (-not (Test-Path $VcVars)) { throw "vcvars64.bat not found: $VcVars" }
New-Item -ItemType Directory -Force -Path $OutDir, $ObjDir | Out-Null

$sources = @(
  'test\TestSsfSkin\TestSsfSkin.cpp',
  'WeaselUI\ssf\SsfImage.cpp',
  'WeaselUI\ssf\SsfIniParser.cpp',
  'WeaselUI\ssf\SsfLayout.cpp',
  'WeaselUI\ssf\SsfSkin.cpp'
)

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

$exe = Join-Path $OutDir 'ssf_tests.exe'
# Object paths are passed inline rather than through a response file: the
# checkout path contains non-ASCII characters and an ASCII response file would
# silently corrupt them (link.exe then reports no object files at all).
$objArgs = ($objs | ForEach-Object { "`"$_`"" }) -join ' '

$link = "link.exe /nologo /OUT:`"$exe`" /SUBSYSTEM:CONSOLE /MACHINE:X64 " +
        "/LIBPATH:`"D:\Windows Kits\10\Lib\10.0.26100.0\ucrt\x64`" " +
        "/LIBPATH:`"D:\Windows Kits\10\Lib\10.0.26100.0\um\x64`" " +
        "/LIBPATH:`"D:\VisualStudio2022\VC\Tools\MSVC\14.44.35207\lib\x64`" " +
        "$objArgs kernel32.lib user32.lib"
$out = cmd /c "call `"$VcVars`" >nul 2>&1 && $link" 2>&1
if ($LASTEXITCODE -ne 0) {
  Write-Host 'Link failed:'
  $out | Select-Object -Last 20 | ForEach-Object { Write-Host "  $_" }
  exit 1
}

Write-Host ''
Write-Host "Built $exe"

if ($Run) {
  Write-Host ''
  & $exe
  exit $LASTEXITCODE
}
