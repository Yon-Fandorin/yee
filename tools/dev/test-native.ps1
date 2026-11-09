[CmdletBinding()]
param(
    [string] $Gate = 'required',
    [string] $Mode,
    [ValidateSet('background', 'foreground', 'all')]
    [string] $Tier = 'background',
    [string] $Filter,
    [switch] $PrintFilters,
    [switch] $List,
    [switch] $StartOnDesktop,
    [switch] $NoBuild,
    [string] $ProvenanceFile
)

. (Join-Path $PSScriptRoot 'common.ps1')

$Tier = $Tier.ToLowerInvariant()
$Gate = $Gate.ToLowerInvariant()
if ($Mode) { $Mode = $Mode.ToLowerInvariant() }

if ($NoBuild.IsPresent -ne [bool]$ProvenanceFile) {
    throw '-NoBuild는 빌드 성공을 기록한 -ProvenanceFile과 함께 지정해야 합니다.'
}
if ($PrintFilters -and ($List -or $NoBuild)) {
    throw '-PrintFilters와 실행 옵션을 함께 지정할 수 없습니다.'
}
if ($StartOnDesktop -and ($List -or $PrintFilters)) {
    throw '-StartOnDesktop은 전면 검사 실행에 지정해야 합니다.'
}
if ($Gate -eq 'internal-pages') {
    if (!$Mode -and $env:YEE_TEST_UI -eq 'on') { $Mode = 'ui-on' }
    if ($Mode -eq 'ui-off') { $Mode = 'default' }
    if ($Mode -eq 'ui-on' -and !$PSBoundParameters.ContainsKey('Tier')) { $Tier = 'all' }
}
if ($StartOnDesktop -and $Tier -eq 'background') {
    throw '-StartOnDesktop은 전면 검사 실행에 지정해야 합니다.'
}

$env:PYTHONUTF8 = '1'
$env:PYTHONDONTWRITEBYTECODE = '1'
$python = Get-DepotCommand -Name 'python3'
$runner = Join-Path $PSScriptRoot 'lib\windows_native_gate.py'
$arguments = @('--repo', $script:YeeRoot, '--gate', $Gate, '--tier', $Tier)
if ($Mode) { $arguments += @('--mode', $Mode) }
if ($PSBoundParameters.ContainsKey('Filter')) { $arguments += "--filter=$Filter" }
$plan = & $python -B $runner @arguments --print-filters
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
if ($PrintFilters) { $plan; exit 0 }
if ($StartOnDesktop -and ($plan -join [Environment]::NewLine) -notmatch '"--enable-pixel-output-in-tests"') {
    throw '-StartOnDesktop을 사용할 전면 검사가 없습니다.'
}
if (($plan -join [Environment]::NewLine).Trim() -eq '[]') {
    Write-Host '선택한 tier에서 실행하는 사례가 없습니다.'
    exit 0
}

Assert-YeeOutputSelection
Add-DepotToolsToPath
Assert-ChromiumSrc
if (!(Test-Path -LiteralPath (Join-Path $script:YeeOutDir 'build.ninja') -PathType Leaf)) {
    throw '검사에 사용할 graph가 필요합니다. 먼저 configure.ps1을 실행하세요.'
}
$jobs = Get-YeeBuildJobs
$arguments += @('--chromium', $script:ChromiumSrc, '--out-name', $script:YeeOutName,
    '--local-build', $script:LocalBuildRoot, '--depot-tools', $script:DepotToolsDir,
    '--jobs', $jobs)
if ($List) { $arguments += '--list-only' }
if ($StartOnDesktop) { $arguments += '--start-on-desktop' }
if ($NoBuild) {
    $arguments += @('--provenance', (Get-Item -LiteralPath $ProvenanceFile -ErrorAction Stop).FullName)
}

# 같은 데스크톱에서 실행 중인 gate를 직렬화한다.
$mutex = [Threading.Mutex]::new($false, 'Local\YeeWindowsNativeGate')
$locked = $false
try {
    try {
        $locked = $mutex.WaitOne(0)
    } catch [Threading.AbandonedMutexException] {
        $locked = $true
    }
    if (!$locked) { throw '같은 데스크톱에서 Windows native gate가 이미 실행 중입니다.' }
    $outputPrefix = [IO.Path]::GetFullPath($script:YeeOutDir).TrimEnd('\') + '\'
    $active = @(Get-CimInstance Win32_Process -Property ProcessId,ExecutablePath |
        Where-Object { $_.ExecutablePath -and $_.ExecutablePath.StartsWith(
            $outputPrefix, [StringComparison]::OrdinalIgnoreCase) } |
        Select-Object ProcessId,ExecutablePath)
    if ($active.Count) {
        throw ('검사 출력의 프로세스가 남아 있습니다. ' + ($active | ConvertTo-Json -Compress))
    }
    . (Join-Path $script:YeeRoot 'tools/overlay/lib/overlay-tools.ps1')
    $pending = @(Get-YeeOverlayPatchPlan -ChromiumSrc $script:ChromiumSrc -RepoRoot $script:YeeRoot |
        Where-Object Pending)
    if ($pending.Count) {
        throw '현재 overlay 패치가 적용된 graph가 필요합니다. configure.ps1을 실행하세요.'
    }
    if (!$NoBuild) {
        Assert-FreeGiB -RequiredGiB 15 -Purpose 'Windows native gates'
        Initialize-YeeBuildInputs
        Set-YeeCacheEnvironment
        $env:NINJA_SUMMARIZE_BUILD = '1'
    }
    $realPython = (Get-Command python3.exe -CommandType Application -ErrorAction Stop |
        Select-Object -First 1).Source
    & $realPython -B $runner @arguments
    $gateExitCode = $LASTEXITCODE
} finally {
    if ($locked) { $mutex.ReleaseMutex() }
    $mutex.Dispose()
}
exit $gateExitCode
