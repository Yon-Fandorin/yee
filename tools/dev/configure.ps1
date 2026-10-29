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
& (Join-Path $script:YeeRoot 'tools\overlay\apply.ps1') @overlayArgs

if ($CheckOverlayOnly) {
    return
}

$gn = Get-DepotCommand -Name 'gn'
# 배치 실행 시 문자열 값의 따옴표가 사라지지 않도록 GN이 파일을 직접 읽게 한다.
New-Item -ItemType Directory -Path $script:YeeOutDir -Force | Out-Null
$gnArguments = Get-Content -LiteralPath $script:YeeArgsFile -Raw -Encoding UTF8
[System.IO.File]::WriteAllText((Join-Path $script:YeeOutDir 'args.gn'), $gnArguments,
    [System.Text.UTF8Encoding]::new($false))
Push-Location $script:ChromiumSrc
try {
    & $gn gen "out\$script:YeeOutName"
    if ($LASTEXITCODE -ne 0) {
        throw 'GN generation failed.'
    }
    Write-Host "Configured compact Windows output at $script:YeeOutDir"
    & $gn args "out\$script:YeeOutName" --list --short |
        Select-String '^(is_debug|is_component_build|symbol_level|blink_symbol_level|v8_symbol_level|is_official_build|use_lld) ='
} finally {
    Pop-Location
}
