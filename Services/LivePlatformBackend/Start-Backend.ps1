param([string]$EnvironmentFile = (Join-Path $PSScriptRoot '.env'))
$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot
$backendPython = Join-Path $PSScriptRoot '.venv\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $backendPython)) {
    throw 'Create the isolated Python environment and install requirements.lock.txt first; see README.md.'
}
if (Test-Path -LiteralPath $EnvironmentFile) {
    foreach ($backendLine in Get-Content -LiteralPath $EnvironmentFile) {
        if ([string]::IsNullOrWhiteSpace($backendLine) -or $backendLine.TrimStart().StartsWith('#')) { continue }
        $backendPair = $backendLine.Split('=', 2)
        if ($backendPair.Count -ne 2 -or $backendPair[0] -notmatch '^BACKEND_[A-Z0-9_]+$') {
            throw 'Invalid backend environment entry; values are not logged.'
        }
        [Environment]::SetEnvironmentVariable($backendPair[0], $backendPair[1], 'Process')
    }
}
# main.py fixes the bind address to 127.0.0.1 and workers to one.
# Secrets are environment values, never command-line arguments.
& $backendPython -m live_backend.main
exit $LASTEXITCODE
