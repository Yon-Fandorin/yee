# Build helpers; loaded by common.ps1.

function Get-GnArguments {
    $arguments = Get-Content -LiteralPath $script:YeeArgsFile -Encoding UTF8 |
        ForEach-Object { $_.Trim() } |
        Where-Object { $_ -and -not $_.StartsWith('#') }
    return ($arguments -join ' ')
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

function Get-YeeBuildJobs {
    $jobs = 0
    if (-not [int]::TryParse($script:YeeBuildJobs, [ref]$jobs) -or $jobs -lt 1) {
        throw "YEE_BUILD_JOBS must be a positive integer: $script:YeeBuildJobs"
    }
    return $jobs
}

function Sync-YeeBranding {
    & (Join-Path $script:YeeRoot 'tools\overlay\install-branding.ps1') `
        -ChromiumSrc $script:ChromiumSrc
}

function Sync-YeeUiSources {
    $yeeUiBuildFile = Join-Path $script:ChromiumSrc 'chrome\browser\ui\views\yee\BUILD.gn'
    if (Test-Path -LiteralPath $yeeUiBuildFile -PathType Leaf) {
        & (Join-Path $script:YeeRoot 'tools\overlay\install-yee-ui-sources.ps1') `
            -ChromiumSrc $script:ChromiumSrc
    } else {
        & (Join-Path $script:YeeRoot 'tools\overlay\apply.ps1') `
            -ChromiumSrc $script:ChromiumSrc
    }
}

function Initialize-YeeBuildInputs {
    $null = Get-YeeBuildJobs
    Sync-YeeBranding
    Sync-YeeUiSources
    if (-not (Test-Path -LiteralPath (Join-Path $script:YeeOutDir 'build.ninja') -PathType Leaf)) {
        & (Join-Path $script:WindowsDevDir 'configure.ps1')
    }
}

function Invoke-YeeBuildTarget {
    param([Parameter(Mandatory = $true)][string] $Target)

    $jobs = Get-YeeBuildJobs
    Set-YeeCacheEnvironment
    $env:NINJA_SUMMARIZE_BUILD = '1'
    $autoninja = Get-DepotCommand -Name 'autoninja'
    Push-Location $script:ChromiumSrc
    try {
        Write-Host "Building $Target with $jobs parallel jobs."
        & $autoninja -C "out\$script:YeeOutName" -j $jobs $Target
        if ($LASTEXITCODE -ne 0) { throw "Chromium $Target build failed." }
    } finally {
        Pop-Location
    }
}
