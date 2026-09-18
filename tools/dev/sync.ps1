[CmdletBinding()]
param()

. (Join-Path $PSScriptRoot 'common.ps1')

Add-DepotToolsToPath
Assert-ChromiumSrc
Assert-FreeGiB -RequiredGiB 35 -Purpose 'a Chromium dependency sync'

$gclient = Get-DepotCommand -Name 'gclient'
Push-Location $script:ChromiumRoot
try {
    & $gclient sync --no-history -D
    if ($LASTEXITCODE -ne 0) {
        throw 'gclient sync failed.'
    }
} finally {
    Pop-Location
}

Write-Host "Sync complete. Free space: $(Get-AvailableGiB) GiB"
Write-Host 'This command syncs dependencies for the checked-out Chromium commit; it does not move src to a newer commit.'
