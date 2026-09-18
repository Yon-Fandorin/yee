[CmdletBinding()]
param()

. (Join-Path $PSScriptRoot 'common.ps1')

Add-DepotToolsToPath
Assert-ChromiumSrc
Assert-FreeGiB -RequiredGiB 5 -Purpose "the isolated $script:YeeProductName UI target"

Initialize-YeeBuildInputs
Invoke-YeeBuildTarget -Target 'chrome/browser/ui/views/yee:yee_ui'

Write-Host "$script:YeeProductName UI target complete. Run .\tools\dev\build.ps1 only when an integrated app is needed."
