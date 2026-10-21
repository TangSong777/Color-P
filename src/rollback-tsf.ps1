# 回滚 TSF DLL 到最初版本（849786D3 / B4DF28EB），用于排除回归。
# 结果写入 C:\ProgramData\ColorPWeasel\rollback-tsf.log

$ErrorActionPreference = 'Stop'
$log = 'C:\ProgramData\ColorPWeasel\rollback-tsf.log'
$logDir = Split-Path $log -Parent
if (-not (Test-Path $logDir)) { New-Item -ItemType Directory -Force -Path $logDir | Out-Null }
Set-Content -Path $log -Value "rollback start $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" -Encoding UTF8
function Log([string]$m) { Write-Host $m; Add-Content -Path $log -Value $m -Encoding UTF8 }

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Start-Process -FilePath 'powershell.exe' -Verb RunAs -ArgumentList @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $PSCommandPath)
    exit 0
}
Log "ADMIN ok"

$jobs = @(
    @{ Bak = 'C:\Windows\System32\weasel.dll.old-20261021163536'; Dst = 'C:\Windows\System32\weasel.dll'; Name = 'System32' },
    @{ Bak = 'C:\Windows\SysWOW64\weasel.dll.old-20261021163536'; Dst = 'C:\Windows\SysWOW64\weasel.dll'; Name = 'SysWOW64' },
    @{ Bak = 'D:\Rime\weasel-0.17.4\weaselx64.dll.old-20261021163105'; Dst = 'D:\Rime\weasel-0.17.4\weaselx64.dll'; Name = 'inst-x64' },
    @{ Bak = 'D:\Rime\weasel-0.17.4\weasel.dll.old-20261021163105';    Dst = 'D:\Rime\weasel-0.17.4\weasel.dll';    Name = 'inst-x86' }
)
$failed = 0
foreach ($j in $jobs) {
    try {
        if (-not (Test-Path $j.Bak)) { throw "missing backup $($j.Bak)" }
        if (Test-Path $j.Dst) { Move-Item -LiteralPath $j.Dst -Destination "$($j.Dst).broken-$(Get-Date -Format 'HHmmss')" -Force }
        Copy-Item -LiteralPath $j.Bak -Destination $j.Dst -Force
        $h = (Get-FileHash $j.Dst -Algorithm SHA256).Hash.Substring(0,16)
        Log "$($j.Name) restored $h"
    } catch {
        Log "$($j.Name) FAIL $($_.Exception.Message)"
        $failed++
    }
}

Get-Process WeaselServer -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2
Start-Process -FilePath 'D:\Rime\weasel-0.17.4\WeaselServer.exe' -WorkingDirectory 'D:\Rime\weasel-0.17.4' -WindowStyle Hidden
Log "WeaselServer restarted"

# 重启资源管理器，让它重新加载回滚后的 DLL（窗口会短暂消失后自动恢复）
Get-Process explorer -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Log "explorer restarted"

if ($failed -eq 0) { Log "DONE ok" } else { Log "DONE failed=$failed" }
exit $failed
