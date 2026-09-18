Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Keep this entry point stable for build, validation, and research scripts.
$script:WindowsDevDir = $PSScriptRoot
. (Join-Path $script:WindowsDevDir 'lib\paths.ps1')
. (Join-Path $script:WindowsDevDir 'lib\preflight.ps1')
. (Join-Path $script:WindowsDevDir 'lib\build.ps1')
