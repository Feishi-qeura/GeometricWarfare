param([string]$PackageRoot='', [string]$Version='1.1.16', [string]$SenderName='mate_spout_local')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
if(-not $PackageRoot){$PackageRoot=Join-Path $projectRoot ('output/GeomeWar_'+$Version)}
if([string]::IsNullOrWhiteSpace($SenderName) -or $SenderName -match '["\r\n]'){throw 'SenderName must be a nonempty name without quotes or line breaks'}
$rootExe=[IO.Path]::GetFullPath((Join-Path $PackageRoot 'GeometricWarfare.exe'))
$nativeExe=[IO.Path]::GetFullPath((Join-Path $PackageRoot 'GeometricWarfare/Binaries/Win64/GeometricWarfare-Win64-Shipping.exe'))
foreach($required in @($rootExe,$nativeExe)){if(-not (Test-Path -LiteralPath $required -PathType Leaf)){throw "Package executable missing: $required"}}
$logPrefix='Saved/Review-Package-Spout-'+$Version
& (Join-Path $projectRoot 'Tools/SpoutProbe/Build.ps1')
$probe=Join-Path $projectRoot 'Saved/SpoutProbe/receiver-package-probe.exe'
$senderList=& $probe --list
if($LASTEXITCODE -ne 0){throw 'Receiver enumeration failed'}
$senderList|Set-Content -LiteralPath (Join-Path $projectRoot ($logPrefix+'-Before.log'))
$senderPattern='^SENDER name='+[regex]::Escape($SenderName)+' info=1(?: |$)'
if($senderList -cmatch $senderPattern){throw 'An active sender already owns SenderName; leave it untouched and close it manually before rerunning'}
$started=Get-Date
$launcher=Start-Process -FilePath $rootExe -ArgumentList @('-dx12','-RenderOffscreen','-Windowed','-ResX=960','-ResY=540','-GWGMPreview','-unattended','-nosplash','-GWProgressSlot=ReviewNativePackage20261008') -WindowStyle Hidden -PassThru
$probeProcess=$null
try {
    $imagePath=Join-Path $projectRoot ('Saved/SpoutProbe/package-'+$Version+'-final.bmp')
    $probeProcess=Start-Process -FilePath $probe -ArgumentList @('--sender',('"'+$SenderName+'"'),'--output',('"'+$imagePath+'"')) -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $projectRoot ($logPrefix+'-Receiver.log')) -RedirectStandardError (Join-Path $projectRoot ($logPrefix+'-Receiver-error.log'))
    if(-not $probeProcess.WaitForExit(55000)){throw 'Receiver exceeded its own 50-second acquisition deadline'}
    $probeProcess.Refresh();if($probeProcess.ExitCode -ne 0){throw "Packaged native Spout probe failed: $($probeProcess.ExitCode)"}
    $received=Get-Content -LiteralPath (Join-Path $projectRoot ($logPrefix+'-Receiver.log')) -Raw
    $result=[regex]::Match($received,'RESULT frames=(\d+) new=(\d+) wrongsize=0 saved=1 wrongsender=0')
    if(-not $result.Success -or [int64]$result.Groups[1].Value -lt 240 -or [int64]$result.Groups[2].Value -lt 240 -or $received -match 'all_opaque=0'){throw 'Incomplete packaged frame verification'}
    Write-Output $received
} finally {
    if($probeProcess -and -not $probeProcess.HasExited){Stop-Process -Id $probeProcess.Id}
    $children=Get-CimInstance Win32_Process -Filter "ParentProcessId=$($launcher.Id) AND Name='GeometricWarfare-Win64-Shipping.exe'"
    foreach($child in $children){if($child.ExecutablePath -eq $nativeExe -and $child.CreationDate -ge $started.AddSeconds(-1)){Stop-Process -Id $child.ProcessId}}
}
