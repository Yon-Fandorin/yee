[CmdletBinding()]
param()

. (Join-Path $PSScriptRoot 'common.ps1')

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

if (Test-Path -LiteralPath (Join-Path $script:ChromiumSrc 'BUILD.gn') -PathType Leaf) {
    Write-Host "Chromium checkout already exists at $script:ChromiumSrc"
    Write-Host 'Use .\chromium-dev\sync.ps1 to update its dependencies.'
    return
}

if ((Test-Path -LiteralPath (Join-Path $script:ChromiumRoot '.gclient')) -or
    (Test-Path -LiteralPath $script:ChromiumSrc)) {
    throw "Partial checkout detected at $script:ChromiumRoot. Resume it there with gclient sync --no-history."
}

Assert-FreeGiB -RequiredGiB 115 -Purpose 'the shallow Chromium source checkout'
New-Item -ItemType Directory -Path $script:ChromiumRoot -Force | Out-Null
$fetch = Get-DepotCommand -Name 'fetch'

Push-Location $script:ChromiumRoot
try {
    Write-Host 'Fetching Chromium without repository history or a separate git cache.'
    & $fetch --no-history chromium
    if ($LASTEXITCODE -ne 0) {
        throw 'Chromium fetch failed. Resume with gclient sync --no-history.'
    }
} finally {
    Pop-Location
}

Write-Host "Checkout complete: $script:ChromiumSrc"
Write-Host "Free space: $(Get-AvailableGiB) GiB"
