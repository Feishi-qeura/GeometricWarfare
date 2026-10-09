param([string]$OutputDirectory = '')
$ErrorActionPreference = 'Stop'
if (-not $OutputDirectory) { $OutputDirectory = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../tmp/DouyinLauncherBuild')) }
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
if (-not $env:INCLUDE) {
    $compilerDir = Split-Path (Get-Command cl.exe).Source -Parent
    $vcvars = $null
    while ($compilerDir) {
        $candidate = Join-Path $compilerDir 'Auxiliary/Build/vcvars64.bat'
        if (Test-Path -LiteralPath $candidate) { $vcvars = $candidate; break }
        $compilerDir = Split-Path $compilerDir -Parent
    }
    if (-not $vcvars) { throw 'Visual Studio x64 C++ toolchain required' }
    & $env:ComSpec /d /c ('"' + $vcvars + '" >nul && set') | ForEach-Object {
        if ($_ -match '^(INCLUDE|LIB|LIBPATH|PATH)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
    }
}
Push-Location $OutputDirectory
try {
    & cl.exe /nologo /std:c++20 /utf-8 /EHsc /O2 /W4 /WX (Join-Path $PSScriptRoot 'Launcher.cpp') /Fe:DouyinLauncher.exe /link /SUBSYSTEM:WINDOWS shell32.lib
    if ($LASTEXITCODE -ne 0) { throw 'Launcher compilation failed' }
    & cl.exe /nologo /std:c++20 /utf-8 /EHsc /O2 /W4 /WX (Join-Path $PSScriptRoot 'Tests/FakeChild.cpp') /Fe:FakeChild.exe /link /SUBSYSTEM:WINDOWS shell32.lib
    if ($LASTEXITCODE -ne 0) { throw 'Fake child compilation failed' }
    Write-Output "Launcher built: $OutputDirectory/DouyinLauncher.exe"
} finally { Pop-Location }
