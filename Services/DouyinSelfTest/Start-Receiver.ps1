param([string]$PythonExecutable = 'C:\Services\LivePlatformBackend\.venv\Scripts\python.exe')
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $PythonExecutable -PathType Leaf)) { throw 'Python not found. Pass -PythonExecutable with your installed Python 3.12 executable.' }
& $PythonExecutable (Join-Path $PSScriptRoot 'receiver.py') serve --config (Join-Path $PSScriptRoot 'settings.json')
exit $LASTEXITCODE
