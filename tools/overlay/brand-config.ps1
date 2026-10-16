# Shared data source for Windows tooling; no Python/toolchain is needed to read it.
$script:ProductBrandRepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$script:ProductBrandConfigPath = Join-Path $script:ProductBrandRepoRoot 'branding\brand.json'

function Get-YeeBrandConfig {
    param([switch] $SkipAssetValidation)
    $repoRoot = $script:ProductBrandRepoRoot
    $config = Get-Content -LiteralPath $script:ProductBrandConfigPath -Raw -Encoding UTF8 | ConvertFrom-Json
    foreach ($property in $config.PSObject.Properties.Name) {
        if ($property -notin @('name', 'short_name', 'logo_source', 'logo_crop_size', 'provisional', 'internal_url_scheme', 'logo_assets_manifest', 'logo_palette', 'strings_policy')) {
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
    $scheme = if ($config.PSObject.Properties['internal_url_scheme']) { $config.internal_url_scheme } else { $shortName.ToLowerInvariant() }
    $reservedSchemes = @('about', 'android-app', 'blob', 'chrome', 'cid', 'content', 'data',
        'devtools', 'dom-distiller', 'externalfile', 'file', 'filesystem',
        'ftp', 'http', 'https', 'intent', 'isolated-app', 'javascript', 'mailto',
        'shell', 'sms', 'tel', 'urn', 'view-source', 'webcal', 'ws', 'wss')
    if ($scheme -isnot [string] -or $scheme -cnotmatch '^[a-z][a-z0-9+.-]*$' -or
        $scheme -in $reservedSchemes -or $scheme.StartsWith('chrome-')) {
        throw 'internal_url_scheme must be a lowercase ASCII URL scheme that does not conflict with a built-in protocol; set it explicitly when short_name cannot be used as a scheme.'
    }
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
    $cropSize = if ($config.PSObject.Properties['logo_crop_size']) { $config.logo_crop_size } else { 1024 }
    if ($cropSize -isnot [int] -and $cropSize -isnot [long]) {
        throw 'logo_crop_size must be a positive integer.'
    }
    if ($cropSize -le 0) { throw 'logo_crop_size must be a positive integer.' }
    $assetsManifest = $null
    if ($config.PSObject.Properties['logo_assets_manifest']) {
        $value = $config.logo_assets_manifest
        if ($value -isnot [string] -or [string]::IsNullOrWhiteSpace($value) -or [System.IO.Path]::IsPathRooted($value)) {
            throw 'logo_assets_manifest must be a repository-relative file path.'
        }
        $assetsManifest = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $value))
        if (-not $assetsManifest.StartsWith($rootPrefix, [System.StringComparison]::OrdinalIgnoreCase) -or
            (-not $SkipAssetValidation -and -not (Test-Path -LiteralPath $assetsManifest -PathType Leaf))) {
            throw 'logo_assets_manifest must identify a file inside the repository.'
        }
    }
    $palette = @{}
    if ($config.PSObject.Properties['logo_palette']) {
        if ($config.logo_palette -isnot [pscustomobject]) { throw 'logo_palette must be a colour object.' }
        foreach ($property in $config.logo_palette.PSObject.Properties) {
            if ($property.Name -notin @('primary', 'secondary', 'dark_primary', 'dark_secondary', 'background', 'dark_background') -or
                $property.Value -isnot [string] -or $property.Value -cnotmatch '^#[0-9A-Fa-f]{6}$') {
                throw 'logo_palette must contain named six-digit hexadecimal colours.'
            }
            $palette[$property.Name] = $property.Value
        }
    }
    $stringsPolicy = $null
    if ($config.PSObject.Properties['strings_policy']) {
        $value = $config.strings_policy
        if ($value -isnot [string] -or [string]::IsNullOrWhiteSpace($value) -or [System.IO.Path]::IsPathRooted($value)) {
            throw 'strings_policy must be a repository-relative file path.'
        }
        $stringsPolicy = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $value))
        if (-not $stringsPolicy.StartsWith($rootPrefix, [System.StringComparison]::OrdinalIgnoreCase) -or
            -not (Test-Path -LiteralPath $stringsPolicy -PathType Leaf)) {
            throw 'strings_policy must identify a file inside the repository.'
        }
    }
    return [pscustomobject]@{
        Name = $config.name
        ShortName = $shortName
        LogoSource = $logoSource
        LogoCropSize = $cropSize
        LogoAssetsManifest = $assetsManifest
        LogoPalette = $palette
        StringsPolicy = $stringsPolicy
        Provisional = $provisional
        InternalUrlScheme = $scheme
    }
}
