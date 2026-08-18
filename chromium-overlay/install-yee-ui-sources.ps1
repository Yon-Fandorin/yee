[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string] $ChromiumSrc
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$sourceRoot = Join-Path $scriptDir 'yee-ui\chrome\browser\ui\views\yee'
$destinationRoot = Join-Path ([System.IO.Path]::GetFullPath($ChromiumSrc)) `
    'chrome\browser\ui\views\yee'

if (-not (Test-Path -LiteralPath (Join-Path $ChromiumSrc 'chrome\browser\ui\BUILD.gn') -PathType Leaf)) {
    throw "Not a Chromium src checkout: $ChromiumSrc"
}

New-Item -ItemType Directory -Path $destinationRoot -Force | Out-Null

Get-ChildItem -LiteralPath $sourceRoot -File | ForEach-Object {
    $destination = Join-Path $destinationRoot $_.Name
    $needsCopy = -not (Test-Path -LiteralPath $destination -PathType Leaf)
    if (-not $needsCopy) {
        $sourceHash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
        $destinationHash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
        $needsCopy = $sourceHash -ne $destinationHash
    }
    if ($needsCopy) {
        Copy-Item -LiteralPath $_.FullName -Destination $destination -Force
        Write-Host "Synced Yee UI source: $($_.Name)"
    }
}
