[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string] $ChromiumSrc,
    [switch] $CheckOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'brand-config.ps1')
$brand = Get-YeeBrandConfig
$repoRoot = $script:ProductBrandRepoRoot
$chromiumPath = [System.IO.Path]::GetFullPath($ChromiumSrc)
if (-not [System.IO.Path]::IsPathRooted($ChromiumSrc) -or
    -not (Test-Path -LiteralPath (Join-Path $chromiumPath 'chrome/app/theme/chromium/BRANDING') -PathType Leaf)) {
    throw 'Expected an absolute Chromium source checkout.'
}
if (-not $brand.LogoAssetsManifest) { throw 'Set logo_assets_manifest and generate the brand package first.' }
$packageRoot = Split-Path -Parent $brand.LogoAssetsManifest
$manifest = Get-Content -LiteralPath $brand.LogoAssetsManifest -Raw -Encoding UTF8 | ConvertFrom-Json
$sourceHash = (Get-FileHash -LiteralPath $brand.LogoSource -Algorithm SHA256).Hash.ToLowerInvariant()
$generatorHash = (Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'generate-brand-assets.py') -Algorithm SHA256).Hash.ToLowerInvariant()
if ($manifest.schema_version -ne 1) { throw 'Unsupported brand asset package schema.' }
if ($manifest.product_name -cne $brand.Name -or $manifest.short_name -cne $brand.ShortName -or
    $manifest.source_sha256 -ne $sourceHash -or $manifest.generator_sha256 -ne $generatorHash -or
    @($manifest.palette.PSObject.Properties).Count -ne $brand.LogoPalette.Count) {
    throw 'Brand asset package is stale; regenerate it on macOS with generate-brand-assets.py.'
}
foreach ($property in $manifest.palette.PSObject.Properties) {
    if (-not $brand.LogoPalette.ContainsKey($property.Name) -or
        $brand.LogoPalette[$property.Name] -cne $property.Value) {
        throw 'Brand asset palette is stale; regenerate the package.'
    }
}
$catalog = Get-Content -LiteralPath (Join-Path $repoRoot 'build/overlay.json') -Raw -Encoding UTF8 | ConvertFrom-Json
function Get-ConfinedAssetPath {
    param([string] $Root, [string] $Relative)
    if ([string]::IsNullOrWhiteSpace($Relative) -or $Relative -match '[\\:*?\[\]\x00\r\n]' -or
        [System.IO.Path]::IsPathRooted($Relative) -or
        ($Relative.Split('/') | Where-Object { $_ -in @('', '.', '..') })) {
        throw "Expected a literal relative asset path: $Relative"
    }
    $path = [System.IO.Path]::GetFullPath((Join-Path $Root $Relative))
    $prefix = [System.IO.Path]::GetFullPath($Root) + [System.IO.Path]::DirectorySeparatorChar
    if (-not $path.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Asset path escapes its root: $Relative"
    }
    $part = $path
    while ($part -and $part -ne $Root) {
        if (Test-Path -LiteralPath $part) {
            if ($part -ne $path -and -not (Test-Path -LiteralPath $part -PathType Container)) {
                throw "Asset destination parent is not a directory: $Relative"
            }
            if ((Get-Item -LiteralPath $part -Force).Attributes -band [System.IO.FileAttributes]::ReparsePoint) {
                throw "Asset path traverses a link: $Relative"
            }
        }
        $part = Split-Path -Parent $part
    }
    return $path
}
$plan = @()
$destinations = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
if (-not $manifest.files.Count) { throw 'Brand package contains no outputs.' }
foreach ($entry in $manifest.files) {
    $source = Get-ConfinedAssetPath -Root $packageRoot -Relative $entry.source
    $destination = Get-ConfinedAssetPath -Root $chromiumPath -Relative $entry.destination
    $owned = @($catalog.generated_roots | Where-Object {
        $entry.destination -ceq $_ -or $entry.destination.StartsWith($_ + '/', [System.StringComparison]::Ordinal)
    })
    if (-not $owned.Count -or -not $destinations.Add($entry.destination)) {
        throw "Unowned or duplicate asset destination: $($entry.destination)"
    }
    if (-not (Test-Path -LiteralPath $source -PathType Leaf) -or
        (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant() -cne $entry.sha256) {
        throw "Missing or modified package asset: $source"
    }
    if (Test-Path -LiteralPath $destination) {
        if (-not (Test-Path -LiteralPath $destination -PathType Leaf)) { throw "Asset destination is not a file: $destination" }
        if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant() -ceq $entry.sha256) { continue }
    }
    $plan += [pscustomobject]@{ Source = $source; Destination = $destination }
}
if (-not $CheckOnly) {
    foreach ($item in $plan) {
        [System.IO.Directory]::CreateDirectory((Split-Path -Parent $item.Destination)) | Out-Null
        $temporary = $item.Destination + '.yee.tmp'
        try {
            Copy-Item -LiteralPath $item.Source -Destination $temporary -Force
            Move-Item -LiteralPath $temporary -Destination $item.Destination -Force
        } finally {
            if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Force }
        }
    }
}
$action = if ($CheckOnly) { 'pending' } else { 'installed' }
Write-Host "$($brand.Name) brand assets: $($manifest.files.Count) verified, $($plan.Count) $action"
