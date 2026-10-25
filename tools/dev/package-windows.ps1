[CmdletBinding()]
param()

. (Join-Path $PSScriptRoot 'common.ps1')

# 설치 경로·등록은 Chromium mini_installer가 소유하고 이 진입점은 배포 산출물만 만든다.
& (Join-Path $PSScriptRoot 'build.ps1') -Target mini_installer
if ($LASTEXITCODE -ne 0) { throw 'Windows installer build failed.' }

$python = Get-DepotCommand -Name python3
$identityText = & $python (Join-Path $script:YeeRoot 'tools/overlay/product_identity.py') show
if ($LASTEXITCODE -ne 0) { throw 'Invalid product identity.' }
$identity = $identityText | ConvertFrom-Json
$browser = Get-Item -LiteralPath $script:YeeBrowserBin
$version = [regex]::Match($browser.VersionInfo.ProductVersion, '^\d+\.\d+\.\d+\.\d+').Value
if (-not $version) { throw 'Browser version is missing.' }
$stream = [System.IO.File]::OpenRead($browser.FullName)
$reader = [System.IO.BinaryReader]::new($stream)
try {
    $stream.Position = 0x3c
    $peOffset = $reader.ReadUInt32()
    $stream.Position = $peOffset + 4
    $architecture = switch ($reader.ReadUInt16()) {
        0x8664 { 'x64' }
        0xAA64 { 'arm64' }
        default { throw 'Unsupported Windows browser architecture.' }
    }
} finally { $reader.Dispose() }

$sourceCommit = (& git -C $script:YeeRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot read source revision.' }
$dirty = [bool](& git -C $script:YeeRoot status --porcelain)
$stamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ')
$directory = Join-Path $script:LocalBuildRoot "releases\windows-$version-$stamp"
if (Test-Path -LiteralPath $directory) { throw "Package output already exists: $directory" }
New-Item -ItemType Directory -Path $directory | Out-Null
$name = "Yee-$version-windows-$architecture-test-unsigned.exe"
$artifact = Join-Path $directory $name
Copy-Item -LiteralPath (Join-Path $script:YeeOutDir 'mini_installer.exe') -Destination $artifact
$signature = (Get-AuthenticodeSignature -LiteralPath $artifact).Status.ToString()
if ($signature -ne 'NotSigned') { throw 'This entry point packages unsigned test installers only.' }

$payloadNames = @(
    'YeeCommunityFilterManifest.json', 'YeeCommunityFilterNotices.txt',
    'YeeCommunityFilters.txt', 'YeeCommunityFilterSources.tar.xz',
    'YeeCommunityResources.json', 'YeeCompiledFilterManifest.json',
    'YeeCompiledFilters.dat', 'YeeContentBlockingNotices.txt',
    'YeeContentBlockingSources.tar.xz'
)
$payloadHashes = [ordered]@{}
foreach ($payloadName in $payloadNames) {
    $payloadHashes[$payloadName] = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $script:YeeOutDir $payloadName)).Hash.ToLowerInvariant()
}
$metadata = [ordered]@{
    schema_version = 1
    product_id = $identity.product_id
    platform = 'windows'
    architecture = $architecture
    channel = 'test'
    version = $version
    source_commit = $sourceCommit
    working_tree_dirty = $dirty
    built_at_utc = $stamp
    installer = $name
    sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $artifact).Hash.ToLowerInvariant()
    size_bytes = (Get-Item -LiteralPath $artifact).Length
    signature = $signature
    component_build = [bool](Select-String -LiteralPath (Join-Path $script:YeeOutDir 'args.gn') -Pattern '^is_component_build\s*=\s*true')
    profile_directory = $identity.profile_directory
    application_name = $identity.windows.application_name
    payload_sha256 = $payloadHashes
}
$utf8 = [System.Text.UTF8Encoding]::new($false)
[System.IO.File]::WriteAllText((Join-Path $directory 'release.json'), ($metadata | ConvertTo-Json -Depth 5) + "`n", $utf8)
Copy-Item -LiteralPath (Join-Path $script:YeeRoot 'docs/windows-test-release.md') -Destination (Join-Path $directory 'README.ko.md')
Write-Host "Windows test package: $directory"
Write-Host "Installer: $artifact"
