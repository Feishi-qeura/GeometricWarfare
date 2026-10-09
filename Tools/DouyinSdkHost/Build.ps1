param(
    [string]$UnityEditor = 'D:\unityhub\2022.3.62f1c1\Editor\Unity.exe',
    [string]$AcquisitionProject = '',
    [string]$BuildProject = '',
    [string]$OutputDirectory = ''
)
$ErrorActionPreference = 'Stop'
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (-not $AcquisitionProject) { $AcquisitionProject = Join-Path $workspace 'tmp/DouyinSdkAcquisition' }
if (-not $BuildProject) { $BuildProject = Join-Path $workspace 'tmp/DouyinSdkHostBuild' }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $workspace 'Plugins/DouyinLiveProvider/Binaries/Win64/SdkHost' }
if (-not (Test-Path -LiteralPath $UnityEditor)) { throw 'Unity editor not found' }
$sdkSource = Join-Path $AcquisitionProject 'Packages/com.bytedance.liveopensdk'
if (-not (Test-Path -LiteralPath (Join-Path $sdkSource 'package.json'))) { throw 'Acquire official LiveOpenSDK with BGDT first' }
foreach ($part in @('Assets/Editor', 'Packages', 'ProjectSettings', 'Library/PackageCache')) { New-Item -ItemType Directory -Path (Join-Path $BuildProject $part) -Force | Out-Null }
Copy-Item -LiteralPath $sdkSource -Destination (Join-Path $BuildProject 'Packages') -Recurse -Force
# Preserve registry-package classification (embedding exposes their test assemblies).
foreach ($cache in Get-ChildItem -LiteralPath (Join-Path $AcquisitionProject 'Library/PackageCache') -Directory) {
    $packageName = ($cache.Name -split '@')[0]
    $oldEmbedded = [IO.Path]::GetFullPath((Join-Path $BuildProject "Packages/$packageName"))
    $expectedRoot = [IO.Path]::GetFullPath((Join-Path $BuildProject 'Packages')) + [IO.Path]::DirectorySeparatorChar
    if (-not $oldEmbedded.StartsWith($expectedRoot, [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid package copy target' }
    if (Test-Path -LiteralPath $oldEmbedded) { Remove-Item -LiteralPath $oldEmbedded -Recurse -Force }
    $target = Join-Path $BuildProject "Library/PackageCache/$($cache.Name)"
    New-Item -ItemType Directory -Path $target -Force | Out-Null
    foreach ($item in Get-ChildItem -LiteralPath $cache.FullName -Force) { Copy-Item -LiteralPath $item.FullName -Destination $target -Recurse -Force }
}
Copy-Item -LiteralPath (Join-Path $AcquisitionProject 'Packages/manifest.json') -Destination (Join-Path $BuildProject 'Packages/manifest.json') -Force
Copy-Item -LiteralPath (Join-Path $AcquisitionProject 'Packages/packages-lock.json') -Destination (Join-Path $BuildProject 'Packages/packages-lock.json') -Force
$manifestPath = Join-Path $BuildProject 'Packages/manifest.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$manifest.dependencies | Add-Member -NotePropertyName 'com.unity.ugui' -NotePropertyValue '1.0.0' -Force
$manifest | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
Copy-Item -LiteralPath (Join-Path $AcquisitionProject 'ProjectSettings/ProjectVersion.txt') -Destination (Join-Path $BuildProject 'ProjectSettings/ProjectVersion.txt') -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Runtime/DouyinSdkHost.cs') -Destination (Join-Path $BuildProject 'Assets/DouyinSdkHost.cs') -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Runtime/HostDiagnostics.cs') -Destination (Join-Path $BuildProject 'Assets/HostDiagnostics.cs') -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Runtime/LiveBackendClient.cs') -Destination (Join-Path $BuildProject 'Assets/LiveBackendClient.cs') -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Editor/BuildHost.cs') -Destination (Join-Path $BuildProject 'Assets/Editor/BuildHost.cs') -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Editor/HostSchedulingTests.cs') -Destination (Join-Path $BuildProject 'Assets/Editor/HostSchedulingTests.cs') -Force
$env:GWSDK_BUILD_OUTPUT = [IO.Path]::GetFullPath($OutputDirectory)
$buildLog = Join-Path $BuildProject 'build.log'
$arguments = @('-batchmode', '-nographics', '-quit', '-projectPath', ('"' + $BuildProject + '"'), '-buildTarget', 'Win64', '-executeMethod', 'BuildHost.Build', '-logFile', ('"' + $buildLog + '"'))
$process = Start-Process -FilePath $UnityEditor -ArgumentList $arguments -WorkingDirectory $BuildProject -WindowStyle Hidden -PassThru
Write-Output "SDK host build started PID=$($process.Id) log=$buildLog"
$process.WaitForExit()
if ($process.ExitCode -ne 0) { throw "Unity build failed exit=$($process.ExitCode); inspect $buildLog" }
if (-not (Test-Path -LiteralPath (Join-Path $OutputDirectory 'DouyinSdkHost.exe'))) { throw 'Build did not produce host executable' }
Write-Output "SDK host built: $OutputDirectory"
