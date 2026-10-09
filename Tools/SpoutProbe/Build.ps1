param()
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$thirdParty = Join-Path $projectRoot 'Plugins/LiveSpoutOutput/Source/ThirdParty'
$outputDirectory = Join-Path $projectRoot 'Saved/SpoutProbe'
$compiler = Get-Command cl.exe -ErrorAction SilentlyContinue
if (-not $compiler) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $found = @(& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'VC/Tools/MSVC/*/bin/Hostx64/x64/cl.exe')
        if ($found.Count -gt 0) { $compiler = Get-Item -LiteralPath $found[0] }
    }
}
if (-not $compiler) { throw 'MSVC x64 compiler not found. Install Visual Studio C++ build tools and a Windows SDK, then use an x64 Developer PowerShell.' }
$compilerPath = if ($compiler.Source) { $compiler.Source } else { $compiler.FullName }
$vcDirectory = Split-Path $compilerPath -Parent
$vcvars = $null
while ($vcDirectory) {
    $candidate = Join-Path $vcDirectory 'Auxiliary/Build/vcvars64.bat'
    if (Test-Path -LiteralPath $candidate) { $vcvars = $candidate; break }
    $vcDirectory = Split-Path $vcDirectory -Parent
}
if (-not $vcvars) { throw 'Could not locate vcvars64.bat beside the selected MSVC installation.' }
$savedEnvironment = @{}
foreach ($name in @('INCLUDE', 'LIB', 'LIBPATH', 'PATH')) { $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
try {
    $environmentLines = & $env:ComSpec /d /c ('"' + $vcvars + '" >nul && set')
    if ($LASTEXITCODE -ne 0) { throw 'MSVC x64 environment initialization failed.' }
    foreach ($line in $environmentLines) {
        if ($line -match '^(INCLUDE|LIB|LIBPATH|PATH)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process') }
    }
    # vcvars64 chooses the matching x64 compiler, even when the caller started in an x86 shell.
    $compilerPath = (Get-Command cl.exe -ErrorAction Stop).Source
    $library = Join-Path $thirdParty 'lib/Win64/SpoutDX12.lib'
    $dll = Join-Path $thirdParty 'bin/Win64/SpoutDX12.dll'
    foreach ($required in @($library, $dll, (Join-Path $thirdParty 'include/SpoutDX.h'))) {
        if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing bundled Spout dependency: $required" }
    }
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
    $executable = Join-Path $outputDirectory 'receiver-package-probe.exe'
    & $compilerPath /nologo /std:c++17 /EHsc /O2 /W4 /MD "/I$(Join-Path $thirdParty 'include')" `
        (Join-Path $PSScriptRoot 'receiver-package-probe.cpp') "/Fe:$executable" "/Fo:$(Join-Path $outputDirectory 'receiver-package-probe.obj')" `
        /link $library d3d11.lib dxgi.lib
    if ($LASTEXITCODE -ne 0) { throw "Spout probe compilation failed with exit code $LASTEXITCODE." }
    Copy-Item -LiteralPath $dll -Destination (Join-Path $outputDirectory 'SpoutDX12.dll') -Force
    Write-Output "Spout probe built: $executable"
} finally {
    foreach ($name in $savedEnvironment.Keys) { [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], 'Process') }
}
