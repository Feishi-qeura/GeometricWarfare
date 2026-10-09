param([string]$PythonExecutable = 'C:\Services\LivePlatformBackend\.venv\Scripts\python.exe', [switch]$Recent)
$ErrorActionPreference = 'Stop'
$operation = if ($Recent) { 'recent' } else { 'status' }
& $PythonExecutable (Join-Path $PSScriptRoot 'receiver.py') $operation --config (Join-Path $PSScriptRoot 'settings.json')
exit $LASTEXITCODE
