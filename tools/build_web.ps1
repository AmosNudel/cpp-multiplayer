# Builds the web (Emscripten) client with WebSocket networking.
param(
    [string]$DeployPath = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $root "build-web"
$exeName = "MultiplayerGame"

function Find-EmsdkEnv {
    $candidates = @(
        "C:\raylib\emsdk\emsdk_env.ps1",
        "C:\emsdk\emsdk_env.ps1",
        (Join-Path $env:USERPROFILE "emsdk\emsdk_env.ps1")
    )
    foreach ($path in $candidates) {
        if (Test-Path $path) { return $path }
    }
    return $null
}

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Host "CMake is required. Install from https://cmake.org/download/"
    exit 1
}

$emsdkEnv = Find-EmsdkEnv
if (-not $emsdkEnv) {
    Write-Host ""
    Write-Host "Emscripten (emsdk) is not installed."
    Write-Host ""
    Write-Host "One-time setup:"
    Write-Host "  git clone https://github.com/emscripten-core/emsdk.git C:\raylib\emsdk"
    Write-Host "  cd C:\raylib\emsdk"
    Write-Host "  .\emsdk install latest"
    Write-Host "  .\emsdk activate latest"
    exit 1
}

Write-Host "Activating Emscripten..."
. $emsdkEnv

if (-not (Get-Command emcc -ErrorAction SilentlyContinue)) {
    Write-Host "emcc not found after emsdk activation."
    exit 1
}

$toolchain = Join-Path $env:EMSCRIPTEN "cmake/Modules/Platform/Emscripten.cmake"
if (-not (Test-Path $toolchain)) {
    Write-Host "Emscripten CMake toolchain not found at: $toolchain"
    exit 1
}

Write-Host "Configuring web build..."
& cmake -S $root -B $buildDir `
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" `
    -DBUILD_SERVER=OFF `
    -DBUILD_CLIENT=ON `
    -DRAYLIB_PATH=C:/raylib/raylib `
    -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# Emscripten-specific link flags for raylib web + websocket client.
$linkFlags = @(
    "-sUSE_GLFW=3"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sASYNCIFY"
    "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap']"
    "--shell-file=C:/raylib/raylib/src/shell.html"
)
$flagsJoined = ($linkFlags -join " ")
& cmake -S $root -B $buildDir `
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" `
    -DBUILD_SERVER=OFF `
    -DBUILD_CLIENT=ON `
    -DRAYLIB_PATH=C:/raylib/raylib `
    -DCMAKE_BUILD_TYPE=Release `
    "-DCMAKE_EXE_LINKER_FLAGS=$flagsJoined"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Compiling web build..."
& cmake --build $buildDir --config Release
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$webOut = Join-Path $buildDir "game_client.html"
if (-not (Test-Path $webOut)) {
    Write-Host "Expected output not found: $webOut"
    exit 1
}

if ($DeployPath -ne "") {
    if (Test-Path $DeployPath) { Remove-Item $DeployPath -Recurse -Force }
    New-Item -ItemType Directory -Path $DeployPath | Out-Null
    Copy-Item (Join-Path $buildDir "game_client.*") $DeployPath
    Write-Host "Deployed to $DeployPath"
}

Write-Host ""
Write-Host "Web build ready!"
Write-Host "  $webOut"
Write-Host ""
Write-Host "Set WS_HOST / WS_PORT in the shell URL or environment before loading."
