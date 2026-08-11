[CmdletBinding()]
param(
    [switch] $CheckOverlayOnly
)

. (Join-Path $PSScriptRoot 'common.ps1')

Add-DepotToolsToPath
Assert-ChromiumSrc
Assert-FreeGiB -RequiredGiB 45 -Purpose 'GN generation and the compact Chromium build'

$overlayArgs = @{ ChromiumSrc = $script:ChromiumSrc }
if ($CheckOverlayOnly) {
    $overlayArgs.CheckOnly = $true
}
& (Join-Path $script:YeeRoot 'chromium-overlay\apply.ps1') @overlayArgs

if ($CheckOverlayOnly) {
    return
}

$gn = Get-DepotCommand -Name 'gn'
$gnArguments = Get-GnArguments
Push-Location $script:ChromiumSrc
try {
    & $gn gen "out\$script:YeeOutName" "--args=$gnArguments"
    if ($LASTEXITCODE -ne 0) {
        throw 'GN generation failed.'
    }
    Write-Host "Configured compact Windows output at $script:YeeOutDir"
    & $gn args "out\$script:YeeOutName" --list --short |
        Select-String '^(is_debug|is_component_build|symbol_level|blink_symbol_level|v8_symbol_level|is_official_build|use_lld) ='
} finally {
    Pop-Location
}
