param([string]$EngineRoot, [switch]$Game)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
if (-not $EngineRoot) {
    $registered = (Get-ItemProperty 'HKLM:\SOFTWARE\EpicGames\Unreal Engine\5.8' -ErrorAction SilentlyContinue).InstalledDirectory
    $candidates = @($env:UE_ENGINE_ROOT)
    if ($registered) { $candidates += $registered; $candidates += Join-Path $registered 'UE_5.8' }
    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path (Join-Path $candidate 'Engine\Build\BatchFiles\Build.bat'))) { $EngineRoot=$candidate; break }
    }
}
if (-not $EngineRoot) { throw 'Pass -EngineRoot with your UE 5.8 folder.' }
$target = if ($Game) { 'GeometricWarfare' } else { 'GeometricWarfareEditor' }
& (Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat') $target Win64 Development "-Project=$projectRoot\GeometricWarfare.uproject" -WaitMutex -NoHotReloadFromIDE -NoXGE
if ($LASTEXITCODE -ne 0) { throw 'Unreal build failed.' }
