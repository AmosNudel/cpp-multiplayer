# Builds release client into dist/ for sharing or installer packaging.
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$buildScript = Join-Path $PSScriptRoot "build.ps1"
$exeName = "game_client"

& $buildScript -Config Release -ClientOnly
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$dist = Join-Path $root "dist"
if (Test-Path $dist) { Remove-Item $dist -Recurse -Force }
New-Item -ItemType Directory -Path $dist | Out-Null

Copy-Item (Join-Path $root "build\game_client.exe") (Join-Path $dist "$exeName.exe")

Write-Host ""
Write-Host "Done! Ship this folder:"
Write-Host "  $dist"
Write-Host ""
Write-Host "Run: dist\$exeName.exe"
