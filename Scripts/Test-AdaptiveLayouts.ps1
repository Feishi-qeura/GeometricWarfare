param([string]$EngineRoot='F:\UNREAL\UE_5.8\UE_5.8')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$cases=@(
    @{Name='Adaptive-Final-Mobile-16x9';X=1280;Y=720;Device=2;Aspect=1},
    @{Name='Adaptive-Final-Mobile-4x3';X=1280;Y=960;Device=2;Aspect=2},
    @{Name='Adaptive-Final-Mobile-9x16';X=1080;Y=1920;Device=2;Aspect=3},
    @{Name='Adaptive-Final-Mobile-20x17';X=1200;Y=1020;Device=2;Aspect=4},
    @{Name='Adaptive-Final-Mobile-19x9';X=1900;Y=900;Device=2;Aspect=5},
    @{Name='Adaptive-Final-Settings';X=1080;Y=1920;Device=2;Aspect=3;Flag='-GWSettingsPreview'},
    @{Name='Adaptive-Final-HostPanel';X=1280;Y=720;Device=2;Aspect=1;Flag='-GWHostPanelPreview'},
    @{Name='Adaptive-Final-Portrait-SpoutFit';X=1920;Y=1080;Device=2;Aspect=3},
    @{Name='Adaptive-Final-PC-16x9';X=1920;Y=1080;Device=1;Aspect=1},
    @{Name='Adaptive-Final-PC-Portrait-Host';X=1080;Y=1920;Device=1;Aspect=3;Flag='-GWHostAssistPreview'},
    @{Name='Adaptive-Final-NearSquare-Host';X=1024;Y=1280;Device=2;Aspect=0;Flag='-GWHostAssistPreview'}
)
foreach($case in $cases){
    $renderArgs=@((Join-Path $projectRoot 'GeometricWarfare.uproject'),'/Engine/Maps/Entry','-game','-dx12','-sm5','-unattended','-nosplash','-nosound','-RenderOffscreen','-Windowed',('-ResX='+$case.X),('-ResY='+$case.Y),'-ForceRes','-GWLocalTest','-GWVisualTest',('-GWCaptureName='+$case.Name),('-ini:GameUserSettings:[/Script/GeometricWarfare.ArenaUserSettings]:LayoutDevice='+$case.Device),('-ini:GameUserSettings:[/Script/GeometricWarfare.ArenaUserSettings]:LayoutAspect='+$case.Aspect),('-abslog='+$projectRoot+'/Saved/'+$case.Name+'.log'))
    if($case.Flag){$renderArgs+=$case.Flag}
    $process=Start-Process (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor.exe') -ArgumentList $renderArgs -WindowStyle Hidden -PassThru
    if(-not $process.WaitForExit(60000)){Stop-Process -Id $process.Id;throw ('Render timed out: '+$case.Name)}
    $screenshot=Join-Path $projectRoot ('Saved/Screenshots/'+$case.Name+'.png')
    if($process.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $screenshot)){throw ('Render failed: '+$case.Name)}
    Write-Output ($case.Name+': exit='+$process.ExitCode+' screenshot='+$screenshot)
}
