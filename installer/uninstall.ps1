# Audio Watchdog uninstaller.
# Removes the binary, Start Menu shortcut, auto-start entry and (optionally)
# the ProgramData logs/config with -RemoveData.
param([switch]$RemoveData)

$ErrorActionPreference = "Stop"

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = New-Object Security.Principal.WindowsPrincipal($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Start-Process powershell -Verb RunAs -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" -RemoveData:$($RemoveData.IsPresent)"
    exit
}

$installDir = Join-Path $env:ProgramFiles "Audio Watchdog"
$appDataDir  = "C:\ProgramData\Audio Watchdog"
$runKey      = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run"
$startMenu   = [Environment]::GetFolderPath("Programs")
$lnk         = Join-Path $startMenu "Audio Watchdog.lnk"

try {
    # Stop the running instance.
    Stop-Process -Name AudioWatchdog -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 500

    Remove-ItemProperty -Path $runKey -Name "Audio Watchdog" -ErrorAction SilentlyContinue
    Remove-Item $lnk -Force -ErrorAction SilentlyContinue
    Remove-Item $installDir -Recurse -Force -ErrorAction SilentlyContinue

    if ($RemoveData) {
        Remove-Item $appDataDir -Recurse -Force -ErrorAction SilentlyContinue
        Write-Host "Audio Watchdog uninstalled (data removed)."
    } else {
        Remove-Item (Join-Path $appDataDir "logs\setup.log") -Force -ErrorAction SilentlyContinue
        Write-Host "Audio Watchdog uninstalled."
        Write-Host "Logs and config were kept under: $appDataDir (re-run with -RemoveData to delete them)."
    }
}
catch {
    Write-Error "Uninstall FAILED: $($_.Exception.Message)"
    throw
}