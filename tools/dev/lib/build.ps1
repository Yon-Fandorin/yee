# Build helpers; loaded by common.ps1.

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
    & (Join-Path $script:YeeRoot 'tools\overlay\install-brand-assets.ps1') `
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
    Assert-YeeOutputSelection
    $null = Get-YeeBuildJobs
    Sync-YeeBranding
    Sync-YeeUiSources
    if (-not (Test-Path -LiteralPath (Join-Path $script:YeeOutDir 'build.ninja') -PathType Leaf)) {
        & (Join-Path $script:WindowsDevDir 'configure.ps1')
    }
}

function Invoke-YeeBuildTarget {
    param(
        [Parameter(Mandatory = $true)][string[]] $Target,
        [switch] $KeepGoing
    )

    Assert-YeeOutputSelection
    $jobs = Get-YeeBuildJobs
    Set-YeeCacheEnvironment
    $env:NINJA_SUMMARIZE_BUILD = '1'
    $autoninja = Get-DepotCommand -Name 'autoninja'
    Push-Location $script:ChromiumSrc
    try {
        Write-Host "Building $($Target -join ', ') with $jobs parallel jobs."
        $buildArguments = @('-C', "out\$script:YeeOutName", '-j', $jobs)
        if ($KeepGoing) { $buildArguments += @('-k', '0') }
        $launcher = $autoninja
        if ($Target.Count -gt 1) {
            # 다중 object 목록은 cmd의 8191자 한도를 넘으므로 배치 파일을 거치지 않는다.
            $python = (Get-Command python3.exe -CommandType Application -ErrorAction Stop |
                Select-Object -First 1).Source
            if ((Get-Item -LiteralPath $python).Length -eq 0) {
                throw '실제 depot_tools Python이 필요하다. Add-DepotToolsToPath로 개발 환경을 준비하라.'
            }
            $launcher = $python
            $buildArguments = @((Join-Path $script:DepotToolsDir 'autoninja.py')) + $buildArguments
        }
        # Windows PowerShell 5.1의 native stderr도 경고·로그로 보존하고 종료 코드로 판정한다.
        $previousPreference = $ErrorActionPreference
        try {
            $ErrorActionPreference = 'Continue'
            & $launcher @buildArguments @Target
            $buildExitCode = $LASTEXITCODE
        } finally {
            $ErrorActionPreference = $previousPreference
        }
        if ($buildExitCode -ne 0) {
            $buildError = [System.Exception]::new("Chromium $Target build failed.")
            $buildError.Data['ExitCode'] = $buildExitCode
            throw $buildError
        }
    } finally {
        Pop-Location
    }
}
