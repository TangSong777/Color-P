# 构建工具链位置探测（共享）。
#
# 背景：本仓库原先在多个脚本里写死了本机的工具链路径
# （D:\VisualStudio2022、D:\Windows Kits\10、D:\boost182、D:\Anaconda\python.exe 等），
# 换一台机器就必须逐个手改。这里把探测集中到一处。
#
# 两种用法：
#   1) 被 PowerShell 脚本 dot-source 后调用函数：
#        . "$PSScriptRoot\build-support\Find-Toolchain.ps1"
#        $vcvars = Get-VcVarsPath
#   2) 直接当命令跑，按 KEY=VALUE 输出，供 .cmd 用 for /f 解析：
#        powershell -File Find-Toolchain.ps1 -Query all
#
# 每个探测都遵守同一套约定：环境变量覆盖优先，其次用官方定位手段（vswhere），
# 最后回退到常见默认目录。

[CmdletBinding()]
param(
    [ValidateSet('none', 'all', 'vcvars64', 'vcvars32', 'sdkinc', 'sdklib', 'msvc')]
    [string]$Query = 'none'
)

Set-StrictMode -Off

function Get-VsRoot {
    # Visual Studio 安装根目录（含 VC\Tools\MSVC 的那一层）。
    if ($env:WEASEL_VS_ROOT -and (Test-Path (Join-Path $env:WEASEL_VS_ROOT 'VC\Tools\MSVC'))) {
        return $env:WEASEL_VS_ROOT
    }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $p = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
                        -property installationPath 2>$null
        if ($p) {
            $p = ($p | Select-Object -First 1).Trim()
            if (Test-Path (Join-Path $p 'VC\Tools\MSVC')) { return $p }
        }
        # 没装 C++ 工作负载时上面会空；退一步只要 installationPath
        $p = & $vswhere -latest -property installationPath 2>$null
        if ($p) { $p = ($p | Select-Object -First 1).Trim(); if (Test-Path $p) { return $p } }
    }
    foreach ($c in @("$env:ProgramFiles\Microsoft Visual Studio\2022\Enterprise",
                     "$env:ProgramFiles\Microsoft Visual Studio\2022\Professional",
                     "$env:ProgramFiles\Microsoft Visual Studio\2022\Community",
                     "$env:ProgramFiles\Microsoft Visual Studio\2022\BuildTools",
                     "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools",
                     'D:\VisualStudio2022', 'C:\VisualStudio2022')) {
        if ($c -and (Test-Path (Join-Path $c 'VC\Tools\MSVC'))) { return $c }
    }
    throw "找不到 Visual Studio（需含 C++ 工具集）。用 WEASEL_VS_ROOT 指定安装目录。"
}

function Get-MsvcRoot {
    # 最新一版 MSVC 工具集目录（含 include\ 与 lib\）。
    if ($env:WEASEL_MSVC_ROOT -and (Test-Path $env:WEASEL_MSVC_ROOT)) { return $env:WEASEL_MSVC_ROOT }
    $tools = Join-Path (Get-VsRoot) 'VC\Tools\MSVC'
    $c = Get-ChildItem $tools -Directory -ErrorAction SilentlyContinue |
         Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } |
         Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
    if (-not $c) { throw "在 $tools 下找不到 MSVC 工具集。" }
    return $c.FullName
}

function Get-VcVarsPath {
    # vcvars64.bat / vcvars32.bat：设置编译环境的标准入口。
    param([ValidateSet('x64', 'x86')][string]$Arch = 'x64')
    if ($env:WEASEL_VCVARS -and (Test-Path $env:WEASEL_VCVARS)) { return $env:WEASEL_VCVARS }
    $rel = if ($Arch -eq 'x64') { 'VC\Auxiliary\Build\vcvars64.bat' }
           else { 'VC\Auxiliary\Build\vcvars32.bat' }
    $p = Join-Path (Get-VsRoot) $rel
    if (-not (Test-Path $p)) { throw "找不到 vcvars 脚本：$p" }
    return $p
}

function Get-SdkRoot {
    if ($env:WEASEL_SDK_ROOT -and (Test-Path $env:WEASEL_SDK_ROOT)) { return $env:WEASEL_SDK_ROOT }
    foreach ($c in @("${env:ProgramFiles(x86)}\Windows Kits\10",
                     "$env:ProgramFiles\Windows Kits\10",
                     'D:\Windows Kits\10')) {
        if ($c -and (Test-Path (Join-Path $c 'Include'))) { return $c }
    }
    throw "找不到 Windows SDK。用 WEASEL_SDK_ROOT 指定安装根目录。"
}

function Get-SdkVersion {
    # 本机已安装的最新 SDK 版本号。
    if ($env:WEASEL_SDK_VER) { return $env:WEASEL_SDK_VER }
    $inc = Join-Path (Get-SdkRoot) 'Include'
    $c = Get-ChildItem $inc -Directory -ErrorAction SilentlyContinue |
         Where-Object { $_.Name -match '^\d+\.\d+\.\d+\.\d+$' } |
         Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
    if (-not $c) { throw "在 $inc 下找不到 Windows SDK 版本目录。" }
    return $c.Name
}

function Get-SdkIncludeDir { Join-Path (Get-SdkRoot) "Include\$(Get-SdkVersion)" }
function Get-SdkLibDir {
    param([ValidateSet('x64', 'x86')][string]$Arch = 'x64')
    Join-Path (Get-SdkRoot) "Lib\$(Get-SdkVersion)"
}
function Get-MsvcAtlmfcInclude { Join-Path (Get-MsvcRoot) 'atlmfc\include' }
function Get-MsvcAtlmfcLib {
    param([ValidateSet('x64', 'x86')][string]$Arch = 'x64')
    Join-Path (Get-MsvcRoot) "atlmfc\lib\$Arch"
}

