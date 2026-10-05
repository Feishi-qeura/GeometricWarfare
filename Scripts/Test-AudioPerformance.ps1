param([ValidateSet('On','Off')][string]$Mode='On',[string]$EngineRoot='F:\UNREAL\UE_5.8\UE_5.8')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$audioArgs=@((Join-Path $projectRoot 'GeometricWarfare.uproject'),'/Engine/Maps/Entry','-game','-dx12','-sm5','-unattended','-nosplash','-RenderOffscreen','-Windowed','-ResX=1920','-ResY=1080','-ForceRes','-GWLocalTest','-GWStressTest','-GWCombatStress','-GWAudioStress','-NoSpout','-ini:GameUserSettings:[/Script/GeometricWarfare.ArenaUserSettings]:EffectMode=0','-ini:GameUserSettings:[/Script/GeometricWarfare.ArenaUserSettings]:FrameRateLimit=0','-ini:GameUserSettings:[/Script/GeometricWarfare.ArenaUserSettings]:BgmVolume=0.55','-ini:GameUserSettings:[/Script/GeometricWarfare.ArenaUserSettings]:SfxVolume=0.70',('-abslog='+$projectRoot+'/Saved/AudioStress-'+$Mode+'.log'))
if($Mode -eq 'Off'){$audioArgs+='-GWAudioDisabled'}
$audioProcess=Start-Process (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') -ArgumentList $audioArgs -WindowStyle Hidden -PassThru
Write-Output ('AUDIO_STRESS_PID='+$audioProcess.Id)
if(-not $audioProcess.WaitForExit(55000)){Write-Output 'RUNNING: resume by process ID and inspect fresh CombatStress5000.json';exit 0}
$audioProcess.Refresh();if($audioProcess.ExitCode -ne 0){throw "Audio stress failed: $($audioProcess.ExitCode)"}
$reportPath=Join-Path $projectRoot 'Saved/CombatStress5000.json'
$audioReport=Get-Content -LiteralPath $reportPath -Raw|ConvertFrom-Json
if($audioReport.players -ne 5000 -or $audioReport.samples -lt 100){throw 'Incomplete 5000-player fixture'}
if($Mode -eq 'On' -and (-not $audioReport.audio_device -or $audioReport.audio_missing -ne 0 -or $audioReport.audio_starts -le 0 -or $audioReport.sfx_peak -gt 16 -or $audioReport.audio.p95_ms -gt .5)){throw 'Audio acceptance failed'}
Copy-Item -LiteralPath $reportPath -Destination (Join-Path $projectRoot ('Saved/AudioStress-'+$Mode+'.json'))
Get-Content -LiteralPath $reportPath -Raw
