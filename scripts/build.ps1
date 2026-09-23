# Builds Audio Watchdog with CMake + Ninja + MSVC and runs the unit tests.
# Usage:
#   powershell -ExecutionPolicy Bypass -File scripts\build.ps1 [-Debug] [-SkipTests]
param(
    [switch]$Debug,
    [switch]$SkipTests
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$buildType = if ($Debug) { "Debug" } else { "Release" }

# Locate a CMake (VS Build Tools bundles one).
$cmakeCandidates = @(
    "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
    "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
    "cmake.exe"
)
$cmake = $cmakeCandidates | Where-Object { Get-Command $_ -ErrorAction SilentlyContinue } | Select-Object -First 1
if (-not $cmake) { throw "CMake not found. Install Build Tools for Visual Studio 2022." }

# vcvars to make cl available for Ninja builds.
$vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path $vcvars)) {
    $vcvars = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
}
if (-not (Test-Path $vcvars)) { throw "vcvars64.bat not found." }

Push-Location $root
try {
    cmd /c "call `"$vcvars`" >nul 2>&1 && `"$cmake`" -G Ninja -S . -B build -DCMAKE_BUILD_TYPE=$buildType"
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed." }
    cmd /c "call `"$vcvars`" >nul 2>&1 && `"$cmake`" --build build"
    if ($LASTEXITCODE -ne 0) { throw "Build failed." }
    Write-Host "Built: build\bin\AudioWatchdog.exe"
    Write-Host "      build\bin\audiowatchdog_tests.exe"

    if (-not $SkipTests) {
        & "$root\build\bin\audiowatchdog_tests.exe"
        if ($LASTEXITCODE -ne 0) { throw "Tests failed." }
    }
} finally {
    Pop-Location
}