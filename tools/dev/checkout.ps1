[CmdletBinding()]
param()

. (Join-Path $PSScriptRoot 'common.ps1')
$jobs = Get-YeeSyncJobs

New-Item -ItemType Directory -Path $script:LocalBuildRoot -Force | Out-Null

if (-not (Test-Path -LiteralPath (Join-Path $script:DepotToolsDir '.git') -PathType Container)) {
    Assert-FreeGiB -RequiredGiB 120 -Purpose 'the initial Chromium checkout'
    Write-Host "Cloning depot_tools into $script:DepotToolsDir"
    & git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git $script:DepotToolsDir
    if ($LASTEXITCODE -ne 0) {
        throw 'Failed to clone depot_tools.'
    }
}

Add-DepotToolsToPath

# Chromium asks Windows users to bootstrap depot_tools from cmd.exe once so its
# platform-specific Python and helper shims are installed correctly.
$gclient = Get-DepotCommand -Name 'gclient'
& cmd.exe /d /c "call `"$gclient`""
if ($LASTEXITCODE -ne 0) {
    throw 'depot_tools bootstrap failed.'
}
$revision = Get-ChromiumPinnedValue -Name revision
$sourceUrl = Get-ChromiumPinnedValue -Name source_url

if (Test-Path -LiteralPath (Join-Path $script:ChromiumSrc 'BUILD.gn') -PathType Leaf) {
    Assert-ChromiumSrc
    Write-Host "Chromium checkout already exists at $script:ChromiumSrc"
    Write-Host 'Use .\tools\dev\sync.ps1 to update its dependencies.'
    return
}

if ((Test-Path -LiteralPath (Join-Path $script:ChromiumRoot '.gclient')) -or
    (Test-Path -LiteralPath $script:ChromiumSrc)) {
    throw "Partial checkout detected at $script:ChromiumRoot. Resume it there with gclient sync --no-history --revision src@$revision --jobs $jobs."
}

Assert-FreeGiB -RequiredGiB 115 -Purpose 'the shallow Chromium source checkout'
New-Item -ItemType Directory -Path $script:ChromiumRoot -Force | Out-Null

Push-Location $script:ChromiumRoot
try {
    Write-Host "Preparing the pinned Chromium revision $revision."
    & $gclient config --name src --unmanaged $sourceUrl
    if ($LASTEXITCODE -ne 0) { throw 'gclient configuration failed.' }
    & $gclient sync --no-history --revision "src@$revision" --jobs $jobs
    if ($LASTEXITCODE -ne 0) {
        throw "Chromium fetch failed. Resume at $script:ChromiumRoot with gclient sync --no-history --revision src@$revision --jobs $jobs."
    }
} finally {
    Pop-Location
}

Assert-ChromiumSrc
Write-Host "Checkout complete: $script:ChromiumSrc"
Write-Host "Free space: $(Get-AvailableGiB) GiB"
