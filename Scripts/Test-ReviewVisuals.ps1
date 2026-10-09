param([string]$EngineRoot='F:\UNREAL\UE_5.8\UE_5.8',[string]$Only='',[string]$Prefix='Review')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$cases=@(
    @{Name='Review-PC';X=1920;Y=1080;Device=1;Aspect=1},
    @{Name='Review-PCSmall';X=960;Y=540;Device=1;Aspect=1},
    @{Name='Review-Settings';X=1920;Y=1080;Device=1;Aspect=1;Flag='-GWSettingsPreview'},
    @{Name='Review-Phone';X=390;Y=844;Device=2;Aspect=3},
    @{Name='Review-PhoneAuto';X=390;Y=844;Device=2;Aspect=0},
    @{Name='Review-MobileWide';X=1280;Y=720;Device=2;Aspect=1},
    @{Name='Review-NearSquare';X=1024;Y=1280;Device=2;Aspect=0},
    @{Name='Review-PhoneNotices';X=390;Y=844;Device=2;Aspect=3;Mode='notifications-basic'},
    @{Name='Review-NearSquareNotices';X=1024;Y=1280;Device=2;Aspect=0;Mode='notifications'},
    @{Name='Review-Tablet';X=960;Y=720;Device=2;Aspect=2},
    @{Name='Review-TabletResults';X=960;Y=720;Device=2;Aspect=2;Mode='review-results'},
    @{Name='Review-PhoneResults';X=390;Y=844;Device=2;Aspect=3;Mode='review-results'},
    @{Name='Review-Results-Small';X=854;Y=480;Device=2;Aspect=1;Mode='review-results'},
    @{Name='Review-Results-540';X=960;Y=540;Device=2;Aspect=1;Mode='review-results'},
    @{Name='Review-WeaponNotices';X=1920;Y=1080;Device=1;Aspect=1;Mode='notifications'},
    @{Name='Review-BaseNotices';X=1920;Y=1080;Device=1;Aspect=1;Mode='notifications-basic'},
    @{Name='Review-WandHealth';X=1920;Y=1080;Device=1;Aspect=1;Mode='wand-health'},
    @{Name='Review-WandEvolved';X=1920;Y=1080;Device=1;Aspect=1;Mode='wand-evolved'},
    @{Name='Review-WandRevive';X=1920;Y=1080;Device=1;Aspect=1;Mode='wand-revive'},
    @{Name='Review-Score-PC';X=1920;Y=1080;Device=1;Aspect=1;Mode='score-bars'},
    @{Name='Review-Score-PCSmall';X=960;Y=540;Device=1;Aspect=1;Mode='score-bars'},
    @{Name='Review-Score-Phone';X=390;Y=844;Device=2;Aspect=0;Mode='score-bars'},
    @{Name='Review-Score-MobileWide';X=1280;Y=720;Device=2;Aspect=1;Mode='score-bars'},
    @{Name='Review-Score-NearSquare';X=1024;Y=1280;Device=2;Aspect=0;Mode='score-bars'},
    @{Name='Review-Score-ZeroPhone';X=390;Y=844;Device=2;Aspect=0;Mode='score-bars-zero'},
    @{Name='Review-Score-LongPC';X=1920;Y=1080;Device=1;Aspect=1;Mode='score-bars-long'},
    @{Name='Review-Score-LongPhone';X=390;Y=844;Device=2;Aspect=0;Mode='score-bars-long'}
)
foreach($case in $cases){
    if($Only -and $case.Name -notlike $Only){continue}
    $case.Name=$case.Name -replace '^Review-',($Prefix+'-')
    $renderArguments=@((Join-Path $projectRoot 'GeometricWarfare.uproject'),'/Engine/Maps/Entry','-game','-dx12','-sm5','-unattended','-nosplash','-nosound','-NoSpout','-RenderOffscreen','-Windowed',('-ResX='+$case.X),('-ResY='+$case.Y),'-ForceRes','-GWLocalTest','-GWVisualTest',('-GWProgressSlot=Visual-'+$case.Name),('-GWCaptureName='+$case.Name),('-ini:GameUserSettings:[/Script/GeometricWarfare.ArenaUserSettings]:LayoutDevice='+$case.Device),('-ini:GameUserSettings:[/Script/GeometricWarfare.ArenaUserSettings]:LayoutAspect='+$case.Aspect),('-abslog='+$projectRoot+'/Saved/'+$case.Name+'.log'))
    if($case.Flag){$renderArguments+=$case.Flag}
    if($case.Mode){$renderArguments+=('-GWCombatTest='+$case.Mode)}
    $started=Get-Date
    $process=Start-Process (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') -ArgumentList $renderArguments -WindowStyle Hidden -PassThru
    if(-not $process.WaitForExit(55000)){Stop-Process -Id $process.Id;throw ('Render timed out: '+$case.Name)}
    $process.Refresh()
    $screenshot=Join-Path $projectRoot ('Saved/Screenshots/'+$case.Name+'.png')
    if($process.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $screenshot) -or (Get-Item -LiteralPath $screenshot).LastWriteTime -lt $started){throw ('Render failed: '+$case.Name)}
    Write-Output ($case.Name+': exit='+$process.ExitCode+' screenshot='+$screenshot)
}
