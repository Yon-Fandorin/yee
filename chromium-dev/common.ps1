Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:WindowsDevDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$script:YeeRoot = Split-Path -Parent $script:WindowsDevDir
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
$script:YeeArgsFile = Join-Path $script:YeeRoot 'chromium-overlay\args.gn'
$script:YeeBuildJobs = if ($env:YEE_BUILD_JOBS) { $env:YEE_BUILD_JOBS } else { '2' }

function Get-AvailableGiB {
    $probePath = $script:LocalBuildRoot
    while (-not (Test-Path -LiteralPath $probePath)) {
        $parent = Split-Path -Parent $probePath
        if (-not $parent -or $parent -eq $probePath) {
            break
        }
        $probePath = $parent
    }

    $root = [System.IO.Path]::GetPathRoot([System.IO.Path]::GetFullPath($probePath))
    $drive = New-Object System.IO.DriveInfo $root
    return [Math]::Floor($drive.AvailableFreeSpace / 1GB)
}

function Assert-FreeGiB {
    param(
        [Parameter(Mandatory = $true)][int] $RequiredGiB,
        [Parameter(Mandatory = $true)][string] $Purpose
    )

    $available = Get-AvailableGiB
    if ($available -lt $RequiredGiB) {
        throw "Need at least $RequiredGiB GiB free for $Purpose; $available GiB is available."
    }
    Write-Host "Disk guard: $available GiB free ($RequiredGiB GiB required for $Purpose)."
}

function Add-DepotToolsToPath {
    $gclient = Join-Path $script:DepotToolsDir 'gclient.bat'
    if (-not (Test-Path -LiteralPath $gclient -PathType Leaf)) {
        throw "depot_tools is missing. Run .\chromium-dev\checkout.ps1 first. Expected: $gclient"
    }

    $pythonBinDir = $null
    $pythonRelDirFile = Join-Path $script:DepotToolsDir 'python3_bin_reldir.txt'
    if (Test-Path -LiteralPath $pythonRelDirFile -PathType Leaf) {
        $pythonRelDir = (Get-Content -LiteralPath $pythonRelDirFile -Raw).Trim()
        if ($pythonRelDir) {
            $candidate = Join-Path $script:DepotToolsDir $pythonRelDir
            if (Test-Path -LiteralPath (Join-Path $candidate 'python3.exe') -PathType Leaf) {
                $pythonBinDir = [System.IO.Path]::GetFullPath($candidate)
            }
        }
    }

    # Windows app execution aliases can expose a zero-byte python3.exe under
    # WindowsApps. Put depot_tools' real Python executable first so GN/Siso
    # subprocesses do not select that alias instead of Chromium's toolchain.
    $pathEntries = @($env:Path -split ';' | Where-Object {
        $_ -and $_ -ne $script:DepotToolsDir -and $_ -ne $pythonBinDir
    })
    $prefixEntries = @($pythonBinDir, $script:DepotToolsDir) | Where-Object { $_ }
    $env:Path = (@($prefixEntries) + $pathEntries) -join ';'
    $env:DEPOT_TOOLS_WIN_TOOLCHAIN = '0'
}

function Assert-ChromiumSrc {
    if (-not (Test-Path -LiteralPath (Join-Path $script:ChromiumSrc 'BUILD.gn') -PathType Leaf)) {
        throw "Chromium source is missing. Run .\chromium-dev\checkout.ps1 first. Expected: $script:ChromiumSrc"
    }
}

function Get-DepotCommand {
    param([Parameter(Mandatory = $true)][string] $Name)

    $batchPath = Join-Path $script:DepotToolsDir "$Name.bat"
    if (Test-Path -LiteralPath $batchPath -PathType Leaf) {
        return $batchPath
    }

    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if (-not $command) {
        throw "Cannot find depot_tools command: $Name"
    }
    return $command.Source
}

function Get-GnArguments {
    $arguments = Get-Content -LiteralPath $script:YeeArgsFile -Encoding UTF8 |
        ForEach-Object { $_.Trim() } |
        Where-Object { $_ -and -not $_.StartsWith('#') }
    return ($arguments -join ' ')
}

function Write-YeePaths {
    Write-Host "yee root:       $script:YeeRoot"
    Write-Host "local data:     $script:LocalBuildRoot"
    Write-Host "depot_tools:    $script:DepotToolsDir"
    Write-Host "Chromium src:   $script:ChromiumSrc"
    Write-Host "build output:   $script:YeeOutDir"
}

function Set-YeeCacheEnvironment {
    $cacheRoot = Join-Path $script:LocalBuildRoot 'cache'
    $env:XDG_CACHE_HOME = $cacheRoot
    $env:CLANG_MODULE_CACHE_PATH = Join-Path $cacheRoot 'clang\ModuleCache'
    $env:GOCACHE = Join-Path $cacheRoot 'go-build'
    $env:GOMODCACHE = Join-Path $cacheRoot 'go-mod'
    $env:CARGO_HOME = Join-Path $cacheRoot 'cargo'
    $env:npm_config_cache = Join-Path $cacheRoot 'npm'
    $env:PIP_CACHE_DIR = Join-Path $cacheRoot 'pip'

    foreach ($directory in @(
        $env:CLANG_MODULE_CACHE_PATH,
        $env:GOCACHE,
        $env:GOMODCACHE,
        $env:CARGO_HOME,
        $env:npm_config_cache,
        $env:PIP_CACHE_DIR
    )) {
        New-Item -ItemType Directory -Path $directory -Force | Out-Null
    }
}

function Get-RecommendedBuildJobs {
    $processor = Get-CimInstance Win32_Processor | Select-Object -First 1
    $operatingSystem = Get-CimInstance Win32_OperatingSystem
    $totalRamGiB = $operatingSystem.TotalVisibleMemorySize / 1MB
    # Chromium's largest clang/link steps and Siso's file-state cache can use
    # several GiB each. Eight GiB per job kept this 32 GiB workstation out of
    # paging, while six jobs left less than 2 GiB available in an observed run.
    $memoryBoundJobs = [Math]::Max(1, [Math]::Ceiling($totalRamGiB / 8))
    return [Math]::Max(1, [Math]::Min($processor.NumberOfCores, $memoryBoundJobs))
}
