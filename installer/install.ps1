# Audio Watchdog installer.
# - Installs AudioWatchdog.exe under Program Files.
# - Creates the logs folder C:\ProgramData\Audio Watchdog\logs.
# - Writes a default config.ini, registers auto-start at logon and a Start Menu shortcut.
# Can be run directly or through the IExpress AudioWatchdog_Setup.exe package.
param()

$ErrorActionPreference = "Stop"

# Self-elevate with UAC if needed.
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = New-Object Security.Principal.WindowsPrincipal($identity)
$isAdmin = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Start-Process powershell -Verb RunAs -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`""
    exit
}

$src          = $PSScriptRoot
$installDir   = Join-Path $env:ProgramFiles "Audio Watchdog"
$exe          = Join-Path $installDir "AudioWatchdog.exe"
$appDataDir   = "C:\ProgramData\Audio Watchdog"
$logsDir      = Join-Path $appDataDir "logs"
$configPath   = Join-Path $appDataDir "config.ini"
$setupLog     = Join-Path $logsDir "setup.log"
$runKey       = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run"

try {
    # Stop any running instance so the binary can be replaced.
    Stop-Process -Name AudioWatchdog -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 500

    # 1. Binary.
    New-Item -ItemType Directory -Force -Path $installDir | Out-Null
    $srcExe = Join-Path $src "AudioWatchdog.exe"
    if (-not (Test-Path $srcExe)) { throw "AudioWatchdog.exe not found next to this installer." }
    Copy-Item $srcExe $exe -Force

    # 2. App data + logs folders.
    New-Item -ItemType Directory -Force -Path $appDataDir | Out-Null
    New-Item -ItemType Directory -Force -Path $logsDir | Out-Null

    # 3. Default config (only if missing).
    if (-not (Test-Path $configPath)) {
        @"
[Monitor]
Playback=true
Capture=true
CheckIntervalSeconds=60

[Logging]
Enable=true
Level=INFO

[Behavior]
Enforce=true
ExclusiveModeDisabled=false
"@ | Set-Content -Path $configPath -Encoding ASCII
    }

    # 4. ACLs so any user can read/write/rotate the logs.
    icacls $appDataDir /grant "*S-1-5-32-545:(OI)(CI)M" /T /Q | Out-Null
    icacls $appDataDir /grant "$($identity.Name):(OI)(CI)F" /T /Q | Out-Null

    # 5. Auto-start at logon for the current user.
    New-Item -Path $runKey -Force | Out-Null
    Set-ItemProperty -Path $runKey -Name "Audio Watchdog" -Value "`"$exe`""

    # 6. Start Menu shortcut.
    $startMenu = [Environment]::GetFolderPath("Programs")
    $lnk = Join-Path $startMenu "Audio Watchdog.lnk"
    $ws = New-Object -ComObject WScript.Shell
    $sc = $ws.CreateShortcut($lnk)
    $sc.TargetPath = $exe
    $sc.WorkingDirectory = $installDir
    $sc.IconLocation = "$exe,0"
    $sc.Description = "Audio Watchdog - keeps Windows audio exclusive mode disabled"
    $sc.Save()

    # 7. Start it now (runs in the notification area).
    Start-Process -FilePath $exe

    # 8. Record the install.
    $logText = @"
Audio Watchdog installed: $(Get-Date -Format "yyyy-MM-dd HH:mm:ss")
Binary:    $exe
Logs:      $logsDir
Config:    $configPath
Auto-start: enabled (HKCU Run)
"@
    Set-Content -Path $setupLog -Value $logText -Encoding UTF8

    try {
        Add-Type -AssemblyName System.Windows.Forms
        [System.Windows.Forms.MessageBox]::Show(
            "Audio Watchdog installed successfully.`n`nLogs are written to:`n$logsDir`n`nIt is now running in the notification area.",
            "Audio Watchdog", "OK", "Information") | Out-Null
    } catch { /* no interactive desktop; skip the dialog */ }
}
catch {
    $err = "Audio Watchdog installation FAILED: $($_.Exception.Message)"
    Write-Error $err
    try { Add-Content -Path $setupLog -Value $err } catch {}
    throw
}