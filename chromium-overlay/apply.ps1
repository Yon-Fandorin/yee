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
$chromiumPath = [System.IO.Path]::GetFullPath($ChromiumSrc)
$patchFiles = @(
    (Join-Path $scriptDir 'patches\0001-enable-yee-vertical-shell-defaults.patch'),
    (Join-Path $scriptDir 'patches\0002-brand-yee-application.patch'),
    (Join-Path $scriptDir 'patches\0003-add-yee-shell-scaffold.patch'),
    (Join-Path $scriptDir 'patches\0004-place-yee-content-in-layout.patch'),
    (Join-Path $scriptDir 'patches\0005-replace-runway-with-native-toolbar.patch'),
    (Join-Path $scriptDir 'patches\0006-add-interactive-tab-sidebar.patch'),
    (Join-Path $scriptDir 'patches\0007-unify-tab-sidebar-motion.patch'),
    (Join-Path $scriptDir 'patches\0008-fade-pinned-tab-sidebar-with-motion.patch'),
    (Join-Path $scriptDir 'patches\0009-float-edge-tab-sidebar.patch'),
    (Join-Path $scriptDir 'patches\0010-refine-floating-tab-sidebar-surface.patch'),
    (Join-Path $scriptDir 'patches\0011-align-floating-tab-sidebar-hit-region.patch'),
    (Join-Path $scriptDir 'patches\0012-sync-floating-tab-sidebar-foreground-opacity.patch'),
    (Join-Path $scriptDir 'patches\0013-match-pilot-tabs-and-location-bar.patch'),
    (Join-Path $scriptDir 'patches\0014-fix-windows-protoc-python-aliases.patch')
)

function Invoke-GitApply {
    param(
        [Parameter(Mandatory = $true)][string[]] $Arguments,
        [switch] $Quiet
    )

    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = @(& git -C $chromiumPath apply @Arguments 2>&1)
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousPreference
    }

    if (-not $Quiet -and $output.Count -gt 0) {
        $output | ForEach-Object { Write-Host $_ }
    }
    return [pscustomobject]@{
        Success = ($exitCode -eq 0)
        Output = $output
    }
}

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

$shellSeriesAppliedThrough = -1
for ($patchIndex = 12; $patchIndex -ge 2; $patchIndex--) {
    $reverseCheck = Invoke-GitApply -Arguments @(
        '--reverse', '--check', $patchFiles[$patchIndex]
    ) -Quiet
    if ($reverseCheck.Success) {
        $shellSeriesAppliedThrough = $patchIndex
        break
    }
}

for ($patchIndex = 0; $patchIndex -lt $patchFiles.Count; $patchIndex++) {
    $patchFile = $patchFiles[$patchIndex]
    $patchName = Split-Path -Leaf $patchFile

    if ($patchIndex -ge 2 -and $patchIndex -le $shellSeriesAppliedThrough) {
        Write-Host "Already applied: $patchName"
        continue
    }

    $reverseCheck = Invoke-GitApply -Arguments @('--reverse', '--check', $patchFile) -Quiet
    if ($reverseCheck.Success) {
        Write-Host "Already applied: $patchName"
        continue
    }

    $forwardCheck = Invoke-GitApply -Arguments @('--check', $patchFile) -Quiet
    if (-not $forwardCheck.Success) {
        $forwardCheck.Output | ForEach-Object { Write-Host $_ }
        throw "Cannot apply $patchName; its target files have unexpected changes."
    }

    if ($CheckOnly) {
        Write-Host "Applicable: $patchName"
        continue
    }

    $applyResult = Invoke-GitApply -Arguments @($patchFile)
    if (-not $applyResult.Success) {
        throw "Failed to apply $patchName."
    }
    Write-Host "Applied: $patchName"
}

if (-not $SkipBrandAssets) {
    $brandArgs = @{ ChromiumSrc = $chromiumPath }
    if ($CheckOnly) {
        $brandArgs.CheckOnly = $true
    }
    & (Join-Path $scriptDir 'install-brand-assets.ps1') @brandArgs
}

if ($CheckOnly) {
    Write-Host "Yee Chromium overlay is compatible with $chromiumPath"
} else {
    Write-Host "Yee Chromium overlay is ready in $chromiumPath"
}
