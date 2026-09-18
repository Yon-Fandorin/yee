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

Initialize-YeeBuildInputs
Invoke-YeeBuildTarget -Target $Target

Write-Host "Build complete. Free space: $(Get-AvailableGiB) GiB"
Write-Host 'Run .\tools\dev\usage.ps1 separately when a full recursive disk-usage scan is needed.'
