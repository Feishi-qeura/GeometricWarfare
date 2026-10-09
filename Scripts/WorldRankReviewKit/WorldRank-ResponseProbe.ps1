param([string]$BackendDirectory='C:\Services\LivePlatformBackend',[string]$EnvironmentFile='',[int]$ExpectedJobId=1)
$ErrorActionPreference='Stop'
$BackendDirectory=[IO.Path]::GetFullPath($BackendDirectory)
$python=Join-Path $BackendDirectory '.venv\Scripts\python.exe'
if(-not(Test-Path -LiteralPath $python -PathType Leaf)){throw 'Backend Python not found. Specify the installed backend directory.'}
$probeArguments=@((Join-Path $PSScriptRoot 'response_probe.py'),'--backend-directory',$BackendDirectory,'--expected-job-id',[string]$ExpectedJobId)
if($EnvironmentFile){$probeArguments+=@('--environment-file',[IO.Path]::GetFullPath($EnvironmentFile))}
# All secrets remain in the server process; the probe prints only fixed-key
# response types and numeric status/error codes, never body previews.
& $python @probeArguments
exit $LASTEXITCODE
