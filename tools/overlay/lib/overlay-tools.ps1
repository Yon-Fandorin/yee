# Internal catalog, patch, and source helpers. No Python dependency on Windows.

function Assert-OverlayRelativePath {
    param([Parameter(Mandatory = $true)][string] $Value)
    if (-not $Value -or $Value -match '[\\:*?\[\]\x00\r\n]' -or
        [System.IO.Path]::IsPathRooted($Value) -or
        @($Value.Split('/') | Where-Object { $_ -eq '' -or $_ -eq '.' -or $_ -eq '..' }).Count) {
        throw "Expected a literal relative overlay path: $Value"
    }
}

function Get-YeeOverlayCatalog {
    param([Parameter(Mandatory = $true)][string] $RepoRoot)
    $catalog = Get-Content -LiteralPath (Join-Path $RepoRoot 'build/overlay.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($catalog.schema_version -ne 2) { throw 'Unsupported overlay.json schema.' }
    $roles = @{}
    $files = @{}
    foreach ($patch in $catalog.patches) {
        Assert-OverlayRelativePath $patch.file
        if (-not $patch.role -or $roles.ContainsKey($patch.role) -or $files.ContainsKey($patch.file) -or
            $patch.strategy -notin @('whole', 'sections')) { throw 'Invalid or duplicate overlay patch entry.' }
        $roles[$patch.role] = $true
        $files[$patch.file] = $true
    }
    foreach ($key in @('generated_roots', 'new_glue', 'ignored_source_names')) {
        $seen = @{}
        foreach ($path in $catalog.$key) {
            Assert-OverlayRelativePath $path
            if ($seen.ContainsKey($path)) { throw "Duplicate overlay path: $path" }
            $seen[$path] = $true
        }
    }
    foreach ($binding in $catalog.source_roots) {
        Assert-OverlayRelativePath $binding.source
        Assert-OverlayRelativePath $binding.destination
        if ($binding.source.Split('/')[0] -notin @('browser', 'renderer', 'components', 'third_party')) {
            throw 'Expected a product source ownership directory.'
        }
    }
    foreach ($side in @('source', 'destination')) {
        $roots = @($catalog.source_roots | ForEach-Object { $_.$side })
        if ($side -eq 'destination') { $roots += @($catalog.generated_roots) }
        for ($i = 0; $i -lt $roots.Count; $i++) {
            for ($j = $i + 1; $j -lt $roots.Count; $j++) {
                if ($roots[$i] -eq $roots[$j] -or $roots[$i].StartsWith($roots[$j] + '/') -or
                    $roots[$j].StartsWith($roots[$i] + '/')) { throw 'Overlay ownership roots must not overlap.' }
            }
        }
    }
    return $catalog
}

function Get-YeePatchPaths {
    param([Parameter(Mandatory = $true)][string] $PatchFile)
    $text = [System.IO.File]::ReadAllText($PatchFile)
    $sections = [regex]::Split($text, '(?m)(?=^diff --git )')
    if ($sections.Count -lt 2 -or $sections[0].Trim()) {
        throw "Expected explicit patch paths: $PatchFile"
    }
    $seen = @{}
    $paths = @()
    for ($i = 1; $i -lt $sections.Count; $i++) {
        $lines = $sections[$i] -split '\r?\n'
        $header = [regex]::Match($lines[0], '^diff --git a/(\S+) b/(\S+)$')
        if (-not $header.Success -or $header.Groups[1].Value -cne $header.Groups[2].Value) {
            throw "Expected unique, unchanged patch paths: $PatchFile"
        }
        $path = $header.Groups[2].Value
        Assert-OverlayRelativePath $path
        if ($seen.ContainsKey($path)) {
            throw "Expected unique, unchanged patch paths: $PatchFile"
        }
        $fileHeaders = @()
        $oldRemaining = $newRemaining = 0
        $hasHunk = $hasMode = $false
        foreach ($line in $lines | Select-Object -Skip 1) {
            # 본문의 ---/+++는 파일 헤더가 아니라 삭제·추가한 소스일 수 있다.
            if ($oldRemaining -or $newRemaining) {
                if ($line -ceq '\ No newline at end of file') { continue }
                if ($line.StartsWith(' ')) { $oldRemaining--; $newRemaining-- }
                elseif ($line.StartsWith('-')) { $oldRemaining-- }
                elseif ($line.StartsWith('+')) { $newRemaining-- }
                else { throw "Malformed patch hunk: $PatchFile" }
                if ($oldRemaining -lt 0 -or $newRemaining -lt 0) { throw "Malformed patch hunk: $PatchFile" }
                continue
            }
            if ($fileHeaders.Count -eq 1 -and -not $line.StartsWith('+++ ')) {
                throw "Expected matching patch file headers: $PatchFile"
            }
            if ($line.StartsWith('--- ')) {
                $value = $line.Substring(4)
                if ($hasHunk -or $fileHeaders.Count -or ($value -cne "a/$path" -and $value -cne '/dev/null')) {
                    throw "Expected matching patch file headers: $PatchFile"
                }
                $fileHeaders += $value
            } elseif ($line.StartsWith('+++ ')) {
                $value = $line.Substring(4)
                if ($hasHunk -or $fileHeaders.Count -ne 1 -or
                    ($value -cne "b/$path" -and $value -cne '/dev/null') -or
                    ($fileHeaders[0] -ceq '/dev/null' -and $value -ceq '/dev/null')) {
                    throw "Expected matching patch file headers: $PatchFile"
                }
                $fileHeaders += $value
            } elseif ($line.StartsWith('@@')) {
                $hunk = [regex]::Match($line, '^@@ -\d+(?:,(\d+))? \+\d+(?:,(\d+))? @@.*$')
                if (-not $hunk.Success -or $fileHeaders.Count -ne 2) { throw "Malformed patch hunk: $PatchFile" }
                $oldRemaining = if ($hunk.Groups[1].Success) { [int]$hunk.Groups[1].Value } else { 1 }
                $newRemaining = if ($hunk.Groups[2].Success) { [int]$hunk.Groups[2].Value } else { 1 }
                $hasHunk = $true
            } elseif ($line -cmatch '^(GIT binary patch|Binary files |rename |copy |similarity index |dissimilarity index )') {
                throw "Unsupported binary or rename/copy patch: $PatchFile"
            } elseif (-not $line -or ($hasHunk -and $line -ceq '\ No newline at end of file')) {
                continue
            } elseif (-not $hasHunk -and $line -cmatch '^(old|new|new file|deleted file) mode [0-7]{6}$') {
                $hasMode = $true
            } elseif (-not $hasHunk -and $line -cmatch '^index [0-9a-f]+\.\.[0-9a-f]+( [0-7]{6})?$') {
                continue
            } else { throw "Unsupported patch metadata: $PatchFile" }
        }
        if ($oldRemaining -or $newRemaining -or $fileHeaders.Count -eq 1 -or -not ($hasHunk -or $hasMode)) {
            throw "Incomplete patch section: $PatchFile"
        }
        $seen[$path] = $true
        $paths += $path
    }
    $paths
}

function Invoke-YeeGitApply {
    param(
        [Parameter(Mandatory = $true)][string] $ChromiumSrc,
        [Parameter(Mandatory = $true)][string[]] $Arguments
    )
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = @(& git -C $ChromiumSrc apply @Arguments 2>&1)
        $exitCode = $LASTEXITCODE
    } finally { $ErrorActionPreference = $previousPreference }
    return [pscustomobject]@{ Success = ($exitCode -eq 0); Output = ($output -join "`n") }
}

