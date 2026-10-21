# 把新编译的 TSF DLL 部署到 System32 与 SysWOW64（无需重启系统）。
#
# 原理：Windows 允许**重命名**正在被进程加载的 DLL（映射仍指向旧文件对象），
# 随后把新文件复制到原文件名，新启动的进程就会加载新版本。
# 已经运行的进程仍持有旧 DLL，需要各自重启（或注销）才会用上新版本。
#
# 用法（二选一）：
#   1. 右键 deploy-tsf.cmd -> 以管理员身份运行
#   2. 管理员 PowerShell：pwsh -File D:\Rime\src\deploy-tsf.ps1
#
# 结果写入 C:\ProgramData\ColorPWeasel\deploy-tsf.log（可直接读取校验）。

$ErrorActionPreference = 'Stop'
$log = 'C:\ProgramData\ColorPWeasel\deploy-tsf.log'
$logDir = Split-Path $log -Parent
if (-not (Test-Path $logDir)) { New-Item -ItemType Directory -Force -Path $logDir | Out-Null }
Set-Content -Path $log -Value "deploy-tsf start $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" -Encoding UTF8
function Log([string]$m) {
    Write-Host $m
    Add-Content -Path $log -Value $m -Encoding UTF8
}

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Log "NOT-ADMIN"
    Start-Process -FilePath 'powershell.exe' -Verb RunAs -ArgumentList @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $PSCommandPath
    )
    exit 0
}
Log "ADMIN ok"

$root = 'D:\Rime'
$pairs = @(
    @{ Src = "$root\src\dist\x64\weaselx64.dll"; Dst = 'C:\Windows\System32\weasel.dll'; Name = 'System32' },
    @{ Src = "$root\src\dist\x86\weasel.dll";    Dst = 'C:\Windows\SysWOW64\weasel.dll'; Name = 'SysWOW64' }
)
$ts = Get-Date -Format 'yyyyMMddHHmmss'
$failed = 0
foreach ($p in $pairs) {
    try {
        if (-not (Test-Path $p.Src)) { throw "missing source $($p.Src)" }
        $want = (Get-FileHash $p.Src -Algorithm SHA256).Hash
        $before = if (Test-Path $p.Dst) { (Get-FileHash $p.Dst -Algorithm SHA256).Hash } else { '(none)' }
        Log "$($p.Name) before=$($before.Substring(0,[Math]::Min(16,$before.Length)))"

        $renamed = $false
        if (Test-Path $p.Dst) {
            try {
                Move-Item -LiteralPath $p.Dst -Destination "$($p.Dst).old-$ts" -Force -ErrorAction Stop
                Log "$($p.Name) renamed old -> *.old-$ts"
                $renamed = $true
            } catch {
                Log "$($p.Name) rename failed: $($_.Exception.Message)"
            }
        }
        Copy-Item -LiteralPath $p.Src -Destination $p.Dst -Force -ErrorAction Stop

        $after = (Get-FileHash $p.Dst -Algorithm SHA256).Hash
        if ($after -eq $want) {
            Log "$($p.Name) OK $($after.Substring(0,16))"
        } else {
            Log "$($p.Name) MISMATCH want=$($want.Substring(0,16)) got=$($after.Substring(0,16))"
            $failed++
        }
    } catch {
        Log "$($p.Name) FAIL $($_.Exception.Message)"
        $failed++
    }
}

# 让服务端也重新加载（TSF 客户端 DLL 由各宿主进程加载，服务端本身不用它）
Get-Process WeaselServer -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2
Start-Process -FilePath "$root\weasel-0.17.4\WeaselServer.exe" -WorkingDirectory "$root\weasel-0.17.4" -WindowStyle Hidden
Log "WeaselServer restarted"

if ($failed -eq 0) { Log "DONE ok" } else { Log "DONE failed=$failed" }
exit $failed
