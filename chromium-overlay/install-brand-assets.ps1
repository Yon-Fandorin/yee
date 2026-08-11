[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string] $ChromiumSrc,

    [switch] $CheckOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw 'The PowerShell brand asset installer is supported on Windows only.'
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$yeeRoot = Split-Path -Parent $scriptDir
$chromiumPath = [System.IO.Path]::GetFullPath($ChromiumSrc)
$logoSource = Join-Path $yeeRoot 'assets\brand\yee-logo-v8c-dino-nubs.png'
$themeRoot = Join-Path $chromiumPath 'chrome\app\theme\chromium'
$brandingFile = Join-Path $themeRoot 'BRANDING'
$windowsIcon = Join-Path $themeRoot 'win\chromium.ico'

if (-not [System.IO.Path]::IsPathRooted($ChromiumSrc)) {
    throw "Chromium src path must be absolute: $ChromiumSrc"
}
if (-not (Test-Path -LiteralPath $brandingFile -PathType Leaf)) {
    throw "Not a Chromium src checkout: $chromiumPath"
}
if (-not (Test-Path -LiteralPath $logoSource -PathType Leaf)) {
    throw "Yee logo source is missing: $logoSource"
}
if (-not (Test-Path -LiteralPath (Split-Path -Parent $windowsIcon) -PathType Container)) {
    throw "Chromium Windows theme directory is missing: $(Split-Path -Parent $windowsIcon)"
}

try {
    Add-Type -AssemblyName System.Drawing
} catch {
    throw "System.Drawing is required to generate Windows brand assets: $($_.Exception.Message)"
}

function New-YeeMasterBitmap {
    param([Parameter(Mandatory = $true)][string] $SourcePath)

    $source = [System.Drawing.Image]::FromFile($SourcePath)
    try {
        $cropSize = [Math]::Min(820, [Math]::Min($source.Width, $source.Height))
        $sourceX = [Math]::Floor(($source.Width - $cropSize) / 2)
        $sourceY = [Math]::Floor(($source.Height - $cropSize) / 2)
        $master = New-Object System.Drawing.Bitmap 1024, 1024, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $graphics = [System.Drawing.Graphics]::FromImage($master)
        try {
            $graphics.Clear([System.Drawing.Color]::Transparent)
            $graphics.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
            $graphics.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
            $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
            $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
            $sourceRectangle = New-Object System.Drawing.Rectangle $sourceX, $sourceY, $cropSize, $cropSize
            $destinationRectangle = New-Object System.Drawing.Rectangle 0, 0, 1024, 1024
            $graphics.DrawImage($source, $destinationRectangle, $sourceRectangle, [System.Drawing.GraphicsUnit]::Pixel)
        } finally {
            $graphics.Dispose()
        }
        return $master
    } finally {
        $source.Dispose()
    }
}

function New-YeePngBytes {
    param(
        [Parameter(Mandatory = $true)][System.Drawing.Image] $Master,
        [Parameter(Mandatory = $true)][int] $Size
    )

    $bitmap = New-Object System.Drawing.Bitmap $Size, $Size, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.Clear([System.Drawing.Color]::Transparent)
        $graphics.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
        $graphics.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
        $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
        $graphics.DrawImage($Master, 0, 0, $Size, $Size)

        $stream = New-Object System.IO.MemoryStream
        try {
            $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
            # Keep PowerShell's pipeline from enumerating the byte array. ICO
            # assembly needs each rendered PNG to remain one byte[] value.
            return ,$stream.ToArray()
        } finally {
            $stream.Dispose()
        }
    } finally {
        $graphics.Dispose()
        $bitmap.Dispose()
    }
}

function Write-YeePng {
    param(
        [Parameter(Mandatory = $true)][System.Drawing.Image] $Master,
        [Parameter(Mandatory = $true)][int] $Size,
        [Parameter(Mandatory = $true)][string] $Destination
    )

    $bytes = New-YeePngBytes -Master $Master -Size $Size
    [System.IO.File]::WriteAllBytes($Destination, $bytes)
}

function Write-YeeIcon {
    param(
        [Parameter(Mandatory = $true)][System.Drawing.Image] $Master,
        [Parameter(Mandatory = $true)][int[]] $Sizes,
        [Parameter(Mandatory = $true)][string] $Destination
    )

    $images = @()
    foreach ($size in $Sizes) {
        $images += ,(New-YeePngBytes -Master $Master -Size $size)
    }

    $temporaryIcon = "$Destination.yee.tmp"
    $stream = [System.IO.File]::Open($temporaryIcon, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write)
    $writer = New-Object System.IO.BinaryWriter $stream
    try {
        $writer.Write([uint16] 0)
        $writer.Write([uint16] 1)
        $writer.Write([uint16] $Sizes.Count)

        $offset = 6 + (16 * $Sizes.Count)
        for ($index = 0; $index -lt $Sizes.Count; $index++) {
            $sizeByte = if ($Sizes[$index] -ge 256) { 0 } else { $Sizes[$index] }
            $writer.Write([byte] $sizeByte)
            $writer.Write([byte] $sizeByte)
            $writer.Write([byte] 0)
            $writer.Write([byte] 0)
            $writer.Write([uint16] 1)
            $writer.Write([uint16] 32)
            $writer.Write([uint32] $images[$index].Length)
            $writer.Write([uint32] $offset)
            $offset += $images[$index].Length
        }

        foreach ($image in $images) {
            $writer.Write($image)
        }
    } finally {
        $writer.Dispose()
        $stream.Dispose()
    }

    Move-Item -LiteralPath $temporaryIcon -Destination $Destination -Force
}

$master = New-YeeMasterBitmap -SourcePath $logoSource
try {
    if ($CheckOnly) {
        $probe = New-YeePngBytes -Master $master -Size 16
        if ($probe.Length -eq 0) {
            throw 'The Yee logo could not be rendered.'
        }
        Write-Host "Windows brand assets can be generated from $logoSource"
        return
    }

    foreach ($size in @(16, 24, 48, 64, 128, 256)) {
        Write-YeePng -Master $master -Size $size -Destination (Join-Path $themeRoot "product_logo_$size.png")
    }
    Write-YeeIcon -Master $master -Sizes @(16, 24, 32, 48, 64, 128, 256) -Destination $windowsIcon
} finally {
    $master.Dispose()
}

Write-Host "Installed Yee Windows app icon and product logos in $chromiumPath"
