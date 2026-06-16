# Builds the desktop client and headless server with CMake.
param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Release",
    [switch]$ServerOnly,
    [switch]$ClientOnly
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $root "build"
$w64 = "C:/raylib/w64devkit/bin"
$gpp = Join-Path $w64 "g++.exe"
$make = Join-Path $w64 "mingw32-make.exe"

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Host ""
    Write-Host "CMake is required but was not found on PATH."
    Write-Host "Install from: https://cmake.org/download/"
    Write-Host "During install, check 'Add CMake to the system PATH'."
    exit 1
}

if (-not (Test-Path $gpp)) {
    Write-Host "MinGW g++ not found at $gpp"
    Write-Host "Install raylib w64devkit or update the path in tools/build.ps1"
    exit 1
}

$buildServer = if ($ClientOnly) { "OFF" } else { "ON" }
$buildClient = if ($ServerOnly) { "OFF" } else { "ON" }

$env:PATH = "$w64;$env:PATH"

$cmakeArgs = @(
    "-S", $root,
    "-B", $buildDir,
    "-G", "MinGW Makefiles",
    "-DCMAKE_BUILD_TYPE=$Config",
    "-DCMAKE_CXX_COMPILER=$gpp",
    "-DCMAKE_C_COMPILER=$(Join-Path $w64 'gcc.exe')",
    "-DCMAKE_MAKE_PROGRAM=$make",
    "-DRAYLIB_PATH=C:/raylib/raylib",
    "-DBUILD_SERVER=$buildServer",
    "-DBUILD_CLIENT=$buildClient"
)

Write-Host "Configuring CMake (MinGW)..."
& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Building ($Config)..."
& cmake --build $buildDir
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host ""
Write-Host "Build complete."
if ($buildClient -eq "ON") {
    Write-Host "  Client: $buildDir\game_client.exe"
}
if ($buildServer -eq "ON") {
    Write-Host "  Server: $buildDir\game_server.exe"
}
