# Preflight helpers; loaded by common.ps1.

function Assert-YeeBuildRootSelection {
    if ($script:YeeLocalBuildRootExplicit -or $script:YeeChromiumRootExplicit) { return }
    if ((Test-Path -LiteralPath (Join-Path $script:DepotToolsDir 'gclient.bat') -PathType Leaf) -and
        (Test-Path -LiteralPath (Join-Path $script:ChromiumSrc 'BUILD.gn') -PathType Leaf)) { return }

    $defaultRoot = Join-Path $script:YeeRoot '.local-build'
    $candidates = @(Get-ChildItem -LiteralPath $defaultRoot -Directory -ErrorAction SilentlyContinue |
        Where-Object {
            (Test-Path -LiteralPath (Join-Path $_.FullName 'chromium\src\BUILD.gn') -PathType Leaf) -or
            (Test-Path -LiteralPath (Join-Path $_.FullName 'depot_tools\gclient.bat') -PathType Leaf)
        } | Sort-Object FullName)
    if ($candidates.Count -eq 0) { return }

    [Console]::Error.WriteLine('환경 미설정: 빌드 루트가 지정되지 않았고 기본 루트의 개발 입력이 누락되었다.')
    [Console]::Error.WriteLine('후보 빌드 루트:')
    foreach ($candidate in $candidates) { [Console]::Error.WriteLine("  $($candidate.FullName)") }
    $example = $candidates[0].FullName.Replace("'", "''")
    [Console]::Error.WriteLine("사용할 루트를 확인해 지정하라: `$env:YEE_LOCAL_BUILD_ROOT = '$example'")
    [Console]::Error.WriteLine('저장소 밖 루트는 검색하지 않는다. 기존 원본은 $env:YEE_CHROMIUM_ROOT로 지정할 수 있다.')
    [Console]::Error.WriteLine('출력 폴더도 $env:YEE_OUT_NAME으로 지정하라.')
    $defaultExample = $defaultRoot.Replace("'", "''")
    [Console]::Error.WriteLine("기본 루트를 쓰려면: `$env:YEE_LOCAL_BUILD_ROOT = '$defaultExample'")
    exit 17
}

function Assert-YeeOutputSelection {
    Assert-YeeBuildRootSelection
    if ($script:YeeOutNameExplicit -or
        (Test-Path -LiteralPath (Join-Path $script:YeeOutDir 'build.ninja') -PathType Leaf)) { return }

    $candidates = @(Get-ChildItem -LiteralPath (Join-Path $script:ChromiumSrc 'out') -Directory -ErrorAction SilentlyContinue |
        Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'build.ninja') -PathType Leaf } |
        Sort-Object FullName)
    if ($candidates.Count -eq 0) { return }

    [Console]::Error.WriteLine("환경 미설정: YEE_OUT_NAME이 없고 기본 출력에 build.ninja가 없다: $script:YeeOutDir")
    [Console]::Error.WriteLine('후보 출력:')
    foreach ($candidate in $candidates) { [Console]::Error.WriteLine("  $($candidate.FullName)") }
    $rootExample = $script:ChromiumRoot.Replace("'", "''")
    $outputExample = $candidates[0].Name.Replace("'", "''")
    [Console]::Error.WriteLine("기존 원본을 유지하라: `$env:YEE_CHROMIUM_ROOT = '$rootExample'")
    [Console]::Error.WriteLine("사용할 출력을 확인해 지정하라: `$env:YEE_OUT_NAME = '$outputExample'")
    exit 18
}

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
    Assert-YeeBuildRootSelection
    $gclient = Join-Path $script:DepotToolsDir 'gclient.bat'
    if (-not (Test-Path -LiteralPath $gclient -PathType Leaf)) {
        throw "depot_tools is missing. Run .\tools\dev\checkout.ps1 first. Expected: $gclient"
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
    Assert-YeeBuildRootSelection
    if (-not (Test-Path -LiteralPath (Join-Path $script:ChromiumSrc 'BUILD.gn') -PathType Leaf)) {
        throw "Chromium source is missing. Run .\tools\dev\checkout.ps1 first. Expected: $script:ChromiumSrc"
    }
    $python = Get-DepotCommand -Name 'python3'
    & $python (Join-Path $script:WindowsDevDir 'chromium-version.py') verify $script:ChromiumSrc --platform windows
    if ($LASTEXITCODE -ne 0) { throw 'Chromium 기준 검사에 실패했다. Stable 업그레이드 절차를 확인한다.' }
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

function Get-ChromiumPinnedValue {
    param([Parameter(Mandatory = $true)][ValidateSet('version','revision','source_url')][string] $Name)
    $python = Get-DepotCommand -Name 'python3'
    $value = & $python (Join-Path $script:WindowsDevDir 'chromium-version.py') show --get $Name
    if ($LASTEXITCODE -ne 0) { throw 'Chromium 공통 기준을 읽지 못했다.' }
    return ($value -join "`n").Trim()
}

function Get-YeeSyncJobs {
    $jobs = 0
    if (-not [int]::TryParse($script:YeeSyncJobs, [ref]$jobs) -or $jobs -lt 1) {
        throw "YEE_SYNC_JOBS는 양의 정수여야 한다: $script:YeeSyncJobs"
    }
    return $jobs
}
