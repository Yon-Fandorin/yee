[CmdletBinding()]
param()

. (Join-Path $PSScriptRoot 'common.ps1')
$jobs = Get-YeeSyncJobs

Add-DepotToolsToPath
Assert-ChromiumSrc
Assert-FreeGiB -RequiredGiB 35 -Purpose 'a Chromium dependency sync'

$gclient = Get-DepotCommand -Name 'gclient'
Push-Location $script:ChromiumRoot
try {
    # 기준 검사를 통과한 src의 제품 수정은 유지하고 의존 소스만 갱신한다.
    & $gclient sync --no-history -D --revision src@unmanaged --jobs $jobs
    if ($LASTEXITCODE -ne 0) {
        throw 'gclient sync failed.'
    }
} finally {
    Pop-Location
}

Write-Host "Sync complete. Free space: $(Get-AvailableGiB) GiB"
Write-Host 'This command syncs dependencies for the checked-out Chromium commit; it does not move src to a newer commit.'
