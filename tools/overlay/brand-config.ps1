# Shared data source for Windows tooling; no Python/toolchain is needed to read it.
$script:ProductBrandRepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$script:ProductBrandConfigPath = Join-Path $script:ProductBrandRepoRoot 'branding\brand.json'

function Get-YeeBrandConfig {
    param([switch] $SkipAssetValidation)
    $repoRoot = $script:ProductBrandRepoRoot
    $config = Get-Content -LiteralPath $script:ProductBrandConfigPath -Raw -Encoding UTF8 | ConvertFrom-Json
    foreach ($property in $config.PSObject.Properties.Name) {
        if ($property -notin @('name', 'short_name', 'logo_source', 'logo_crop_size', 'provisional')) {
            throw "brand.json contains an unknown field: $property"
        }
    }
    $shortName = if ($config.PSObject.Properties['short_name']) { $config.short_name } else { $config.name }
    foreach ($name in @($config.name, $shortName)) {
        if ($name -isnot [string] -or [string]::IsNullOrWhiteSpace($name) -or
            $name -ne $name.Trim() -or $name -match '[\x00-\x1f\x7f\\/:*?"<>|]' -or $name.EndsWith('.')) {
            throw 'name and short_name must be non-empty, portable app names.'
        }
        if ($name.Contains('$') -or $name -match '@[A-Za-z0-9_]+@') {
            throw 'name and short_name must not contain GN interpolation or version placeholders.'
        }
    }
    $provisional = if ($config.PSObject.Properties['provisional']) { $config.provisional } else { $true }
    if ($provisional -isnot [bool]) { throw 'provisional must be a boolean.' }
    if ($config.logo_source -isnot [string] -or [string]::IsNullOrWhiteSpace($config.logo_source) -or
        [System.IO.Path]::IsPathRooted($config.logo_source)) {
        throw 'logo_source must be a repository-relative file path.'
    }
    $logoSource = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $config.logo_source))
    $rootPrefix = [System.IO.Path]::GetFullPath($repoRoot) + [System.IO.Path]::DirectorySeparatorChar
    if (-not $logoSource.StartsWith($rootPrefix, [System.StringComparison]::OrdinalIgnoreCase) -or
        (-not $SkipAssetValidation -and -not (Test-Path -LiteralPath $logoSource -PathType Leaf))) {
        throw 'logo_source must identify a file inside the repository.'
    }
    if ($config.logo_crop_size -isnot [int] -and $config.logo_crop_size -isnot [long]) {
        throw 'logo_crop_size must be a positive integer.'
    }
    if ($config.logo_crop_size -le 0) { throw 'logo_crop_size must be a positive integer.' }
    return [pscustomobject]@{
        Name = $config.name
        ShortName = $shortName
        LogoSource = $logoSource
        LogoCropSize = $config.logo_crop_size
        Provisional = $provisional
    }
}
