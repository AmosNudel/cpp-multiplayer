param(
    [switch]$Clean,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

$workspaceRoot = Split-Path -Parent $PSScriptRoot

$roots = @(
    "assets/player_sprites/Sprites/with_outline",
    "assets/player_sprites/Sprites/without_outline",
    "assets/enemy"
)

function Get-FlippedPath {
    param([string]$Path)

    $dir = Split-Path -Parent $Path
    $name = [System.IO.Path]::GetFileNameWithoutExtension($Path)
    $ext = [System.IO.Path]::GetExtension($Path)
    return (Join-Path $dir ($name + "_flipped" + $ext))
}

function Test-ImageMagickAvailable {
    return $null -ne (Get-Command magick -ErrorAction SilentlyContinue)
}

function Test-SystemDrawingAvailable {
    try {
        Add-Type -AssemblyName System.Drawing -ErrorAction Stop
        return $true
    } catch {
        return $false
    }
}

function New-FlippedImage {
    param(
        [string]$Source,
        [string]$Target,
        [bool]$UseImageMagick
    )

    if ($UseImageMagick) {
        & magick "$Source" -flop "$Target"
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to flip image with ImageMagick: $Source"
        }
        return
    }

    $image = [System.Drawing.Image]::FromFile($Source)
    try {
        $image.RotateFlip([System.Drawing.RotateFlipType]::RotateNoneFlipX)
        $image.Save($Target, $image.RawFormat)
    } finally {
        $image.Dispose()
    }
}

$useImageMagick = Test-ImageMagickAvailable
$useSystemDrawing = Test-SystemDrawingAvailable

if (-not $useImageMagick -and -not $useSystemDrawing) {
    throw "No image backend found. Install ImageMagick (magick) or enable .NET System.Drawing."
}

if ($useImageMagick) {
    Write-Host "Using ImageMagick backend."
} else {
    Write-Host "Using System.Drawing backend (ImageMagick not found)."
}

[int]$created = 0
[int]$deleted = 0
[int]$skipped = 0

foreach ($root in $roots) {
    $absoluteRoot = Join-Path $workspaceRoot $root
    if (-not (Test-Path $absoluteRoot)) {
        Write-Host "skip missing root: $root"
        continue
    }

    $pngFiles = Get-ChildItem -Path $absoluteRoot -Recurse -File -Filter *.png
    foreach ($file in $pngFiles) {
        if ($file.BaseName.EndsWith("_flipped")) {
            continue
        }

        $source = $file.FullName
        $target = Get-FlippedPath -Path $source

        if ($Clean) {
            if (Test-Path $target) {
                if ($DryRun) {
                    Write-Host "[dry-run] delete $target"
                } else {
                    Remove-Item -Path $target -Force
                    Write-Host "deleted $target"
                }
                $deleted++
            }
            continue
        }

        if (Test-Path $target) {
            $skipped++
            continue
        }

        if ($DryRun) {
            Write-Host "[dry-run] create $target"
            $created++
            continue
        }

        New-FlippedImage -Source $source -Target $target -UseImageMagick:$useImageMagick

        Write-Host "created $target"
        $created++
    }
}

if ($Clean) {
    Write-Host "Done. Deleted $deleted flipped sprite sheets."
} else {
    Write-Host "Done. Created $created flipped sprite sheets. Skipped $skipped existing files."
}
