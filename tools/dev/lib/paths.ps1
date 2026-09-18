# Paths helpers; loaded by common.ps1.

$script:YeeRoot = Split-Path -Parent (Split-Path -Parent $script:WindowsDevDir)
. (Join-Path $script:YeeRoot 'tools\overlay\brand-config.ps1')
$script:YeeBrand = Get-YeeBrandConfig -SkipAssetValidation
$script:YeeProductName = $script:YeeBrand.Name
$script:LocalBuildRoot = if ($env:YEE_LOCAL_BUILD_ROOT) {
    [System.IO.Path]::GetFullPath($env:YEE_LOCAL_BUILD_ROOT)
} else {
    Join-Path $script:YeeRoot '.local-build'
}
$script:DepotToolsDir = if ($env:YEE_DEPOT_TOOLS_DIR) {
    [System.IO.Path]::GetFullPath($env:YEE_DEPOT_TOOLS_DIR)
} else {
    Join-Path $script:LocalBuildRoot 'depot_tools'
}
$script:ChromiumRoot = if ($env:YEE_CHROMIUM_ROOT) {
    [System.IO.Path]::GetFullPath($env:YEE_CHROMIUM_ROOT)
} else {
    Join-Path $script:LocalBuildRoot 'chromium'
}
$script:ChromiumSrc = Join-Path $script:ChromiumRoot 'src'
$script:YeeOutName = if ($env:YEE_OUT_NAME) { $env:YEE_OUT_NAME } else { 'YeePilot' }
if ($script:YeeOutName -notmatch '^[A-Za-z0-9._-]+$') {
    throw "YEE_OUT_NAME must be one directory name without spaces or separators: $script:YeeOutName"
}
$script:YeeOutDir = Join-Path $script:ChromiumSrc "out\$script:YeeOutName"
$script:YeeBrowserBin = Join-Path $script:YeeOutDir 'chrome.exe'
$script:YeeArgsFile = Join-Path $script:YeeRoot 'build\args.gn'
$script:YeeBuildJobs = if ($env:YEE_BUILD_JOBS) { $env:YEE_BUILD_JOBS } else { '2' }

function Write-YeePaths {
    Write-Host "project root:   $script:YeeRoot"
    Write-Host "local data:     $script:LocalBuildRoot"
    Write-Host "depot_tools:    $script:DepotToolsDir"
    Write-Host "Chromium src:   $script:ChromiumSrc"
    Write-Host "build output:   $script:YeeOutDir"
}
