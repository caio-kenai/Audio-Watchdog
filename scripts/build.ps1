# Builds Audio Watchdog (CMake + Ninja + MSVC), runs the unit tests and copies
# the installer to dist\.
# Usage:
#   powershell -ExecutionPolicy Bypass -File scripts\build.ps1 [-Debug] [-SkipTests]
param(
    [switch]$Debug,
    [switch]$SkipTests
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$buildType = if ($Debug) { "Debug" } else { "Release" }
$buildDir = Join-Path $root "build"

# Visual Studio 2022 (Build Tools or Community) provides cl, rc, cmake and ninja.
$vsRoots = @(
    "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools",
    "C:\Program Files\Microsoft Visual Studio\2022\Community",
    "C:\Program Files\Microsoft Visual Studio\2022\Professional",
    "C:\Program Files\Microsoft Visual Studio\2022\Enterprise"
)
$vs = $vsRoots | Where-Object { Test-Path "$_\VC\Auxiliary\Build\vcvars64.bat" } | Select-Object -First 1
if (-not $vs) { throw "Visual Studio 2022 (C++ workload) not found." }
$vcvars = "$vs\VC\Auxiliary\Build\vcvars64.bat"
$tools = "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake"
$env:PATH = "$tools\CMake\bin;$tools\Ninja;$env:PATH"

function Invoke-VS([string]$cmd) {
    cmd /c "call `"$vcvars`" >nul 2>&1 && $cmd"
    if ($LASTEXITCODE -ne 0) { throw "Command failed: $cmd" }
}

Push-Location $root
try {
    Invoke-VS "cmake -G Ninja -S . -B `"$buildDir`" -DCMAKE_BUILD_TYPE=$buildType"
    Invoke-VS "cmake --build `"$buildDir`""

    if (-not $SkipTests) {
        & "$buildDir\bin\audiowatchdog_tests.exe"
        if ($LASTEXITCODE -ne 0) { throw "Tests failed." }
    }

    $version = (Select-String -Path "$buildDir\generated\version.h" -Pattern 'AWW_VERSION_STR\s+"([^"]+)"').Matches[0].Groups[1].Value
    $dist = Join-Path $root "dist"
    New-Item -ItemType Directory -Force -Path $dist | Out-Null
    Copy-Item "$buildDir\bin\AudioWatchdog-Setup.exe" "$dist\AudioWatchdog-Setup.exe" -Force
    $hash = (Get-FileHash "$dist\AudioWatchdog-Setup.exe" -Algorithm SHA256).Hash
    "$hash  AudioWatchdog-Setup.exe" | Set-Content "$dist\AudioWatchdog-Setup.exe.sha256" -Encoding ascii

    Write-Host ""
    Write-Host "Audio Watchdog $version ($buildType)"
    Write-Host "  build\bin\AudioWatchdog.exe        app + service + CLI"
    Write-Host "  dist\AudioWatchdog-Setup.exe       installer (SHA-256 $hash)"
} finally {
    Pop-Location
}