function Get-YeeOverlayPatchPlan {
    param(
        [Parameter(Mandatory = $true)][string] $ChromiumSrc,
        [Parameter(Mandatory = $true)][string] $RepoRoot,
        [string] $Role
    )
    $catalog = Get-YeeOverlayCatalog $RepoRoot
    $selected = @($catalog.patches | Where-Object { -not $Role -or $_.role -eq $Role })
    if (-not $selected.Count) { throw "Unknown patch role: $Role" }
    $managedRoots = @($catalog.source_roots | ForEach-Object { $_.destination }) + @($catalog.generated_roots)
    $owners = @{}
    $patchInputs = @()
    foreach ($patch in $selected) {
        $patchFile = Join-Path $RepoRoot $patch.file
        $paths = @(Get-YeePatchPaths $patchFile)
        foreach ($path in $paths) {
            # Windows에서 대소문자만 다른 경로도 같은 관리 파일을 가리킨다.
            foreach ($root in $managedRoots) {
                if ($path -eq $root -or
                    $path.StartsWith($root + '/', [System.StringComparison]::OrdinalIgnoreCase) -or
                    $root.StartsWith($path + '/', [System.StringComparison]::OrdinalIgnoreCase)) {
                    throw "Patch sections must not own generated or mirrored source paths: $path"
                }
            }
            if ($owners.ContainsKey($path)) { throw 'Patch series entries must own disjoint files.' }
            $owners[$path] = $true
        }
        $patchInputs += [pscustomobject]@{ Patch = $patch; File = $patchFile; Paths = $paths }
    }
    foreach ($patchInput in $patchInputs) {
        $patch = $patchInput.Patch
        $patchFile = $patchInput.File
        $paths = $patchInput.Paths
        $includes = if ($patch.strategy -eq 'sections') { $paths } else { @('') }
        foreach ($include in $includes) {
            $arguments = @()
            if ($include) { $arguments += "--include=$include" }
            $arguments += $patchFile
            $reverse = Invoke-YeeGitApply $ChromiumSrc (@('--reverse', '--check') + $arguments)
            if (-not $reverse.Success) {
                $forward = Invoke-YeeGitApply $ChromiumSrc (@('--check') + $arguments)
                if (-not $forward.Success) { throw "Cannot apply ${patchFile}: $($forward.Output)" }
            }
            [pscustomobject]@{ Patch = $patchFile; Arguments = $arguments; Pending = (-not $reverse.Success) }
        }
    }
}

