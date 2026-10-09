param([string]$EngineRoot='F:\UNREAL\UE_5.8\UE_5.8')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$reportPath=Join-Path $projectRoot 'Saved/CombatStress500.json'
$renderArguments=@((Join-Path $projectRoot 'GeometricWarfare.uproject'),'/Engine/Maps/Entry','-game','-dx12','-sm5','-unattended','-nosplash','-RenderOffscreen','-Windowed','-ResX=1920','-ResY=1080','-ForceRes','-GWLocalTest','-GWStressTest','-GWCombatStress','-GWDualStress','-GWAudioStress','-NoSpout','-GWProgressSlot=DualHandPerformance20261007','-ini:GameUserSettings:[/Script/GeometricWarfare.ArenaUserSettings]:EffectMode=0','-ini:GameUserSettings:[/Script/GeometricWarfare.ArenaUserSettings]:FrameRateLimit=0',('-abslog='+$projectRoot+'/Saved/DualHand-Performance-20261007.log'))
$started=Get-Date
$process=Start-Process (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') -ArgumentList $renderArguments -WindowStyle Hidden -PassThru
if(-not $process.WaitForExit(55000)){Write-Output ('RUNNING_PID='+$process.Id);exit 0}
$process.Refresh();if($process.ExitCode -ne 0){throw "Dual-hand stress failed: $($process.ExitCode)"}
if(-not (Test-Path -LiteralPath $reportPath) -or (Get-Item -LiteralPath $reportPath).LastWriteTime -lt $started){throw 'Fresh stress report missing'}
$report=Get-Content -LiteralPath $reportPath -Raw|ConvertFrom-Json
if($report.players -ne 500 -or $report.host -ne 1 -or $report.samples -lt 100 -or $report.boss_active_samples -lt 1){throw 'Incomplete 500-viewer plus host BOSS fixture'}
if(-not $report.audio_device -or $report.audio_missing -ne 0 -or $report.audio_starts -le 0 -or $report.sfx_peak -gt 16){throw 'Dual-hand audio acceptance failed'}
Copy-Item -LiteralPath $reportPath -Destination (Join-Path $projectRoot 'Saved/DualHand-Performance-20261007.json')
Get-Content -LiteralPath $reportPath -Raw
