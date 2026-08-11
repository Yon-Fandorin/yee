[CmdletBinding()]
param(
    [ValidateSet('chrome', 'mini_installer')]
    [string] $Target = 'chrome',

    [switch] $AllowSharedChromiumInstallIdentity
)

. (Join-Path $PSScriptRoot 'common.ps1')

Add-DepotToolsToPath
Assert-ChromiumSrc
Assert-FreeGiB -RequiredGiB 35 -Purpose "the Chromium $Target target"

if ($Target -eq 'mini_installer' -and -not $AllowSharedChromiumInstallIdentity) {
    throw @'
The pilot changes Yee's display name and icon but still shares Chromium's
Windows install identity, paths, AppID, and ProgID. Building mini_installer
requires -AllowSharedChromiumInstallIdentity until a dedicated Yee install-mode
patch is added. Do not install it alongside another Chromium installation.
'@
}

$jobs = 0
if (-not [int]::TryParse($script:YeeBuildJobs, [ref]$jobs) -or $jobs -lt 1) {
    throw "YEE_BUILD_JOBS must be a positive integer: $script:YeeBuildJobs"
}

if (-not (Test-Path -LiteralPath (Join-Path $script:YeeOutDir 'build.ninja') -PathType Leaf)) {
    & (Join-Path $PSScriptRoot 'configure.ps1')
}

Set-YeeCacheEnvironment
$env:NINJA_SUMMARIZE_BUILD = '1'
$autoninja = Get-DepotCommand -Name 'autoninja'

Push-Location $script:ChromiumSrc
try {
    Write-Host "Building $Target with $jobs parallel jobs."
    & $autoninja -C "out\$script:YeeOutName" -j $jobs $Target
    if ($LASTEXITCODE -ne 0) {
        throw "Chromium $Target build failed."
    }
} finally {
    Pop-Location
}

Write-Host "Build complete. Free space: $(Get-AvailableGiB) GiB"
Write-Host 'Run .\chromium-dev\usage.ps1 separately when a full recursive disk-usage scan is needed.'
