param(
    [Parameter(Mandatory)] [string]$GameRoot,
    [switch]$ShortcutEnvironment,
    [switch]$InitOnly,
    [switch]$ShowInvite,
    [switch]$Render,
    [switch]$Launcher
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$kind = if ($ShortcutEnvironment) { 'shortcut' } else { 'direct' }
$logDir = Join-Path $root 'Saved\SteamFix'
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$log = Join-Path $logDir "$kind.log"
$exe = Join-Path $GameRoot 'GeometricWarfare\Binaries\Win64\GeometricWarfare.exe'
if ($Launcher) { $exe = Join-Path $GameRoot 'GeometricWarfare.exe' }
$previousApp = $env:SteamAppId
$previousGame = $env:SteamGameId
try {
    # Reproduce the real non-Steam shortcut identity seen in Steam's gameprocess_log.txt.
    $env:SteamAppId = if ($ShortcutEnvironment) { '3812677243' } else { $null }
    $env:SteamGameId = if ($ShortcutEnvironment) { '16375324068921999360' } else { $null }
    $args = "-nosound -unattended -nosplash -GWNoIntro -saveddirsuffix=SteamFix-$kind -abslog=`"$log`""
    $args += if ($Render) { ' -RenderOffscreen -windowed -ResX=1280 -ResY=720' } else { ' -nullrhi' }
    $args += if ($InitOnly) { ' -ExecCmds="quit"' } else {
        ' -ExecCmds="Automation RunTests GW.Steam.Smoke" -TestExit="Automation Test Queue Empty"'
    }
    if ($ShowInvite) { $args += ' -GWTestSteamInviteUI' }
    $process = Start-Process -FilePath $exe -WorkingDirectory $GameRoot -ArgumentList $args -WindowStyle Hidden -PassThru
    if (-not $process.WaitForExit(90000)) {
        Stop-Process -Id $process.Id
        throw "Steam $kind test timed out: $log"
    }
    $contents = Get-Content -LiteralPath $log -Raw
    if ($contents -notmatch '\[AppId: 480\] Client API initialized 1') { throw "Steam $kind did not initialize as AppID 480: $log" }
    if ($contents -match 'SteamAPI failed to initialize|Fatal error:') { throw "Steam $kind failed: $log" }
    if (-not $InitOnly -and $contents -notmatch 'Test Completed\. Result=\{Success\} Name=\{Smoke\}') {
        throw "Steam $kind lobby smoke test did not pass: $log"
    }
    Write-Output "PASS: Steam $kind (AppID 480). Log: $log"
} finally {
    $env:SteamAppId = $previousApp
    $env:SteamGameId = $previousGame
}