function Get-BoostRoot {
    # Boost 源码/构建根目录（其下应有 stage\lib）。
    if ($env:WEASEL_BOOST_ROOT) { return $env:WEASEL_BOOST_ROOT }
    foreach ($c in @('D:\boost182', 'C:\boost182', 'D:\boost', 'C:\boost',
                     "$env:USERPROFILE\boost182", "$env:USERPROFILE\boost")) {
        if ($c -and (Test-Path (Join-Path $c 'stage\lib'))) { return $c }
    }
    # 也接受 <仓库>\..\boost* 或 <仓库>\deps\boost*
    foreach ($base in @((Split-Path $PSScriptRoot -Parent), $PSScriptRoot)) {
        if (-not $base) { continue }
        $hit = Get-ChildItem $base -Directory -Filter 'boost*' -ErrorAction SilentlyContinue |
               Where-Object { Test-Path (Join-Path $_.FullName 'stage\lib') } |
               Select-Object -First 1
        if ($hit) { return $hit.FullName }
    }
    throw "找不到 Boost（其下需有 stage\lib）。用 WEASEL_BOOST_ROOT 指定。"
}

function Get-BoostStageDir {
    param([ValidateSet('x64', 'x86')][string]$Arch = 'x64')
    $root = Get-BoostRoot
    $sub = if ($Arch -eq 'x64') { 'stage\lib' } else { 'stage32\lib' }
    $p = Join-Path $root $sub
    if (-not (Test-Path $p)) { throw "找不到 Boost 库目录：$p（Win32 需要先构建 stage32）" }
    return $p
}

function Get-MsBuildPath {
    if ($env:WEASEL_MSBUILD -and (Test-Path $env:WEASEL_MSBUILD)) { return $env:WEASEL_MSBUILD }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $p = & $vswhere -latest -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' 2>$null
        if ($p) {
            $p = ($p | Select-Object -First 1).Trim()
            if (Test-Path $p) { return $p }
        }
    }
    $cmd = Get-Command MSBuild.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    foreach ($c in @('D:\VisualStudio2022\MSBuild\Current\Bin\MSBuild.exe')) {
        if (Test-Path $c) { return $c }
    }
    throw "找不到 MSBuild.exe。用 WEASEL_MSBUILD 指定。"
}

function Get-PythonExe {
    # 测试脚本需要 Python + lupa。优先 PATH，其次几个常见发行版目录。
    foreach ($n in 'python.exe', 'python3.exe') {
        $c = Get-Command $n -ErrorAction SilentlyContinue
        if ($c) { return $c.Source }
    }
    foreach ($c in @("$env:USERPROFILE\anaconda3\python.exe",
                     "$env:USERPROFILE\miniconda3\python.exe",
                     'D:\Anaconda\python.exe', 'C:\Anaconda\python.exe',
                     'D:\Anaconda3\python.exe', 'C:\Anaconda3\python.exe',
                     "$env:LOCALAPPDATA\Programs\Python\Python312\python.exe",
                     "$env:LOCALAPPDATA\Programs\Python\Python311\python.exe")) {
        if ($c -and (Test-Path $c)) { return $c }
    }
    return $null
}

function Get-IsccPath {
    # Inno Setup 编译器。注册表 InstallLocation 是唯一能覆盖自定义安装位置的办法。
    foreach ($hive in @('HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1',
                        'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1',
                        'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1')) {
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

# ---------------------------------------------------------------------------
# CLI 模式：只有当本文件被**直接执行**（而非 dot-source）时才输出 KEY=VALUE。
# ---------------------------------------------------------------------------
if ($Query -ne 'none') {
    function Emit([string]$k, [string]$v) { Write-Output "$k=$v" }
    switch ($Query) {
        'vcvars64' { Write-Output (Get-VcVarsPath x64) }
        'vcvars32' { Write-Output (Get-VcVarsPath x86) }
        'sdkinc'   { Write-Output (Get-SdkIncludeDir) }
        'sdklib'   { Write-Output (Get-SdkLibDir x64) }
        'msvc'     { Write-Output (Get-MsvcRoot) }
        default {
            Emit 'VCVARS'    (Get-VcVarsPath x64)
            Emit 'VCVARS32'  (Get-VcVarsPath x86)
            Emit 'SDKROOT'   (Get-SdkRoot)
            Emit 'SDKVER'    (Get-SdkVersion)
            Emit 'SDKINC'    (Get-SdkIncludeDir)
            Emit 'SDKLIB'    (Get-SdkLibDir x64)
            Emit 'MSVCVER'   (Split-Path (Get-MsvcRoot) -Leaf)
            Emit 'MSVC'      (Get-MsvcRoot)
            Emit 'MSVCINC'   (Join-Path (Get-MsvcRoot) 'include')
            Emit 'MSVCLIB'   (Join-Path (Get-MsvcRoot) 'lib\x64')
            Emit 'ATLMFCINC' (Get-MsvcRoot | ForEach-Object { Join-Path $_ 'atlmfc\include' })
            Emit 'ATLMFCLIB' (Get-MsvcRoot | ForEach-Object { Join-Path $_ 'atlmfc\lib\x64' })
        }
    }
}
