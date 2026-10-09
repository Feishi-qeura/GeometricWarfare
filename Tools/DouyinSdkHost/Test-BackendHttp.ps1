param([string]$UnityEditor = 'D:\unityhub\2022.3.62f1c1\Editor\Unity.exe')
$ErrorActionPreference = 'Stop'
$dataRoot = Join-Path (Split-Path $UnityEditor -Parent) 'Data'
$monoExe = Join-Path $dataRoot 'MonoBleedingEdge/bin/mono.exe'
$csc = Join-Path $dataRoot 'MonoBleedingEdge/lib/mono/4.5/csc.exe'
$framework = Join-Path $dataRoot 'MonoBleedingEdge/lib/mono/4.5'
$output = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../tmp/DouyinSdkHostHttpTests'))
New-Item -ItemType Directory -Path $output -Force | Out-Null
$json = Join-Path $dataRoot 'Managed/Newtonsoft.Json.dll'
Copy-Item -LiteralPath $json -Destination $output -Force
$target = Join-Path $output 'LiveBackendClientTests.exe'
& $monoExe $csc /nologo /langversion:latest /target:exe ("/out:" + $target) ("/r:" + $json) ("/r:" + (Join-Path $framework 'System.Net.Http.dll')) ("/r:" + (Join-Path $framework 'Facades/netstandard.dll')) (Join-Path $PSScriptRoot 'Runtime/LiveBackendClient.cs') (Join-Path $PSScriptRoot 'Tests/LiveBackendClientTests.cs')
if ($LASTEXITCODE -ne 0) { throw 'Backend HTTP contract test compilation failed' }
& $monoExe $target
if ($LASTEXITCODE -ne 0) { throw 'Backend HTTP contract test failed' }
