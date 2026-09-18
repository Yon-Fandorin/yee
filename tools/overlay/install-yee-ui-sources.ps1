[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string] $ChromiumSrc,
    [switch] $CheckOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $scriptDir 'lib\overlay-tools.ps1')
$repoRoot = Split-Path -Parent (Split-Path -Parent $scriptDir)
if (-not [System.IO.Path]::IsPathRooted($ChromiumSrc)) { throw 'Chromium src path must be absolute.' }
$destinationRoot = [System.IO.Path]::GetFullPath($ChromiumSrc)
if (-not (Test-Path -LiteralPath (Join-Path $destinationRoot 'chrome\browser\ui\BUILD.gn') -PathType Leaf)) {
    throw "Not a Chromium src checkout: $ChromiumSrc"
}
$plan = @(Get-YeeOverlaySourcePlan -ChromiumSrc $destinationRoot -RepoRoot $repoRoot)
Sync-YeeOverlaySourcePlan -Plan $plan -CheckOnly:$CheckOnly
