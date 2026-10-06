[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)][string] $ChromiumSrc,
    [switch] $CheckOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'brand-config.ps1')
. (Join-Path $PSScriptRoot 'lib\overlay-tools.ps1')
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$brand = Get-YeeBrandConfig
if (-not [System.IO.Path]::IsPathRooted($ChromiumSrc)) { throw 'Chromium src path must be absolute.' }
$appDir = Join-Path $ChromiumSrc 'chrome\app'
$brandingFile = Join-Path $appDir 'theme\chromium\BRANDING'
function Read-BrandingInput {
    $inputText = [System.IO.File]::ReadAllText($brandingFile)
    $inputText = [regex]::Replace($inputText, '(?m)^# Product names are managed by the Yee overlay brand installer\.\r?\n', '')
    $reader = New-Object System.IO.StringReader $inputText
    try {
        while ($null -ne ($line = $reader.ReadLine())) {
            if (-not $line.Contains('=')) {
                throw 'BRANDING requires KEY=VALUE on every line for Chromium version.py.'
            }
        }
    } finally { $reader.Dispose() }
    foreach ($key in @('PRODUCT_FULLNAME', 'PRODUCT_SHORTNAME', 'PRODUCT_INSTALLER_FULLNAME', 'PRODUCT_INSTALLER_SHORTNAME')) {
        if ([regex]::Matches($inputText, "(?m)^${key}=[^\r\n]*").Count -ne 1) {
            throw "BRANDING must contain exactly one $key"
        }
    }
    if ([regex]::Matches($inputText, '(?m)^PRODUCT_INTERNAL_URL_SCHEME=[^\r\n]*').Count -gt 1) {
        throw 'BRANDING must contain at most one PRODUCT_INTERNAL_URL_SCHEME.'
    }
    return $inputText
}
$null = Read-BrandingInput
$patchPlan = @(Get-YeeOverlayPatchPlan -ChromiumSrc $ChromiumSrc -RepoRoot $repoRoot -Role branding)
Invoke-YeeOverlayPatchPlan -ChromiumSrc $ChromiumSrc -Plan $patchPlan -CheckOnly:$CheckOnly
$text = Read-BrandingInput
if (-not $CheckOnly -and -not [regex]::IsMatch($text, '(?m)^OVERLAY_BRANDING_MANAGED=1\r?$')) {
    throw 'Apply the 0002 branding patch before installing names.'
}
$values = [ordered]@{
    PRODUCT_FULLNAME = $brand.Name
    PRODUCT_SHORTNAME = $brand.ShortName
    PRODUCT_INSTALLER_FULLNAME = $brand.Name + ' Installer'
    PRODUCT_INSTALLER_SHORTNAME = $brand.ShortName + ' Installer'
}
foreach ($key in $values.Keys) {
    $pattern = "(?m)^${key}=[^\r\n]*"
    if ([regex]::Matches($text, $pattern).Count -ne 1) { throw "BRANDING must contain exactly one $key" }
    $replacement = $key + '=' + $values[$key]
    $text = [regex]::Replace($text, $pattern, [System.Text.RegularExpressions.MatchEvaluator] { param($match) $replacement })
}
$schemePattern = '(?m)^PRODUCT_INTERNAL_URL_SCHEME=[^\r\n]*'
$schemeMatches = [regex]::Matches($text, $schemePattern).Count
if ($schemeMatches -gt 1) { throw 'BRANDING must contain at most one PRODUCT_INTERNAL_URL_SCHEME.' }
$schemeValue = 'PRODUCT_INTERNAL_URL_SCHEME=' + $brand.InternalUrlScheme
if ($schemeMatches -eq 1) {
    $text = [regex]::Replace($text, $schemePattern, $schemeValue)
} else {
    $text = $text.TrimEnd([char[]]"`r`n") + "`n" + $schemeValue + "`n"
}
function New-ProductMessage([string] $Id, [string] $Value) {
    $escaped = [System.Security.SecurityElement]::Escape($Value)
    return "  <message name=`"$Id`" desc=`"Configured product name.`" translateable=`"false`">$escaped</message>`n"
}
function New-ProductPart([string] $Messages) {
    return "<?xml version=`"1.0`" encoding=`"utf-8`"?>`n<!-- Generated from branding/brand.json. -->`n<grit-part>`n${Messages}</grit-part>`n"
}
$plan = [ordered]@{}
$plan[$brandingFile] = $text
$plan[(Join-Path $appDir 'yee_product_names.grdp')] = New-ProductPart (
    (New-ProductMessage 'IDS_PRODUCT_NAME' $brand.Name) + (New-ProductMessage 'IDS_SHORT_PRODUCT_NAME' $brand.ShortName))
$plan[(Join-Path $appDir 'yee_app_menu_name.grdp')] = New-ProductPart (New-ProductMessage 'IDS_APP_MENU_PRODUCT_NAME' $brand.ShortName)
$utf8 = New-Object System.Text.UTF8Encoding $false
foreach ($path in $plan.Keys) {
    if ((Test-Path -LiteralPath $path -PathType Leaf) -and [System.IO.File]::ReadAllText($path) -ceq $plan[$path]) { continue }
    if (-not $CheckOnly) {
        $temporary = $path + '.yee.tmp'
        try {
            [System.IO.File]::WriteAllText($temporary, $plan[$path], $utf8)
            Move-Item -LiteralPath $temporary -Destination $path -Force
        } finally {
            if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Force }
        }
    }
    $verb = if ($CheckOnly) { 'Would install' } else { 'Installed' }
    Write-Host "${verb}: $(Split-Path -Leaf $path)"
}