function Invoke-YeeOverlayPatchPlan {
    param(
        [Parameter(Mandatory = $true)][string] $ChromiumSrc,
        [AllowEmptyCollection()][object[]] $Plan,
        [switch] $CheckOnly
    )
    foreach ($step in $Plan) {
        $label = Split-Path -Leaf $step.Patch
        if (-not $step.Pending) { Write-Host "Already applied: $label"; continue }
        if ($CheckOnly) { Write-Host "Applicable: $label"; continue }
        $result = Invoke-YeeGitApply $ChromiumSrc $step.Arguments
        if (-not $result.Success) { throw "Failed to apply ${label}: $($result.Output)" }
        Write-Host "Applied: $label"
    }
}

function Get-YeeOverlaySourcePlan {
    param(
        [Parameter(Mandatory = $true)][string] $ChromiumSrc,
        [Parameter(Mandatory = $true)][string] $RepoRoot
    )
    $catalog = Get-YeeOverlayCatalog $RepoRoot
    foreach ($binding in $catalog.source_roots) {
        $owned = Join-Path $RepoRoot $binding.source
        if (-not (Test-Path -LiteralPath $owned -PathType Container)) {
            throw "Missing owned source root: $($binding.source)"
        }
        $ancestor = $owned
        while ($ancestor.Length -ge $RepoRoot.Length) {
            if ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [System.IO.FileAttributes]::ReparsePoint) {
                throw "Source paths may not traverse links: $ancestor"
            }
            if ($ancestor -eq $RepoRoot) { break }
            $ancestor = Split-Path -Parent $ancestor
        }
    }
    $families = @($catalog.source_roots | ForEach-Object { $_.source.Split('/')[0] } | Sort-Object -Unique)
    $expected = @{}
    foreach ($family in $families) {
        foreach ($source in Get-ChildItem -LiteralPath (Join-Path $RepoRoot $family) -Recurse -Force) {
            $relativePath = $source.FullName.Substring($RepoRoot.Length + 1).Replace('\', '/')
            if (@($relativePath.Split('/') | Where-Object { $_ -in $catalog.ignored_source_names }).Count) { continue }
            if ($source.Attributes -band [System.IO.FileAttributes]::ReparsePoint) {
                throw "Source symlinks are not supported: $($source.FullName)"
            }
            if ($source.PSIsContainer) { continue }
            $binding = @($catalog.source_roots | Where-Object { $relativePath.StartsWith($_.source + '/') })
            if ($binding.Count -ne 1) {
                # Root documentation describes ownership; it is not an overlay input.
                if ($binding.Count -eq 0 -and $relativePath -ceq "$family/README.md") { continue }
                throw "Register the source owner in build/overlay.json: $relativePath"
            }
            $suffix = $relativePath.Substring($binding[0].source.Length + 1)
            $destination = Join-Path (Join-Path $ChromiumSrc $binding[0].destination) $suffix
            $expected[$destination] = $true
            if ((Test-Path -LiteralPath $destination) -and
                -not (Test-Path -LiteralPath $destination -PathType Leaf)) {
                throw "Source destination is not a file: $destination"
            }
            $ancestor = $destination
            while ($ancestor.Length -gt $ChromiumSrc.Length) {
                if (Test-Path -LiteralPath $ancestor) {
                    $item = Get-Item -LiteralPath $ancestor -Force
                    if ($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) {
                        throw "Source destinations may not traverse links: $ancestor"
                    }
                    if ($ancestor -ne $destination -and -not $item.PSIsContainer) {
                        throw "Source destination parent is not a directory: $ancestor"
                    }
                }
                $ancestor = Split-Path -Parent $ancestor
            }
            $needsCopy = -not (Test-Path -LiteralPath $destination -PathType Leaf)
            if (-not $needsCopy) {
                $needsCopy = (Get-FileHash -LiteralPath $source.FullName).Hash -ne (Get-FileHash -LiteralPath $destination).Hash
            }
            if ($needsCopy) { [pscustomobject]@{ Source = $source.FullName; Destination = $destination } }
        }
    }
    foreach ($binding in $catalog.source_roots) {
        $target = Join-Path $ChromiumSrc $binding.destination
        $ancestor = $target
        while ($ancestor.Length -ge $ChromiumSrc.Length) {
            if ((Test-Path -LiteralPath $ancestor) -and
                ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [System.IO.FileAttributes]::ReparsePoint)) {
                throw "Source destinations may not traverse links: $ancestor"
            }
            if ($ancestor -eq $ChromiumSrc) { break }
            $ancestor = Split-Path -Parent $ancestor
        }
        if (-not (Test-Path -LiteralPath $target)) { continue }
        if (-not (Test-Path -LiteralPath $target -PathType Container)) { throw "Source destination root is not a directory: $target" }
        foreach ($destination in Get-ChildItem -LiteralPath $target -Recurse -Force) {
            $relative = $destination.FullName.Substring($target.Length + 1).Replace('\', '/')
            if (@($relative.Split('/') | Where-Object { $_ -in $catalog.ignored_source_names }).Count) { continue }
            if ($destination.Attributes -band [System.IO.FileAttributes]::ReparsePoint) {
                throw "Source destinations may not traverse links: $($destination.FullName)"
            }
            if (-not $destination.PSIsContainer -and -not $expected.ContainsKey($destination.FullName)) {
                [pscustomobject]@{ Source = $null; Destination = $destination.FullName }
            }
        }
    }
}

function Sync-YeeOverlaySourcePlan {
    param([AllowEmptyCollection()][object[]] $Plan, [switch] $CheckOnly)
    foreach ($step in $Plan) {
        if (-not $CheckOnly) {
            if ($null -eq $step.Source) {
                Remove-Item -LiteralPath $step.Destination -Force
            } else {
                New-Item -ItemType Directory -Path (Split-Path -Parent $step.Destination) -Force | Out-Null
                Copy-Item -LiteralPath $step.Source -Destination $step.Destination -Force
            }
        }
    }
    $verb = if ($CheckOnly) { 'Would sync' } else { 'Synced' }
    $deleted = @($Plan | Where-Object { $null -eq $_.Source }).Count
    Write-Host "$verb $(@($Plan).Count) Yee overlay files ($deleted deletions)."
}
