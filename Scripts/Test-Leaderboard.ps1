$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
if(-not $env:INCLUDE){
    $compilerDir=Split-Path (Get-Command cl.exe).Source -Parent
    while($compilerDir){$candidate=Join-Path $compilerDir 'Auxiliary\Build\vcvars64.bat';if(Test-Path -LiteralPath $candidate){$vcvars=$candidate;break};$compilerDir=Split-Path $compilerDir -Parent}
    if(-not $vcvars){throw 'Visual Studio C++ toolchain required'}
    & $env:ComSpec /d /c ('"'+$vcvars+'" >nul && set') | ForEach-Object {if($_ -match '^(INCLUDE|LIB|LIBPATH|PATH)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}}
}
$outDir=Join-Path $projectRoot 'Saved\LeaderboardTests';New-Item -ItemType Directory -Force -Path $outDir|Out-Null
Push-Location $outDir
try{& cl.exe /nologo /std:c++20 /EHsc /O2 /W4 (Join-Path $projectRoot 'Tests\ArenaLeaderboardTests.cpp') /Fe:ArenaLeaderboardTests.exe;if($LASTEXITCODE -ne 0){throw 'Leaderboard test compilation failed'};& .\ArenaLeaderboardTests.exe;if($LASTEXITCODE -ne 0){throw 'Leaderboard tests failed'}}finally{Pop-Location}
