[CmdletBinding()]
param(
    [ValidateSet('chrome', 'mini_installer')]
    [string] $Target = 'chrome'
)

. (Join-Path $PSScriptRoot 'common.ps1')

Add-DepotToolsToPath
Assert-ChromiumSrc
Assert-FreeGiB -RequiredGiB 35 -Purpose "the Chromium $Target target"

Initialize-YeeBuildInputs
Invoke-YeeBuildTarget -Target $Target

Write-Host "Build complete. Free space: $(Get-AvailableGiB) GiB"
Write-Host 'Run .\tools\dev\usage.ps1 separately when a full recursive disk-usage scan is needed.'
