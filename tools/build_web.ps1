# Builds the web (Emscripten) client with WebSocket networking.
param(
    [string]$DeployPath = "",
    [string]$WsHost = "",
    [int]$WsPort = 0
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
    if ($WsPort -eq 0 -and $config.WsPort) {
        $WsPort = [int]$config.WsPort
    }
}
if ($WsPort -eq 0) {
    $WsPort = 8080
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
$cmakeConfigureArgs += "-DWS_PORT_DEFAULT=$WsPort"
& emcmake @cmakeConfigureArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Compiling web build..."
& emmake cmake --build $buildDir
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$webOut = Join-Path $buildDir "game_client.js"
if (-not (Test-Path $webOut)) {
    Write-Host "Expected output not found: $webOut"
    exit 1
}

function Get-FileSha256Hex {
    param([string]$Path)
    return (Get-FileHash -Path $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Publish-WebBundle {
    param([string]$Destination)

    $indexSrc = Join-Path $root "web\index.html"
    if (-not (Test-Path $indexSrc)) {
        Write-Host "Missing web shell: $indexSrc"
        exit 1
    }

    $jsSrc = Join-Path $buildDir "game_client.js"
    $wasmSrc = Join-Path $buildDir "game_client.wasm"
    $dataSrc = Join-Path $buildDir "game_client.data"
    if (-not (Test-Path $jsSrc) -or -not (Test-Path $wasmSrc)) {
        Write-Host "Missing web build artifacts in $buildDir"
        exit 1
    }

    $jsHash = Get-FileSha256Hex $jsSrc
    $wasmHash = Get-FileSha256Hex $wasmSrc
    $buildId = $jsHash.Substring(0, 12)
    $builtAt = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ")

    $indexHtml = Get-Content $indexSrc -Raw
    $indexHtml = $indexHtml -replace 'src="game_client\.js(?:\?v=[^"]*)?"', "src=`"game_client.js?v=$buildId`""

    if (Test-Path $Destination) {
        try {
            Remove-Item $Destination -Recurse -Force -ErrorAction Stop
        } catch {
            Write-Host "Could not remove $Destination (in use). Updating files in place."
        }
    }
    if (-not (Test-Path $Destination)) {
        New-Item -ItemType Directory -Path $Destination | Out-Null
    }

    Set-Content -Path (Join-Path $Destination "index.html") -Value $indexHtml -NoNewline
    Copy-Item $jsSrc (Join-Path $Destination "game_client.js") -Force
    Copy-Item $wasmSrc (Join-Path $Destination "game_client.wasm") -Force

    $manifest = @(
        "built_at=$builtAt"
        "build_id=$buildId"
        "game_client.js=$jsHash"
        "game_client.wasm=$wasmHash"
    )
    if (Test-Path $dataSrc) {
        Copy-Item $dataSrc (Join-Path $Destination "game_client.data") -Force
        $manifest += "game_client.data=$(Get-FileSha256Hex $dataSrc)"
    } else {
        Write-Host "Warning: game_client.data not found; assets may be missing at runtime."
    }
    Set-Content -Path (Join-Path $Destination "BUILD_INFO.txt") -Value ($manifest -join "`n")

    Write-Host "Bundle build_id=$buildId"
    Write-Host "  js   $jsHash"
    Write-Host "  wasm $wasmHash"
}

$webDist = Join-Path $root "web-dist"
Publish-WebBundle -Destination $webDist

if ($DeployPath -ne "") {
    Publish-WebBundle -Destination $DeployPath
    Write-Host "Deployed to $DeployPath"
}

Write-Host ""
Write-Host "Web build ready!"
Write-Host "  $webDist"
Write-Host "  index.html, game_client.js, game_client.wasm, game_client.data, BUILD_INFO.txt"
Write-Host ""
Write-Host "Deploy ALL files together into your Next.js public folder."
Write-Host "Use an iframe pointing at index.html; do not import game_client.js in React."
Write-Host ""
Write-Host "Production WebSocket URL (when built with production config):"
Write-Host "  wss://${WsHost}:${WsPort}"
Write-Host ""
Write-Host "Override with WS_HOST / WS_TLS env vars if needed."
