[CmdletBinding()]
param(
    [switch] $List,
    [switch] $IncludeUnbuilt
)

. (Join-Path $PSScriptRoot 'common.ps1')

Assert-YeeOutputSelection
Add-DepotToolsToPath
Assert-ChromiumSrc
$null = Get-YeeBuildJobs
if (-not (Test-Path -LiteralPath (Join-Path $script:YeeOutDir 'build.ninja') -PathType Leaf)) {
    throw "기존 build.ninja가 필요하다: $script:YeeOutDir. 먼저 configure.ps1을 실행하라."
}

$python = Get-DepotCommand -Name python3
$preflight = Join-Path $PSScriptRoot 'lib\compile_preflight.py'
$planArguments = @('plan', $script:ChromiumSrc, $script:YeeOutDir)
if ($IncludeUnbuilt) { $planArguments += '--include-unbuilt' }
if ($List) {
    & $python $preflight @planArguments --list
    exit $LASTEXITCODE
}

Assert-FreeGiB -RequiredGiB 15 -Purpose 'Chromium 이식 사전 컴파일'
# 패치 적용으로 전환하지 않도록 기존 owned 소스 설치를 요구한다.
if (-not (Test-Path -LiteralPath (Join-Path $script:ChromiumSrc 'chrome\browser\ui\views\yee\BUILD.gn') -PathType Leaf)) {
    throw '설치된 Yee owned 소스가 없다. 먼저 configure.ps1을 실행하라.'
}
Sync-YeeUiSources
& $python $preflight check-graph $script:YeeOutDir
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$logDirectory = Join-Path $script:LocalBuildRoot ("logs\compile-preflight.{0}" -f [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $logDirectory | Out-Null
Write-Host "사전 컴파일 기록: $logDirectory"
& $python $preflight @planArguments --save-to $logDirectory
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$targets = @(Get-Content -LiteralPath (Join-Path $logDirectory 'targets.txt') -Encoding UTF8 |
    Where-Object { $_ })
if ($targets.Count -eq 0) {
    [Console]::Error.WriteLine('이 graph에서 컴파일할 대상이 없다. 다른 플랫폼·구성을 점검하라.')
    exit 16
}

$buildLog = Join-Path $logDirectory 'build.log'
$utf8 = [System.Text.UTF8Encoding]::new($false)
[System.IO.File]::WriteAllText($buildLog, '', $utf8)
$buildResult = 0
try {
    & { Invoke-YeeBuildTarget -Target $targets -KeepGoing } *>&1 | ForEach-Object {
        $line = $_.ToString()
        [System.IO.File]::AppendAllText($buildLog, $line + [Environment]::NewLine, $utf8)
        Write-Host $line
    }
} catch {
    $buildResult = if ($_.Exception.Data.Contains('ExitCode')) { $_.Exception.Data['ExitCode'] } else { 1 }
    [System.IO.File]::AppendAllText($buildLog, $_.ToString() + [Environment]::NewLine, $utf8)
    [Console]::Error.WriteLine($_.ToString())
}
if ($buildResult -eq 0) {
    & $python $preflight summary $logDirectory
    $buildResult = $LASTEXITCODE
} else {
    & $python $preflight failures $logDirectory
    [Console]::Error.WriteLine("사전 컴파일 실패 (종료 코드 $buildResult). 기록: $logDirectory")
}
Write-Host "사전 컴파일 기록: $logDirectory"
exit $buildResult
