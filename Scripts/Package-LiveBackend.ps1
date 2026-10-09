param([string]$OutputDirectory = '')
$ErrorActionPreference = 'Stop'
$packageWorkspace = Split-Path $PSScriptRoot -Parent
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $packageWorkspace ('output/live-backend-deploy-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
$packageTarget = [IO.Path]::GetFullPath($OutputDirectory)
$packageZip = $packageTarget + '.zip'
if ((Test-Path -LiteralPath $packageTarget) -or (Test-Path -LiteralPath $packageZip)) {
    throw 'Use a new output path; existing deployment files are not overwritten.'
}
$packageService = Join-Path $packageWorkspace 'Services/LivePlatformBackend'
$packageFiles = [Collections.Generic.List[string]]::new()
foreach ($packageName in @('README.md','DESIGN.md','.env.example','requirements.txt','requirements.lock.txt','requirements-dev.txt','Start-Backend.ps1','Preflight-IIS.ps1','iis-live-api-merge.fragment.xml','benchmark.py')) {
    $packageFiles.Add((Join-Path $packageService $packageName))
}
foreach ($packagePattern in @('live_backend/*.py','tests/*.py','tests/fixtures/*.json')) {
    foreach ($packageFile in Get-ChildItem -Path (Join-Path $packageService $packagePattern) -File) { $packageFiles.Add($packageFile.FullName) }
}
foreach ($packageDocument in @('docs/integration/live-backend-contract-v1.md','docs/integration/2026-10-05-tencent-iis-deployment.md','docs/integration/fixtures/douyin-world-rank-public-contract.json')) {
    $packageFiles.Add((Join-Path $packageWorkspace $packageDocument))
}
# Explicit allow-list: never enumerate credentials, data, virtualenvs or logs.
foreach ($packageFile in $packageFiles) {
    if (-not (Test-Path -LiteralPath $packageFile -PathType Leaf)) { throw "Required deployment source missing: $packageFile" }
    $packageRelative = [IO.Path]::GetRelativePath($packageWorkspace, $packageFile)
    if ($packageRelative.StartsWith('..')) { throw 'Source escaped the workspace.' }
    $packageDestination = Join-Path $packageTarget $packageRelative
    New-Item -ItemType Directory -Path (Split-Path $packageDestination -Parent) -Force | Out-Null
    Copy-Item -LiteralPath $packageFile -Destination $packageDestination
}
$packageChecksums = foreach ($packageFile in Get-ChildItem -LiteralPath $packageTarget -Recurse -File) {
    '{0}  {1}' -f (Get-FileHash -LiteralPath $packageFile.FullName -Algorithm SHA256).Hash, [IO.Path]::GetRelativePath($packageTarget,$packageFile.FullName)
}
$packageChecksums | Set-Content -LiteralPath (Join-Path $packageTarget 'SHA256SUMS.txt') -Encoding UTF8
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($packageTarget,$packageZip,[IO.Compression.CompressionLevel]::Optimal,$false)
Write-Output "BACKEND_PACKAGE=$packageZip"
Write-Output ("SHA256=" + (Get-FileHash -LiteralPath $packageZip).Hash)
