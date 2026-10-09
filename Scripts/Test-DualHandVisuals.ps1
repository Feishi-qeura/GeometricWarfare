param([string]$EngineRoot='F:\UNREAL\UE_5.8\UE_5.8')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$cases=@(
    @{Name='DualHand-PC';Mode='dual-hands';X=1920;Y=1080;Device=1;Aspect=1},
    @{Name='DualHand-Phone';Mode='dual-hands';X=390;Y=844;Device=2;Aspect=3},
    @{Name='DualHand-Heal-Phone';Mode='dual-heal';X=390;Y=844;Device=2;Aspect=3},
    @{Name='DualHand-Expired';Mode='dual-expired';X=1920;Y=1080;Device=1;Aspect=1}
)
foreach($case in $cases){
    $renderArguments=@((Join-Path $projectRoot 'GeometricWarfare.uproject'),'/Engine/Maps/Entry','-game','-dx12','-sm5','-unattended','-nosplash','-nosound','-NoSpout','-RenderOffscreen','-Windowed',('-ResX='+$case.X),('-ResY='+$case.Y),'-ForceRes','-GWLocalTest',('-GWProgressSlot=Visual-'+$case.Name),('-GWCombatTest='+$case.Mode),('-GWCaptureName='+$case.Name),('-ini:GameUserSettings:[/Script/GeometricWarfare.ArenaUserSettings]:LayoutDevice='+$case.Device),('-ini:GameUserSettings:[/Script/GeometricWarfare.ArenaUserSettings]:LayoutAspect='+$case.Aspect),('-abslog='+$projectRoot+'/Saved/'+$case.Name+'.log'))
    $started=Get-Date
    $process=Start-Process (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') -ArgumentList $renderArguments -WindowStyle Hidden -PassThru
    if(-not $process.WaitForExit(55000)){Stop-Process -Id $process.Id;throw ('Render timed out: '+$case.Name)}
    $process.Refresh()
    $screenshot=Join-Path $projectRoot ('Saved/Screenshots/'+$case.Name+'.png')
    if($process.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $screenshot) -or (Get-Item -LiteralPath $screenshot).LastWriteTime -lt $started){throw ('Render failed: '+$case.Name)}
    Write-Output ($case.Name+': exit='+$process.ExitCode+' screenshot='+$screenshot)
}
