# Builds the web (Emscripten) client with WebSocket networking.
param(
    [string]$DeployPath = "",
    [string]$WsHost = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $root "build-web"
$w64 = "C:/raylib/w64devkit/bin"
$make = Join-Path $w64 "mingw32-make.exe"
$raylibSrc = "C:/raylib/raylib/src"
$loader = Join-Path $PSScriptRoot "load_production_config.ps1"

if (-not (Test-Path $make)) {
    Write-Host "MinGW make not found at $make"
    Write-Host "Install raylib w64devkit or update the path in tools/build_web.ps1"
    exit 1
}

if ($WsHost -eq "") {
    $config = & $loader -Required
    $WsHost = $config.WsHost
}

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

function Resolve-EmscriptenRoot {
    param([string]$EmsdkEnvPath)

    if ($env:EMSCRIPTEN -and (Test-Path $env:EMSCRIPTEN)) {
        return $env:EMSCRIPTEN
    }

    $emsdkRoot = $env:EMSDK
    if (-not $emsdkRoot -and $EmsdkEnvPath) {
        $emsdkRoot = Split-Path -Parent $EmsdkEnvPath
    }
    if (-not $emsdkRoot) {
        $emsdkRoot = "C:\raylib\emsdk"
    }

    $emscriptenRoot = Join-Path $emsdkRoot "upstream\emscripten"
    if (Test-Path $emscriptenRoot) {
        return $emscriptenRoot
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
$env:EMSDK_QUIET = "1"
. $emsdkEnv

$env:PATH = "$w64;$env:PATH"

if (-not (Get-Command emcc -ErrorAction SilentlyContinue)) {
    Write-Host "emcc not found after emsdk activation."
    exit 1
}

# Build raylib for web (Emscripten) — desktop libraylib.a cannot link to WASM.
$raylibWeb = Join-Path $raylibSrc "libraylib.web.a"
if (-not (Test-Path $raylibWeb)) {
    Write-Host "Building raylib for web (first time only)..."
    Push-Location $raylibSrc
    & $make PLATFORM=PLATFORM_WEB "MAKE=$make"
    if ($LASTEXITCODE -ne 0) { Pop-Location; exit $LASTEXITCODE }
    Pop-Location
    if (-not (Test-Path $raylibWeb)) {
        Write-Host "Expected raylib web library not found: $raylibWeb"
        exit 1
    }
}

if (-not (Get-Command emcmake -ErrorAction SilentlyContinue)) {
    Write-Host "emcmake not found after emsdk activation."
    exit 1
}

$emscriptenRoot = Resolve-EmscriptenRoot -EmsdkEnvPath $emsdkEnv
if (-not $emscriptenRoot) {
    Write-Host "Could not locate Emscripten root. Set EMSCRIPTEN or install emsdk to C:\raylib\emsdk."
    exit 1
}

$toolchain = Join-Path $emscriptenRoot "cmake\Modules\Platform\Emscripten.cmake"
if (-not (Test-Path $toolchain)) {
    Write-Host "Emscripten CMake toolchain not found at: $toolchain"
    exit 1
}

if (Test-Path $buildDir) {
    Write-Host "Cleaning previous web build cache..."
    try {
        Remove-Item $buildDir -Recurse -Force -ErrorAction Stop
    } catch {
        Write-Host ""
        Write-Host "Could not remove $buildDir (folder is in use)."
        Write-Host "Rebuilding in place instead. To force a clean rebuild:"
        Write-Host "  - Stop any 'python -m http.server' running from build-web"
        Write-Host "  - Close browser tabs using game_client.html"
        Write-Host "  - Close terminals whose cwd is build-web"
        Write-Host ""
    }
}

Write-Host "Configuring web build..."
$cmakeConfigureArgs = @(
    "cmake",
    "-S", $root,
    "-B", $buildDir,
    "-G", "MinGW Makefiles",
    "-DCMAKE_MAKE_PROGRAM=$make",
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain",
    "-DBUILD_SERVER=OFF",
    "-DBUILD_CLIENT=ON",
    "-DRAYLIB_PATH=C:/raylib/raylib",
    "-DCMAKE_BUILD_TYPE=Release"
)
if ($WsHost -ne "") {
    $cmakeConfigureArgs += "-DWS_HOST_DEFAULT=$WsHost"
}
& emcmake @cmakeConfigureArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Compiling web build..."
& emmake cmake --build $buildDir
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
Write-Host "Production WebSocket URL (auto when hosted on HTTPS):"
Write-Host "  wss://$WsHost"
Write-Host ""
Write-Host "Override with WS_HOST / WS_TLS env vars if needed."
