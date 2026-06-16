# Builds release client into dist/ for sharing or installer packaging.
# Reads server host/port from config/production.env.ps1 (gitignored).
param(
    [string]$ServerHost = "",
    [string]$ServerPort = ""
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$buildScript = Join-Path $PSScriptRoot "build.ps1"
$loader = Join-Path $PSScriptRoot "load_production_config.ps1"
$exeName = "game_client"

$config = & $loader -Required
if ($ServerHost -eq "") { $ServerHost = $config.ServerHost }
if ($ServerPort -eq "") { $ServerPort = $config.ServerPort }

& $buildScript -Config Release -ClientOnly -ServerHost $ServerHost -ServerPort $ServerPort
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$dist = Join-Path $root "dist"
if (Test-Path $dist) { Remove-Item $dist -Recurse -Force }
New-Item -ItemType Directory -Path $dist | Out-Null

Copy-Item (Join-Path $root "build\game_client.exe") (Join-Path $dist "$exeName.exe")

Write-Host ""
Write-Host "Done! Ship this folder:"
Write-Host "  $dist"
Write-Host ""
Write-Host "Server baked in: ${ServerHost}:${ServerPort}"
Write-Host "Players can double-click $exeName.exe - no env vars needed."
