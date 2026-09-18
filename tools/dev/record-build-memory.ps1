[CmdletBinding()]
param(
    [ValidateRange(5, 3600)]
    [int] $IntervalSeconds = 30,

    [switch] $Once,

    [string] $SessionName = ''
)

. (Join-Path $PSScriptRoot 'common.ps1')

$memoryRoot = Join-Path $script:YeeRoot '.local-exclude\build-memory'
$logPath = if ($env:YEE_BUILD_MEMORY_LOG) {
    [System.IO.Path]::GetFullPath($env:YEE_BUILD_MEMORY_LOG)
} else {
    Join-Path $memoryRoot 'build-memory.jsonl'
}
$machinePath = Join-Path $memoryRoot 'machine.json'

New-Item -ItemType Directory -Path $memoryRoot -Force | Out-Null
New-Item -ItemType Directory -Path (Split-Path -Parent $logPath) -Force | Out-Null

$processor = Get-CimInstance Win32_Processor | Select-Object -First 1
$operatingSystem = Get-CimInstance Win32_OperatingSystem
$machineRecord = [ordered]@{
    updated_at = [DateTimeOffset]::Now.ToString('o')
    computer_name = $env:COMPUTERNAME
    cpu = $processor.Name.Trim()
    physical_cores = [int]$processor.NumberOfCores
    logical_cores = [int]$processor.NumberOfLogicalProcessors
    total_ram_gib = [Math]::Round($operatingSystem.TotalVisibleMemorySize / 1MB, 2)
    recommended_build_jobs = Get-RecommendedBuildJobs
    chromium_src = $script:ChromiumSrc
    output_dir = $script:YeeOutDir
}
$machineRecord | ConvertTo-Json | Set-Content -LiteralPath $machinePath -Encoding UTF8

if (-not $SessionName) {
    $SessionName = "chrome-$([DateTime]::Now.ToString('yyyyMMdd-HHmmss'))"
}

function Get-SisoProcess {
    $pidFile = Join-Path $script:YeeOutDir '.siso_lock.pid'
    if (-not (Test-Path -LiteralPath $pidFile -PathType Leaf)) {
        return $null
    }

    $pidText = Get-Content -LiteralPath $pidFile -Raw -ErrorAction SilentlyContinue
    if ($pidText -notmatch 'pid=(\d+)') {
        return $null
    }
    return Get-Process -Id ([int]$Matches[1]) -ErrorAction SilentlyContinue
}

function Get-CompletedRecordCount {
    $metricsPath = Join-Path $script:YeeOutDir 'siso_metrics.json'
    if (-not (Test-Path -LiteralPath $metricsPath -PathType Leaf)) {
        return 0
    }
    $lineCount = (Get-Content -LiteralPath $metricsPath | Measure-Object -Line).Lines
    return [Math]::Max(0, $lineCount - 2)
}

$sawActiveBuild = $false
do {
    $operatingSystem = Get-CimInstance Win32_OperatingSystem
    $siso = Get-SisoProcess
    $buildProcesses = @()
    if ($siso) {
        $sawActiveBuild = $true
        $childIds = @(Get-CimInstance Win32_Process |
            Where-Object { $_.ParentProcessId -eq $siso.Id } |
            Select-Object -ExpandProperty ProcessId)
        $buildProcesses = @($siso) + @(Get-Process -Id $childIds -ErrorAction SilentlyContinue)
    }

    $buildWorkingSet = if ($buildProcesses.Count -gt 0) {
        ($buildProcesses | Measure-Object -Property WorkingSet64 -Sum).Sum
    } else {
        0
    }

    $driveRoot = [System.IO.Path]::GetPathRoot($script:YeeOutDir)
    $drive = New-Object System.IO.DriveInfo $driveRoot
    $record = [ordered]@{
        timestamp = [DateTimeOffset]::Now.ToString('o')
        session = $SessionName
        build_active = [bool]$siso
        siso_pid = if ($siso) { $siso.Id } else { $null }
        completed_records = Get-CompletedRecordCount
        total_ram_gib = [Math]::Round($operatingSystem.TotalVisibleMemorySize / 1MB, 2)
        used_ram_gib = [Math]::Round(($operatingSystem.TotalVisibleMemorySize - $operatingSystem.FreePhysicalMemory) / 1MB, 2)
        free_ram_gib = [Math]::Round($operatingSystem.FreePhysicalMemory / 1MB, 2)
        build_working_set_gib = [Math]::Round($buildWorkingSet / 1GB, 2)
        output_drive_free_gib = [Math]::Round($drive.AvailableFreeSpace / 1GB, 2)
        output_dir = $script:YeeOutDir
    }
    $record | ConvertTo-Json -Compress | Add-Content -LiteralPath $logPath -Encoding UTF8
    Write-Host ("{0} active={1} completed={2} free_ram={3}GiB build_ram={4}GiB" -f `
        $record.timestamp, $record.build_active, $record.completed_records,
        $record.free_ram_gib, $record.build_working_set_gib)

    if ($Once -or ($sawActiveBuild -and -not $siso)) {
        break
    }
    Start-Sleep -Seconds $IntervalSeconds
} while ($true)

Write-Host "Build memory log: $logPath"
Write-Host "Machine profile:  $machinePath"
