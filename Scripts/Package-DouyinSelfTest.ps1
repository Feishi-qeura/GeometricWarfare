param([string]$OutputDirectory = '')
$ErrorActionPreference = 'Stop'
$workspace = Split-Path $PSScriptRoot -Parent
if (-not $OutputDirectory) {$OutputDirectory = Join-Path $workspace ('output/douyin-selftest-deploy-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))}
$target = [IO.Path]::GetFullPath($OutputDirectory)
if ((Test-Path -LiteralPath $target) -or (Test-Path -LiteralPath ($target+'.zip'))) {throw 'Choose a fresh output directory.'}
$source = Join-Path $workspace 'Services/DouyinSelfTest'
New-Item -ItemType Directory -Path $target | Out-Null
foreach ($name in @('receiver.py','README.md','Install-Receiver.ps1','Start-Receiver.ps1','Inspect-Receiver.ps1','服务器执行说明.txt','tests/test_receiver.py','tests/test_http.py')) {
    $destination = Join-Path $target $name
    New-Item -ItemType Directory -Path (Split-Path $destination -Parent) -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $source $name) -Destination $destination
}
$checksums = foreach ($file in Get-ChildItem -LiteralPath $target -Recurse -File) {
    (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash+'  '+[IO.Path]::GetRelativePath($target,$file.FullName)
}
$checksums | Set-Content -LiteralPath (Join-Path $target 'SHA256SUMS.txt') -Encoding UTF8
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($target,($target+'.zip'),[IO.Compression.CompressionLevel]::Optimal,$false)
Write-Output ('SELFTEST_PACKAGE='+$target+'.zip')
Write-Output ('SHA256='+(Get-FileHash -LiteralPath ($target+'.zip')).Hash)
