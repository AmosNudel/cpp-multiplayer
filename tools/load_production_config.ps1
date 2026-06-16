# Loads config/production.env.ps1 for release and web builds.
param(
    [switch]$Required
)

$ErrorActionPreference = "Stop"
$configDir = Join-Path (Split-Path -Parent $PSScriptRoot) "config"
$configPath = Join-Path $configDir "production.env.ps1"
$examplePath = Join-Path $configDir "production.env.ps1.example"

if (-not (Test-Path $configPath)) {
    if ($Required) {
        Write-Host ""
        Write-Host "Missing config: $configPath"
        Write-Host ""
        Write-Host "One-time setup:"
        Write-Host "  copy config\production.env.ps1.example config\production.env.ps1"
        Write-Host "  Then edit production.env.ps1 with your Railway TCP proxy and domain."
        exit 1
    }
    return $null
}

. $configPath

return @{
    ServerHost = $ProductionServerHost
    ServerPort = $ProductionServerPort
    WsHost       = $ProductionWsHost
}
