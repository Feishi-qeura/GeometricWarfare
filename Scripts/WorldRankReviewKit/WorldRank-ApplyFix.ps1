param(
    [string]$BackendDirectory='C:\Services\LivePlatformBackend',
    [ValidateSet('Install','RetryFailed')][string]$Mode='Install',
    [string]$EnvironmentFile='',
    [string]$PythonExecutable='',
    [int]$Port=8765
)
$ErrorActionPreference='Stop'
$BackendDirectory=[IO.Path]::GetFullPath($BackendDirectory)
$python=if($PythonExecutable){[IO.Path]::GetFullPath($PythonExecutable)}else{Join-Path $BackendDirectory '.venv\Scripts\python.exe'}
if(-not(Test-Path -LiteralPath $python -PathType Leaf)){throw 'Backend Python not found.'}
$expectedOld='e96ecb3195eea95ed4e0b5405f9441b9f98d2be03b88f518467b801f94c780da'
$expectedNew='9100d508f2ccf3ee513f426f3819c8b10e8292b35794a1b0e2203de6983f99ba'
$target=Join-Path $BackendDirectory 'live_backend\upstream.py'
if(-not(Test-Path -LiteralPath $target -PathType Leaf)){throw 'Installed upstream.py not found.'}
$targetHash=(Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
if($Mode -eq 'Install') {
    if($targetHash -eq $expectedNew){Write-Output 'Patch already installed. Restart the backend before RetryFailed.';exit 0}
    if($targetHash -ne $expectedOld){throw 'Installed source differs from the server fingerprint supplied in this incident. No files changed.'}
    $payload=Join-Path $PSScriptRoot 'upstream.py'
    if((Get-FileHash -LiteralPath $payload -Algorithm SHA256).Hash -ne $expectedNew){throw 'Patch payload hash mismatch.'}
    & $python -c 'import ast,pathlib,sys; ast.parse(pathlib.Path(sys.argv[1]).read_bytes())' $payload
    if($LASTEXITCODE -ne 0){throw 'Python syntax validation failed.'}
    $backup=$target+'.errcode-backup-'+(Get-Date -Format 'yyyyMMdd-HHmmssfff')
    if(Test-Path -LiteralPath $backup){throw 'Preserve existing source backup.'}
    $acl=Get-Acl -LiteralPath $target
    New-Item -ItemType File -Path $backup|Out-Null
    Set-Acl -LiteralPath $backup -AclObject $acl
    [IO.File]::WriteAllBytes($backup,[IO.File]::ReadAllBytes($target))
    [IO.File]::WriteAllBytes($target,[IO.File]::ReadAllBytes($payload))
    if((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $expectedNew){throw 'Installed payload verification failed.'}
    Write-Output 'Errcode response compatibility patch installed; original source backed up. No .env, scores or queue changes.'
    Write-Output 'Restart LivePlatformBackend using its existing management method, then run this script with -Mode RetryFailed.'
    exit 0
}
if($targetHash -ne $expectedNew){throw 'Install the verified patch first.'}
if($Port -lt 1 -or $Port -gt 65535){throw 'Invalid backend port.'}
$listener=Get-NetTCPConnection -LocalAddress 127.0.0.1 -LocalPort $Port -State Listen -ErrorAction Stop|Select-Object -ExpandProperty OwningProcess -Unique
if(@($listener).Count -ne 1){throw 'Expected exactly one backend listener.'}
$backendProcess=Get-Process -Id $listener -ErrorAction Stop
$nativeProcess=Get-CimInstance Win32_Process -Filter "ProcessId=$listener"
if($nativeProcess.CommandLine -notmatch 'live_backend[.\\](main|app)'){throw 'Listener is not the expected backend process. No queue changes.'}
$pythonInvocation='(?i)^(?:"'+[regex]::Escape($python)+'"|'+[regex]::Escape($python)+')(?:\s|$)'
$belongsToSelectedPython=$nativeProcess.ExecutablePath -eq $python -or $nativeProcess.CommandLine -match $pythonInvocation
if(-not $belongsToSelectedPython) {
    # A Windows venv redirector can spawn the base interpreter. In that case
    # its immediate parent must be the selected venv interpreter invocation.
    $parent=Get-CimInstance Win32_Process -Filter "ProcessId=$($nativeProcess.ParentProcessId)"
    $belongsToSelectedPython=$parent.ExecutablePath -eq $python -or $parent.CommandLine -match $pythonInvocation
}
if(-not $belongsToSelectedPython){throw 'Listener belongs to another Python environment. No queue changes.'}
if($backendProcess.StartTime.ToUniversalTime() -lt (Get-Item -LiteralPath $target).LastWriteTimeUtc){throw 'Backend process predates the installed patch. Restart it before retrying.'}
$arguments=@((Join-Path $PSScriptRoot 'errcode_recovery.py'),'--backend-directory',$BackendDirectory,'--mode','locate','--job-id','1')
if($EnvironmentFile){$arguments+=@('--environment-file',[IO.Path]::GetFullPath($EnvironmentFile))}
$json=& $python @arguments
if($LASTEXITCODE -ne 0){$json;throw 'Recovery precondition failed. No queue changes.'}
$location=($json -join [Environment]::NewLine)|ConvertFrom-Json
$database=[IO.Path]::GetFullPath($location.database_file)
$snapshot=$database+'.errcode-fix-backup-'+(Get-Date -Format 'yyyyMMdd-HHmmssfff')+'.sqlite3'
if(Test-Path -LiteralPath $snapshot){throw 'Preserve existing database backup.'}
$databaseAcl=Get-Acl -LiteralPath $database
New-Item -ItemType File -Path $snapshot|Out-Null
Set-Acl -LiteralPath $snapshot -AclObject $databaseAcl
$arguments=@((Join-Path $PSScriptRoot 'errcode_recovery.py'),'--backend-directory',$BackendDirectory,'--mode','retry','--job-id','1','--backup-file',$snapshot)
if($EnvironmentFile){$arguments+=@('--environment-file',[IO.Path]::GetFullPath($EnvironmentFile))}
& $python @arguments
if($LASTEXITCODE -ne 0){throw 'Recovery did not complete. Keep the private snapshot and inspect the safe error code.'}
Write-Output 'Wait 30 seconds, then rerun WorldRank-Review.ps1. Confirm both score API done counters increase.'
