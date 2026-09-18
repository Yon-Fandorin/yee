[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string] $ChromiumSrc,

    [switch] $CheckOnly,
    [switch] $SkipBrandAssets
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $scriptDir 'brand-config.ps1')
. (Join-Path $scriptDir 'lib\overlay-tools.ps1')
$repoRoot = Split-Path -Parent (Split-Path -Parent $scriptDir)
$brand = Get-YeeBrandConfig
$chromiumPath = [System.IO.Path]::GetFullPath($ChromiumSrc)
if (-not [System.IO.Path]::IsPathRooted($ChromiumSrc)) {
    throw "Chromium src path must be absolute: $ChromiumSrc"
}

$requiredFiles = @(
    'chrome\browser\ui\tabs\tab_strip_prefs.cc',
    'chrome\app\theme\chromium\BRANDING'
)
foreach ($relativePath in $requiredFiles) {
    if (-not (Test-Path -LiteralPath (Join-Path $chromiumPath $relativePath) -PathType Leaf)) {
        throw "Not a Chromium src checkout; missing $relativePath under $chromiumPath"
    }
}

# Preflight all inputs and patches before the first checkout write.
& (Join-Path $scriptDir 'install-branding.ps1') -ChromiumSrc $chromiumPath -CheckOnly
$sourcePlan = @(Get-YeeOverlaySourcePlan -ChromiumSrc $chromiumPath -RepoRoot $repoRoot)
$patchPlan = @(Get-YeeOverlayPatchPlan -ChromiumSrc $chromiumPath -RepoRoot $repoRoot)
Invoke-YeeOverlayPatchPlan -ChromiumSrc $chromiumPath -Plan $patchPlan -CheckOnly:$CheckOnly
& (Join-Path $scriptDir 'install-branding.ps1') -ChromiumSrc $chromiumPath -CheckOnly:$CheckOnly
Sync-YeeOverlaySourcePlan -Plan $sourcePlan -CheckOnly:$CheckOnly

if (-not $SkipBrandAssets) {
    $brandArgs = @{ ChromiumSrc = $chromiumPath }
    if ($CheckOnly) {
        $brandArgs.CheckOnly = $true
    }
    & (Join-Path $scriptDir 'install-brand-assets.ps1') @brandArgs
}

if ($CheckOnly) {
    Write-Host "$($brand.Name) Chromium overlay is compatible with $chromiumPath"
} else {
    Write-Host "$($brand.Name) Chromium overlay is ready in $chromiumPath"
}
