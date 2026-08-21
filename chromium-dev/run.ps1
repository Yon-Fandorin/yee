[CmdletBinding()]
param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $BrowserArguments
)

. (Join-Path $PSScriptRoot 'common.ps1')

$profileDir = Join-Path $script:LocalBuildRoot 'runtime-profile'
if (-not (Test-Path -LiteralPath $script:YeeBrowserBin -PathType Leaf)) {
    throw "Built Yee browser is missing: $script:YeeBrowserBin. Run .\chromium-dev\build.ps1 first."
}

$defaultProfileDir = Join-Path $profileDir 'Default'
New-Item -ItemType Directory -Path $defaultProfileDir -Force | Out-Null
$preferencesPath = Join-Path $defaultProfileDir 'Preferences'
if (-not (Test-Path -LiteralPath $preferencesPath -PathType Leaf)) {
    Copy-Item -LiteralPath (Join-Path $script:YeeRoot 'native-pilot\profile-template\Preferences') `
        -Destination $preferencesPath
}
New-Item -ItemType File -Path (Join-Path $profileDir 'First Run') -Force | Out-Null

$arguments = @(
    "--user-data-dir=$profileDir",
    '--no-first-run',
    '--no-default-browser-check',
    '--hide-crash-restore-bubble',
    '--disable-sync',
    '--disable-features=ChromeWhatsNewUI',
    '--force-color-profile=srgb'
) + $BrowserArguments

& $script:YeeBrowserBin @arguments
