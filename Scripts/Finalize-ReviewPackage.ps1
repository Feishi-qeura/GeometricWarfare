param([string]$Version='1.1.16',[string]$StagedDirectory='output/review-release-20261008/Windows')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$staged=Join-Path $projectRoot $StagedDirectory
$folderName='GeomeWar_'+$Version
$final=Join-Path $projectRoot ('output/'+$folderName)
$zip=$final+'.zip'
if((Test-Path -LiteralPath $final) -or (Test-Path -LiteralPath $zip)){throw 'Final version already exists; preserve it and choose a new version.'}
foreach($required in @('GeometricWarfare.exe','GeometricWarfare/Binaries/Win64/GeometricWarfare-Win64-Shipping.exe','GeometricWarfare/Plugins/DouyinLiveProvider/Binaries/Win64/SdkHost/DouyinSdkHost.exe')){if(-not(Test-Path -LiteralPath (Join-Path $staged $required))){throw ('Missing required package file '+$required)}}
Copy-Item -LiteralPath $staged -Destination $final -Recurse
$files=Get-ChildItem -LiteralPath $final -Recurse -File
if($files|Where-Object {$_.Name -match '(?i)(\.pdb$|\.sav$|^\.env($|\.)|\.jsonl$|\.log$)'}){throw 'Unexpected debug/state/secret files staged'}
$built=Join-Path $projectRoot 'Binaries/Win64/GeometricWarfare-Win64-Shipping.exe'
$packaged=Join-Path $final 'GeometricWarfare/Binaries/Win64/GeometricWarfare-Win64-Shipping.exe'
$exeHash=(Get-FileHash -LiteralPath $packaged -Algorithm SHA256).Hash
if($exeHash -ne (Get-FileHash -LiteralPath $built -Algorithm SHA256).Hash){throw 'Packaged application differs from freshly built executable'}
if((Get-FileHash -LiteralPath (Join-Path $final 'GeometricWarfare.exe') -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath (Join-Path $projectRoot 'tmp/DouyinLauncherBuild/DouyinLauncher.exe') -Algorithm SHA256).Hash){throw 'Package root launcher mismatch'}
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($final,$zip,[IO.Compression.CompressionLevel]::Optimal,$true)
$archive=[IO.Compression.ZipFile]::OpenRead($zip)
$verified=0
try {
    if($archive.Entries.Count -ne $files.Count){throw 'ZIP entry count differs from final files'}
    foreach($entry in $archive.Entries){
        if(-not $entry.FullName.StartsWith($folderName+'/')){throw 'Unexpected archive root'}
        $relative=$entry.FullName.Substring($folderName.Length+1)
        $expected=Get-FileHash -LiteralPath (Join-Path $final $relative) -Algorithm SHA256
        $stream=$entry.Open();$sha=[Security.Cryptography.SHA256]::Create()
        try{$actual=[Convert]::ToHexString($sha.ComputeHash($stream))}finally{$stream.Dispose();$sha.Dispose()}
        if($actual -ne $expected.Hash){throw ('ZIP file hash mismatch: '+$relative)}
        ++$verified
    }
}finally{$archive.Dispose()}
$metadata=@{version=$Version;package=$final;zip=$zip;files=$files.Count;zip_verified_files=$verified;bytes=(Get-Item -LiteralPath $zip).Length;zip_sha256=(Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash;application_sha256=$exeHash;gm=$false;source_gm_disabled=([IO.File]::ReadAllText((Join-Path $projectRoot 'Config/DefaultGame.ini')) -match 'EnableGM=False')}
if(-not $metadata.source_gm_disabled){throw 'Source GM config must be disabled for review'}
$metadata|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $projectRoot ('output/'+$folderName+'-verification.json')) -Encoding UTF8
$metadata|ConvertTo-Json
