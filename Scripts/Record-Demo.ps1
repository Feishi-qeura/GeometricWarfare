param([string]$EngineRoot, [switch]$Encode, [ValidateSet(1,4)][int]$Speed=4)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
if(-not $EngineRoot) {
    $registered=(Get-ItemProperty 'HKLM:\SOFTWARE\EpicGames\Unreal Engine\5.8' -ErrorAction SilentlyContinue).InstalledDirectory
    $candidates=@($env:UE_ENGINE_ROOT)
    if($registered){$candidates+=$registered;$candidates+=Join-Path $registered 'UE_5.8'}
    foreach($candidate in $candidates){if($candidate -and (Test-Path (Join-Path $candidate 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'))){$EngineRoot=$candidate;break}}
}
if(-not $EngineRoot){throw 'Pass -EngineRoot with the UE 5.8 folder.'}
$frames=Join-Path $projectRoot 'Saved\DemoFrames'
if(Test-Path $frames){
    # Preserve prior captures; never merge two different recording runs.
    $backup=Join-Path $projectRoot ('Saved\DemoFrames-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
    $workspacePrefix=[IO.Path]::GetFullPath($projectRoot).TrimEnd('\')+'\'
    $resolvedFrames=(Resolve-Path -LiteralPath $frames).Path
    $resolvedBackup=[IO.Path]::GetFullPath($backup)
    if(-not $resolvedFrames.StartsWith($workspacePrefix,[StringComparison]::OrdinalIgnoreCase) -or -not $resolvedBackup.StartsWith($workspacePrefix,[StringComparison]::OrdinalIgnoreCase)){throw 'Capture paths must remain inside the project workspace.'}
    Move-Item -LiteralPath $frames -Destination $backup
}
# Recording requires an explicit development simulation session; it is not a platform SDK session.
Write-Output 'Recording the development demo with explicit -GWLocalTest.'
& (Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe') (Join-Path $projectRoot 'GeometricWarfare.uproject') /Engine/Maps/Entry -game -unattended -nop4 -nosplash -nosound -windowed -ForceRes -ResX=1920 -ResY=1080 -RenderOffscreen -DDC-ForceMemoryCache '-DisablePlugins=RiderLink' -GWLocalTest -GWRecordDemo "-GWRecordSpeed=$Speed" "-abslog=$projectRoot\Saved\RecordDemo.log"
if($LASTEXITCODE -ne 0){throw 'Unreal demo recording failed.'}
$count=(Get-ChildItem -LiteralPath $frames -Filter 'Frame_*.png').Count
if($count -lt (1000*4/$Speed)){throw "Incomplete recording: $count frames. Inspect Saved/RecordDemo.log."}
if($Encode){& (Join-Path $PSScriptRoot 'Encode-Demo.ps1') -ExpectedFrames $count -OutputPath (Join-Path $projectRoot "Saved\Videos\GeometricWarfare-Demo-${Speed}x.mp4")}
Write-Output "Recorded $count frames at 12 fps; game simulation runs at ${Speed}x."
