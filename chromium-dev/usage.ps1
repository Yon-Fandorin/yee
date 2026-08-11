[CmdletBinding()]
param()

. (Join-Path $PSScriptRoot 'common.ps1')

Write-Host "Free filesystem space: $(Get-AvailableGiB) GiB"

foreach ($measuredPath in @($script:DepotToolsDir, $script:ChromiumRoot, $script:YeeOutDir)) {
    if (-not (Test-Path -LiteralPath $measuredPath)) {
        continue
    }

    $bytes = (Get-ChildItem -LiteralPath $measuredPath -File -Recurse -Force -ErrorAction SilentlyContinue |
        Measure-Object -Property Length -Sum).Sum
    if ($null -eq $bytes) {
        $bytes = 0
    }
    Write-Host ("{0,8:N2} GiB  {1}" -f ($bytes / 1GB), $measuredPath)
}
