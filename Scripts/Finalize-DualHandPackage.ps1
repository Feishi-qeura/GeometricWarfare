$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$staged=Join-Path $projectRoot 'output/dual-hand-package-20261007/Windows'
$final=Join-Path $projectRoot 'output/GeomeWar_1.1.15'
$zip=$final+'.zip'
if(Test-Path -LiteralPath $final){throw 'Final version directory already exists; preserve it'}
if(Test-Path -LiteralPath $zip){throw 'Final version ZIP already exists; preserve it'}
foreach($required in @('GeometricWarfare.exe','GeometricWarfare/Binaries/Win64/GeometricWarfare-Win64-Shipping.exe','GeometricWarfare/Plugins/DouyinLiveProvider/Binaries/Win64/SdkHost/DouyinSdkHost.exe')){if(-not(Test-Path -LiteralPath (Join-Path $staged $required))){throw ('Missing required package file '+$required)}}
Copy-Item -LiteralPath $staged -Destination $final -Recurse
Copy-Item -LiteralPath (Join-Path $projectRoot 'output/双手版_使用说明.txt') -Destination (Join-Path $final '使用说明.txt') -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'output/几何战争_500人玩法说明.txt') -Destination (Join-Path $final '玩法说明.txt')
$files=Get-ChildItem -LiteralPath $final -Recurse -File
$unexpected=$files|Where-Object {$_.Name -match '(?i)(\.pdb$|\.sav$|^\.env($|\.)|\.jsonl$|\.log$)'}
if($unexpected){throw 'Unexpected debug/state/secret files staged'}
$built=Join-Path $projectRoot 'Binaries/Win64/GeometricWarfare-Win64-Shipping.exe'
$packaged=Join-Path $final 'GeometricWarfare/Binaries/Win64/GeometricWarfare-Win64-Shipping.exe'
$exeHash=(Get-FileHash -LiteralPath $packaged -Algorithm SHA256).Hash
if($exeHash -ne (Get-FileHash -LiteralPath $built -Algorithm SHA256).Hash){throw 'Packaged game differs from freshly built executable'}
if((Get-FileHash -LiteralPath (Join-Path $final 'GeometricWarfare.exe') -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath (Join-Path $projectRoot 'tmp/DouyinLauncherBuild/DouyinLauncher.exe') -Algorithm SHA256).Hash){throw 'Package root launcher mismatch'}
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($final,$zip,[IO.Compression.CompressionLevel]::Optimal,$true)
$archive=[IO.Compression.ZipFile]::OpenRead($zip)
try {
    if($archive.Entries.Count -ne $files.Count){throw 'ZIP entry count differs from final files'}
    foreach($entry in $archive.Entries){if(-not $entry.FullName.StartsWith('GeomeWar_1.1.15/')){throw 'Unexpected archive root'}}
    $exeEntry=$archive.GetEntry('GeomeWar_1.1.15/GeometricWarfare/Binaries/Win64/GeometricWarfare-Win64-Shipping.exe')
    if(-not $exeEntry){throw 'ZIP game executable missing'}
    $stream=$exeEntry.Open();$sha=[Security.Cryptography.SHA256]::Create()
    try{$zipExeHash=[Convert]::ToHexString($sha.ComputeHash($stream))}finally{$stream.Dispose();$sha.Dispose()}
    if($zipExeHash -ne $exeHash){throw 'ZIP executable hash mismatch'}
}finally{$archive.Dispose()}
$metadata=@{version='1.1.15';package=$final;zip=$zip;files=$files.Count;bytes=(Get-Item -LiteralPath $zip).Length;zip_sha256=(Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash;game_sha256=$exeHash;gm=$true;source_gm_disabled=([IO.File]::ReadAllText((Join-Path $projectRoot 'Config/DefaultGame.ini')) -match 'EnableGM=False')}
if(-not $metadata.source_gm_disabled){throw 'Source GM config was not restored'}
$metadata|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $projectRoot 'output/GeomeWar_1.1.15-verification.json') -Encoding UTF8
$metadata|ConvertTo-Json
