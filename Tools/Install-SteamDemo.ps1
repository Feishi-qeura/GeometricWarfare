param([Parameter(Mandatory)] [string]$Package)
$ErrorActionPreference = 'Stop'
$workspace = Split-Path $PSScriptRoot -Parent
$target = [IO.Path]::GetFullPath('D:\demo\GeometricWarfare_DEMO')
$backup = [IO.Path]::GetFullPath('D:\demo\GeometricWarfare_DEMO.backup-steamfix-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
$packagePath = (Resolve-Path -LiteralPath $Package).Path
if ($target -ne 'D:\demo\GeometricWarfare_DEMO' -or
    (Split-Path $backup -Parent) -ne 'D:\demo' -or
    -not $backup.StartsWith('D:\demo\GeometricWarfare_DEMO.backup-steamfix-') -or
    -not $packagePath.StartsWith($workspace + '\Saved\SteamFix\')) { throw 'Unexpected deployment path' }
if (-not (Test-Path -LiteralPath (Join-Path $packagePath 'GeometricWarfare.exe'))) { throw 'Verified package entry point is missing' }
if (Get-Process GeometricWarfare -ErrorAction SilentlyContinue) { throw 'Close the game before replacing its files' }
$saveRoot = Join-Path $target 'GeometricWarfare\Saved'
$saveHashes = @{}
if (Test-Path -LiteralPath $saveRoot) {
    Get-ChildItem -LiteralPath $saveRoot -File -Recurse | ForEach-Object {
        $saveHashes[$_.FullName.Substring($saveRoot.Length)] = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
    }
}
# Keep the entire previous installation for rollback, including all old logs and test folders.
# Both resolved move endpoints were checked above, and the Steam shortcut retains its original target path.
Move-Item -LiteralPath $target -Destination $backup
New-Item -ItemType Directory -Path $target | Out-Null
$excluded = @(Get-ChildItem -LiteralPath (Join-Path $packagePath 'GeometricWarfare') -Directory |
    Where-Object { $_.Name -like 'Saved*' } | ForEach-Object { $_.FullName })
$copyArgs = @($packagePath, $target, '/E', '/R:1', '/W:1', '/NFL', '/NDL', '/NJH', '/NJS')
if ($excluded.Count -gt 0) { $copyArgs += '/XD'; $copyArgs += $excluded }
& robocopy.exe @copyArgs | Out-Null
if ($LASTEXITCODE -ge 8) { throw "Package copy failed; original installation is safe at $backup" }
$previousSaves = Join-Path $backup 'GeometricWarfare\Saved'
if (Test-Path -LiteralPath $previousSaves) {
    Copy-Item -LiteralPath $previousSaves -Destination (Join-Path $target 'GeometricWarfare\Saved') -Recurse
}
foreach ($relative in $saveHashes.Keys) {
    if ((Get-FileHash -LiteralPath ($saveRoot + $relative) -Algorithm SHA256).Hash -ne $saveHashes[$relative]) {
        throw "Saved data differs after installation: $relative; backup: $backup"
    }
}
$verified = 0
Get-ChildItem -LiteralPath $packagePath -Recurse -File | Where-Object {
    $_.FullName.Substring($packagePath.Length) -notmatch '^\\GeometricWarfare\\Saved[^\\]*\\'
} | ForEach-Object {
    $relative = $_.FullName.Substring($packagePath.Length)
    if ((Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash -ne
        (Get-FileHash -LiteralPath ($target + $relative) -Algorithm SHA256).Hash) { throw "Installed file differs: $relative" }
    $verified++
}
if ($verified -eq 0) { throw 'No installed package files were verified' }
$record = [ordered]@{Target=$target; Backup=$backup; Package=$packagePath; VerifiedFiles=$verified; PreservedSaveFiles=$saveHashes.Count}
$record | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $workspace 'Saved\SteamFix\installation.json') -Encoding utf8
$record | ConvertTo-Json
exit 0
