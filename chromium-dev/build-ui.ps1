[CmdletBinding()]
param()

. (Join-Path $PSScriptRoot 'common.ps1')

Add-DepotToolsToPath
Assert-ChromiumSrc
Assert-FreeGiB -RequiredGiB 5 -Purpose 'the isolated Yee UI target'

$jobs = 0
if (-not [int]::TryParse($script:YeeBuildJobs, [ref]$jobs) -or $jobs -lt 1) {
    throw "YEE_BUILD_JOBS must be a positive integer: $script:YeeBuildJobs"
}

$yeeUiBuildFile = Join-Path $script:ChromiumSrc 'chrome\browser\ui\views\yee\BUILD.gn'
if (Test-Path -LiteralPath $yeeUiBuildFile -PathType Leaf) {
    & (Join-Path $script:YeeRoot 'chromium-overlay\install-yee-ui-sources.ps1') `
        -ChromiumSrc $script:ChromiumSrc
} else {
    & (Join-Path $script:YeeRoot 'chromium-overlay\apply.ps1') `
        -ChromiumSrc $script:ChromiumSrc
}

if (-not (Test-Path -LiteralPath (Join-Path $script:YeeOutDir 'build.ninja') -PathType Leaf)) {
    & (Join-Path $PSScriptRoot 'configure.ps1')
}

Set-YeeCacheEnvironment
$env:NINJA_SUMMARIZE_BUILD = '1'
$autoninja = Get-DepotCommand -Name 'autoninja'

Push-Location $script:ChromiumSrc
try {
    Write-Host "Building the isolated Yee UI target with $jobs parallel jobs."
    & $autoninja -C "out\$script:YeeOutName" -j $jobs `
        'chrome/browser/ui/views/yee:yee_ui'
    if ($LASTEXITCODE -ne 0) {
        throw 'Yee UI target build failed.'
    }
} finally {
    Pop-Location
}

Write-Host 'Yee UI target complete. Run .\chromium-dev\build.ps1 only when an integrated app is needed.'
