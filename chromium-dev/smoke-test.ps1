[CmdletBinding()]
param()

. (Join-Path $PSScriptRoot 'common.ps1')

$timeoutSeconds = if ($env:YEE_SMOKE_TIMEOUT_SECONDS) {
    [int]$env:YEE_SMOKE_TIMEOUT_SECONDS
} else {
    30
}
$smokeUrl = 'data:text/html,%3Ctitle%3EYEE_SMOKE_OK%3C%2Ftitle%3E%3Cmain%20id%3D%22yee-smoke%22%3EYEE_SMOKE_OK%3C%2Fmain%3E'

if (-not (Test-Path -LiteralPath $script:YeeBrowserBin -PathType Leaf)) {
    throw "Built Yee browser is missing: $script:YeeBrowserBin. Run .\chromium-dev\build.ps1 first."
}
if ($timeoutSeconds -lt 1) {
    throw 'YEE_SMOKE_TIMEOUT_SECONDS must be a positive integer.'
}

$smokeProfile = Join-Path ([System.IO.Path]::GetTempPath()) ("yee-chromium-smoke-{0}" -f [guid]::NewGuid().ToString('N'))
$stdoutFile = Join-Path $smokeProfile 'chromium.stdout'
$stderrFile = Join-Path $smokeProfile 'chromium.stderr'
$devToolsPortFile = Join-Path $smokeProfile 'DevToolsActivePort'
$process = $null

New-Item -ItemType Directory -Path $smokeProfile -Force | Out-Null
try {
    # Some cross-platform launchers can pass both Path and PATH into a Windows
    # process. PowerShell's Start-Process treats those names case-insensitively
    # and otherwise throws before Chromium starts. Rebuild one canonical entry.
    $processPath = $env:Path
    [Environment]::SetEnvironmentVariable('PATH', $null, [EnvironmentVariableTarget]::Process)
    [Environment]::SetEnvironmentVariable('Path', $null, [EnvironmentVariableTarget]::Process)
    [Environment]::SetEnvironmentVariable('Path', $processPath, [EnvironmentVariableTarget]::Process)

    $versionInfo = (Get-Item -LiteralPath $script:YeeBrowserBin).VersionInfo
    Write-Host "Binary: $($versionInfo.FileDescription) $($versionInfo.ProductVersion)"
    $arguments = @(
        '--headless',
        '--disable-background-networking',
        '--disable-component-update',
        '--disable-default-apps',
        '--disable-sync',
        '--metrics-recording-only',
        '--no-first-run',
        '--no-default-browser-check',
        "--user-data-dir=`"$smokeProfile`"",
        '--remote-debugging-port=0',
        $smokeUrl
    )
    $process = Start-Process -FilePath $script:YeeBrowserBin `
        -ArgumentList $arguments `
        -RedirectStandardOutput $stdoutFile `
        -RedirectStandardError $stderrFile `
        -PassThru `
        -WindowStyle Hidden

    Write-Host "Waiting for the DevTools endpoint (timeout: ${timeoutSeconds}s)."
    $deadline = [DateTime]::UtcNow.AddSeconds($timeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        if ($process.HasExited) {
            $stderr = if (Test-Path -LiteralPath $stderrFile) {
                Get-Content -LiteralPath $stderrFile -Raw -ErrorAction SilentlyContinue
            } else { '' }
            throw "Chromium exited before the smoke target was ready (status: $($process.ExitCode)).`n$stderr"
        }

        if (Test-Path -LiteralPath $devToolsPortFile -PathType Leaf) {
            $port = (Get-Content -LiteralPath $devToolsPortFile -TotalCount 1).Trim()
            $numericPort = 0
            if ([int]::TryParse($port, [ref]$numericPort)) {
                try {
                    $targets = Invoke-RestMethod -Uri "http://127.0.0.1:$numericPort/json/list" -TimeoutSec 2
                    $target = @($targets) | Where-Object {
                        $_.type -eq 'page' -and $_.title -eq 'YEE_SMOKE_OK'
                    } | Select-Object -First 1
                    if ($target) {
                        Write-Host 'PASS: browser process, DevTools endpoint, and renderer smoke page are ready.'
                        return
                    }
                } catch {
                    # DevToolsActivePort can appear just before the HTTP endpoint is ready.
                }
            }
        }

        Start-Sleep -Milliseconds 200
    }

    $stderr = if (Test-Path -LiteralPath $stderrFile) {
        Get-Content -LiteralPath $stderrFile -Raw -ErrorAction SilentlyContinue
    } else { '' }
    throw "Timed out waiting for the DevTools smoke target after $timeoutSeconds seconds.`n$stderr"
} finally {
    if ($process -and -not $process.HasExited) {
        & taskkill.exe /PID $process.Id /T /F 2>$null | Out-Null
        $process.WaitForExit(5000) | Out-Null
    }
    if (Test-Path -LiteralPath $smokeProfile -PathType Container) {
        $removed = $false
        for ($attempt = 1; $attempt -le 20; $attempt++) {
            try {
                Remove-Item -LiteralPath $smokeProfile -Recurse -Force -ErrorAction Stop
                $removed = $true
                break
            } catch {
                Start-Sleep -Milliseconds 250
            }
        }
        if (-not $removed) {
            Write-Warning "Smoke test passed, but Windows still holds files in the temporary profile: $smokeProfile"
        }
    }
}
