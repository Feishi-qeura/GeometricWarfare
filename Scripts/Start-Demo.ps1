param([string]$EngineRoot, [switch]$Screenshot, [switch]$Stress)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
if (-not $EngineRoot) {
    $registered = (Get-ItemProperty 'HKLM:\SOFTWARE\EpicGames\Unreal Engine\5.8' -ErrorAction SilentlyContinue).InstalledDirectory
    $candidates = @($env:UE_ENGINE_ROOT)
    if ($registered) { $candidates += $registered; $candidates += Join-Path $registered 'UE_5.8' }
    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path (Join-Path $candidate 'Engine\Binaries\Win64\UnrealEditor.exe'))) { $EngineRoot=$candidate; break }
    }
}
if (-not $EngineRoot) { throw 'Pass -EngineRoot with your UE 5.8 folder.' }
# This development demo explicitly opts into local simulation; normal startup waits for the SDK provider.
$arguments = @("`"$projectRoot\GeometricWarfare.uproject`"", '/Engine/Maps/Entry', '-game', '-GWLocalTest', '-windowed', '-ForceRes', '-ResX=1920', '-ResY=1080', '-NoSplash', '-NoSound', '-DDC-ForceMemoryCache', '-DisablePlugins=RiderLink')
if ($Screenshot) { $arguments += '-GWVisualTest' }
if ($Stress) { $arguments += '-GWStressTest' }
# Running this script opens the interactive demo window.
Write-Output 'Starting the development demo with explicit -GWLocalTest.'
Start-Process -FilePath (Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe') -ArgumentList $arguments
