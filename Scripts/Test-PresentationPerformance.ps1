param([ValidateSet('Full','Auto')][string]$Mode='Auto',[string]$EngineRoot='F:\UNREAL\UE_5.8\UE_5.8')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$report=Join-Path $projectRoot 'Saved/CombatStress500.json'
if(Test-Path -LiteralPath $report){Copy-Item -LiteralPath $report -Destination (Join-Path $projectRoot ('Saved/PerformanceSettings-PreviousStress-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'.json'))}
$effectMode=if($Mode -eq 'Full'){1}else{0}
$arguments=@("$projectRoot/GeometricWarfare.uproject",'/Engine/Maps/Entry','-game','-dx12','-sm5','-unattended','-nosplash','-nosound','-RenderOffscreen','-Windowed','-ResX=1920','-ResY=1080','-ForceRes','-GWLocalTest','-GWStressTest','-GWCombatStress','-NoSpout',"-ini:GameUserSettings:[/Script/GeometricWarfare.ArenaUserSettings]:EffectMode=$effectMode",'-ini:GameUserSettings:[/Script/GeometricWarfare.ArenaUserSettings]:FrameRateLimit=0',"-abslog=$projectRoot/Saved/PerformanceSettings-Stress-$Mode.log")
$started=Get-Date
$process=Start-Process -FilePath (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') -ArgumentList $arguments -WindowStyle Hidden -PassThru
# Return a resumable process ID rather than a blocking wait beyond one minute.
if(-not $process.WaitForExit(55000)){Write-Output "RUNNING_PID=$($process.Id)";exit 0}
$process.Refresh()
if($process.ExitCode -ne 0){throw "Stress fixture failed: $($process.ExitCode)"}
if(-not (Test-Path -LiteralPath $report) -or (Get-Item -LiteralPath $report).LastWriteTime -lt $started){throw 'Fresh stress report not produced.'}
Copy-Item -LiteralPath $report -Destination (Join-Path $projectRoot "Saved/PerformanceSettings-Stress-$Mode.json")
Write-Output (Get-Content -LiteralPath $report -Raw)
