[CmdletBinding()]
param(
  [switch]$Elevated,
  [switch]$NoBackup
)

$ErrorActionPreference = 'Stop'

# Validate the entire release before changing registrations or stopping the
# running service. A copied DLL from another build must never be mixed in.
$manifestFile = Join-Path $PSScriptRoot 'release-manifest.json'
if (-not (Test-Path -LiteralPath $manifestFile)) { throw 'Missing release manifest.' }
$manifest = Get-Content -LiteralPath $manifestFile -Raw | ConvertFrom-Json
foreach ($entry in $manifest.files) {
  $artifact = Join-Path $PSScriptRoot $entry.path
  if (-not (Test-Path -LiteralPath $artifact) -or
      (Get-FileHash -LiteralPath $artifact -Algorithm SHA256).Hash -ne $entry.sha256) {
    throw "Release integrity check failed: $($entry.path)"
  }
}

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)) {
  $args = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
            "`"$PSCommandPath`"", '-Elevated')
  if ($NoBackup) { $args += '-NoBackup' }
  $process = Start-Process -FilePath "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" -Verb RunAs -Wait -PassThru -ArgumentList $args
  exit $process.ExitCode
}

$install = 'D:\Rime\weasel-0.17.4'
$backup = Join-Path $install ('backup-source-assets-' +
                              (Get-Date -Format 'yyyyMMdd-HHmmss'))
$system32 = [Environment]::SystemDirectory
$syswow64 = Join-Path $env:WINDIR 'SysWOW64'
$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
$warmStartName = 'WeaselServerWarmStart'
$warmStartValue = '"' + (Join-Path $install 'WeaselServer.exe') + '"'
$warmTaskName = 'ColorPWeaselWarmStart'
$files = @(
  @{ source = 'x64\WeaselServer.exe'; target = (Join-Path $install 'WeaselServer.exe'); backup = 'install-WeaselServer.exe'; label = 'WeaselServer.exe' },
  @{ source = 'x64\weaselx64.dll'; target = (Join-Path $install 'weaselx64.dll'); backup = 'install-weaselx64.dll'; label = 'installed 64-bit TSF DLL' },
  @{ source = 'x86\weasel.dll'; target = (Join-Path $install 'weasel.dll'); backup = 'install-weasel.dll'; label = 'installed 32-bit TSF DLL' },
  # Windows applications activate the machine-registered TSF text service from
  # these two conventional locations.  Updating only D:\Rime leaves Explorer,
  # Edge, and other existing desktop applications on the old client DLL.
  @{ source = 'x64\weaselx64.dll'; target = (Join-Path $system32 'weasel.dll'); backup = 'System32-weasel.dll'; label = 'System32 64-bit TSF DLL' },
  @{ source = 'x86\weasel.dll'; target = (Join-Path $syswow64 'weasel.dll'); backup = 'SysWOW64-weasel.dll'; label = 'SysWOW64 32-bit TSF DLL' }
)

# The TSF text-service DLLs stay mapped in applications such as Explorer and
# Codex even after WeaselServer exits. A rename scheduled at reboot is the
# supported Windows update path for those locked DLLs.
if (-not ('ColorPFileOps' -as [type])) {
  Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class ColorPFileOps {
  [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
  public static extern bool MoveFileEx(string existingFileName,
                                       string newFileName,
                                       int flags);
}
'@
}
$moveFileReplaceExisting = 0x1
$moveFileDelayUntilReboot = 0x4
$deferred = @()

# Older per-user installs register this CLSID under HKCU.  Windows gives that
# registration precedence over the machine-wide TSF registration, so a stale
# DLL there makes a single app render an older UI while other apps use the
# System32/SysWOW64 build.  The deployment owns the machine registration;
# remove the redundant per-user copies on every run.
$tsfClsid = '{A3F4CDED-B1E9-41EE-9CA6-7B4D0DE6CB0A}'
$userTsfKeys = @(
  "HKCU:\Software\Classes\CLSID\$tsfClsid",
  "HKCU:\Software\Classes\WOW6432Node\CLSID\$tsfClsid"
)
foreach ($key in $userTsfKeys) {
  Remove-Item -LiteralPath $key -Recurse -Force -ErrorAction SilentlyContinue
}

foreach ($file in $files) {
  $source = Join-Path $PSScriptRoot $file.source
  if (-not (Test-Path -LiteralPath $source)) {
    throw "Missing package file: $($file.source)"
  }
  if (-not (Test-Path -LiteralPath $file.target)) {
    throw "Installed target is missing: $($file.target)"
  }
}

if (-not $NoBackup) {
  New-Item -ItemType Directory -Path $backup | Out-Null
}
Get-Process WeaselServer -ErrorAction SilentlyContinue | Stop-Process -Force

# Ship the matching Lua processor together with the native TSF components.
$luaTarget = 'D:\RimeUser\lua'
New-Item -ItemType Directory -Path $luaTarget -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'lua\keypad_input.lua') -Destination (Join-Path $luaTarget 'keypad_input.lua') -Force

# Keep the Rime engine hot from the beginning of the interactive logon.  A
# scheduled logon task runs before the delayed Startup-app Run key phase.  It
# replaces the old Run value rather than running beside it: two bare
# WeaselServer launches see each other and deliberately restart the service.
$warmTaskRegistered = $false
try {
  $warmUser = [Security.Principal.WindowsIdentity]::GetCurrent().Name
  $warmAction = New-ScheduledTaskAction -Execute (Join-Path $install 'WeaselServer.exe') -WorkingDirectory $install
  $warmTrigger = New-ScheduledTaskTrigger -AtLogOn -User $warmUser
  $warmPrincipal = New-ScheduledTaskPrincipal -UserId $warmUser -LogonType Interactive -RunLevel Limited
  $warmSettings = New-ScheduledTaskSettingsSet -StartWhenAvailable -MultipleInstances IgnoreNew -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
  Register-ScheduledTask -TaskName $warmTaskName -Action $warmAction -Trigger $warmTrigger -Principal $warmPrincipal -Settings $warmSettings -Force | Out-Null
  $warmTaskRegistered = $true
} catch {
  Write-Warning "Could not register $warmTaskName; retaining the Run-key fallback. $($_.Exception.Message)"
}

New-Item -Path $runKey -Force | Out-Null
if ($warmTaskRegistered) {
  Remove-ItemProperty -Path $runKey -Name $warmStartName -ErrorAction SilentlyContinue
} else {
  New-ItemProperty -Path $runKey -Name $warmStartName -Value $warmStartValue -PropertyType String -Force | Out-Null
}

foreach ($file in $files) {
  $destination = $file.target
  if (-not $NoBackup) {
    Copy-Item -LiteralPath $destination -Destination (Join-Path $backup $file.backup)
  }
  $source = Join-Path $PSScriptRoot $file.source
  try {
    Copy-Item -LiteralPath $source -Destination $destination -Force
    # A previous run may have queued this target for reboot. If today's copy
    # succeeds, Windows will still execute that older queued move. Refresh its
    # source too so reboot cannot silently downgrade the just-installed file.
    $pending = "$destination.color-p-pending"
    if (Test-Path -LiteralPath $pending) {
      Copy-Item -LiteralPath $source -Destination $pending -Force
      $deferred += $file.label
    }
  } catch [System.IO.IOException] {
    $pending = "$destination.color-p-pending"
    Copy-Item -LiteralPath $source -Destination $pending -Force
    $flags = $moveFileReplaceExisting -bor $moveFileDelayUntilReboot
    if (-not [ColorPFileOps]::MoveFileEx($pending, $destination, $flags)) {
      $code = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
      throw "Cannot schedule replacement for $($file.label); Win32 error $code"
    }
    $deferred += $file.label
  }
}

Start-Process -FilePath (Join-Path $install 'WeaselServer.exe') -WorkingDirectory $install -WindowStyle Hidden
if ($NoBackup) {
  Write-Host 'Installed all Rime and machine-registered TSF components without creating a backup.'
} else {
  Write-Host "Installed all Rime and machine-registered TSF components. Backup: $backup"
}
if ($deferred.Count) {
  Write-Host ("Locked files scheduled for replacement at restart: " + ($deferred -join ', ')) -ForegroundColor Yellow
}
Write-Host 'Restart Windows before testing. Explorer, browsers, and other desktop applications keep TSF DLLs mapped until restart.' -ForegroundColor Yellow

[pscustomobject]@{
  completedAt = (Get-Date).ToString('o')
  release = $manifest.release
  restartRequired = ($deferred.Count -gt 0)
  deferred = $deferred
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'deployment-status.json') -Encoding UTF8
