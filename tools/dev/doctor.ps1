[CmdletBinding()]
param()

. (Join-Path $PSScriptRoot 'common.ps1')

Write-YeePaths
Write-Host "free space:     $(Get-AvailableGiB) GiB"
Write-Host "Windows:        $([Environment]::OSVersion.VersionString)"
Write-Host "architecture:   $env:PROCESSOR_ARCHITECTURE"
Write-Host "PowerShell:     $($PSVersionTable.PSVersion)"
Write-Host "recommended -j: $(Get-RecommendedBuildJobs) (physical cores capped by 8 GiB/job)"

if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw 'Chromium Windows build scripts require Windows.'
}

foreach ($commandName in @('git', 'cmd.exe')) {
    $command = Get-Command $commandName -ErrorAction SilentlyContinue
    if (-not $command) {
        throw "Missing required command: $commandName"
    }
    Write-Host ("{0,-16}{1}" -f "$commandName`:", $command.Source)
}

$volumeRoot = [System.IO.Path]::GetPathRoot($script:LocalBuildRoot)
$driveLetter = $volumeRoot.Substring(0, 1)
$volume = Get-Volume -DriveLetter $driveLetter -ErrorAction SilentlyContinue
if ($volume) {
    Write-Host "filesystem:      $($volume.FileSystem)"
    if ($volume.FileSystem -ne 'NTFS') {
        throw "Chromium requires NTFS; $volumeRoot uses $($volume.FileSystem)."
    }
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
    throw 'Visual Studio Installer (vswhere.exe) is missing.'
}
$visualStudioJson = & $vswhere -latest -products '*' -requires `
    Microsoft.VisualStudio.Workload.NativeDesktop `
    Microsoft.VisualStudio.Component.VC.ATLMFC `
    -format json
$visualStudio = @($visualStudioJson | ConvertFrom-Json) | Select-Object -First 1
if (-not $visualStudio) {
    throw 'Visual Studio 2026 with the Desktop development with C++ workload is missing.'
}
$visualStudioMajor = [int]($visualStudio.installationVersion -split '\.')[0]
if ($visualStudioMajor -lt 18) {
    throw "Visual Studio 2026 or newer is required; found $($visualStudio.displayName)."
}
Write-Host "Visual Studio:   $($visualStudio.displayName) $($visualStudio.catalog.productDisplayVersion)"
Write-Host "VS path:         $($visualStudio.installationPath)"

$sdkIncludeRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Include'
$sdkVersions = @(Get-ChildItem -LiteralPath $sdkIncludeRoot -Directory -ErrorAction SilentlyContinue |
    ForEach-Object { try { [version]$_.Name } catch {} } |
    Sort-Object -Descending)
if ($sdkVersions.Count -eq 0) {
    throw 'Windows 11 SDK is missing.'
}
$requiredSdkFamily = [version]'10.0.28000.0'
if ($sdkVersions[0] -lt $requiredSdkFamily) {
    throw "Windows 11 SDK $requiredSdkFamily or newer is required; found $($sdkVersions[0])."
}
$sdkResourceCompiler = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\bin\$($sdkVersions[0])\x64\rc.exe"
if (-not (Test-Path -LiteralPath $sdkResourceCompiler -PathType Leaf)) {
    throw "Windows SDK x64 tools are missing: $sdkResourceCompiler"
}
$sdkBuildVersion = [version](Get-Item -LiteralPath $sdkResourceCompiler).VersionInfo.ProductVersion.Split(' ')[0]
$requiredSdkBuild = [version]'10.0.28000.2270'
if ($sdkBuildVersion -lt $requiredSdkBuild) {
    throw "Windows 11 SDK $requiredSdkBuild or newer is required; found $sdkBuildVersion."
}
Write-Host "Windows SDK:     $sdkBuildVersion"

$debuggerPath = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Debuggers\x64\cdb.exe'
if (-not (Test-Path -LiteralPath $debuggerPath -PathType Leaf)) {
    throw 'Windows SDK Debugging Tools for x64 are missing.'
}
$debuggerVersionText = (Get-Item -LiteralPath $debuggerPath).VersionInfo.ProductVersion.Split(' ')[0]
$debuggerVersion = [version]$debuggerVersionText
$requiredDebugger = [version]'10.0.26100.3323'
if ($debuggerVersion -lt $requiredDebugger) {
    throw "Windows SDK Debugging Tools $requiredDebugger or newer are required; found $debuggerVersion."
}
Write-Host "Debugging Tools: $debuggerVersion"

$gclient = Join-Path $script:DepotToolsDir 'gclient.bat'
if (Test-Path -LiteralPath $gclient -PathType Leaf) {
    Write-Host 'depot_tools:    ready'
    Add-DepotToolsToPath
    $python = Get-Command python3.bat -ErrorAction SilentlyContinue
    if ($python) {
        Write-Host "depot Python:    $($python.Source)"
    }
} else {
    Write-Host 'depot_tools:    not installed yet'
}

if (Test-Path -LiteralPath (Join-Path $script:ChromiumSrc 'BUILD.gn') -PathType Leaf) {
    Write-Host 'checkout:       ready'
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $shallowOutput = @(& git -C $script:ChromiumSrc rev-parse --is-shallow-repository 2>&1)
        $gitExitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousPreference
    }
    if ($gitExitCode -ne 0) {
        throw "Cannot inspect the Chromium Git checkout: $($shallowOutput -join [Environment]::NewLine)"
    }
    Write-Host "shallow source: $($shallowOutput -join '')"
} else {
    Write-Host 'checkout:       not installed yet'
}
