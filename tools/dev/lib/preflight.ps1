# Preflight helpers; loaded by common.ps1.

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
    if (-not (Test-Path -LiteralPath (Join-Path $script:ChromiumSrc 'BUILD.gn') -PathType Leaf)) {
        throw "Chromium source is missing. Run .\tools\dev\checkout.ps1 first. Expected: $script:ChromiumSrc"
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
