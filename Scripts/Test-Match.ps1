$ErrorActionPreference = 'Stop'
if (-not $env:INCLUDE) {
    $compilerDir = Split-Path (Get-Command cl.exe).Source -Parent
    $vcvars = $null
    while ($compilerDir) {
        $candidate = Join-Path $compilerDir 'Auxiliary\Build\vcvars64.bat'
        if (Test-Path -LiteralPath $candidate) { $vcvars = $candidate; break }
        $compilerDir = Split-Path $compilerDir -Parent
    }
    if (-not $vcvars) { throw 'Run from a Visual Studio x64 Developer PowerShell.' }
    $environmentCommand = '"' + $vcvars + '" >nul && set'
    & $env:ComSpec /d /c $environmentCommand | ForEach-Object {
        if ($_ -match '^(INCLUDE|LIB|LIBPATH|PATH)=(.*)$') {
            [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
        }
    }
}
$projectRoot = Split-Path $PSScriptRoot -Parent
$outDir = Join-Path $projectRoot 'Saved\MatchTests'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
Push-Location $outDir
try {
    & cl.exe /nologo /std:c++20 /EHsc /O2 /W4 (Join-Path $projectRoot 'Tests\ArenaMatchTests.cpp') /Fe:ArenaMatchTests.exe
    if ($LASTEXITCODE -ne 0) { throw 'Match test compilation failed' }
    & .\ArenaMatchTests.exe
    if ($LASTEXITCODE -ne 0) { throw 'Match tests failed' }
} finally { Pop-Location }
