param(
    [string]$BackendDirectory='',
    [string]$EnvironmentFile='',
    [string]$PythonExecutable='',
    [ValidateSet('Diagnose','EnableUploads')][string]$Mode='Diagnose',
    [int]$Port=8765
)
$ErrorActionPreference='Stop'
if(-not $BackendDirectory){$BackendDirectory=Read-Host '输入服务器 LivePlatformBackend 所在目录（含 .venv 和 Start-Backend.ps1）'}
$BackendDirectory=[IO.Path]::GetFullPath($BackendDirectory)
if(-not $EnvironmentFile){$EnvironmentFile=Join-Path $BackendDirectory '.env'}
$EnvironmentFile=[IO.Path]::GetFullPath($EnvironmentFile)
$python=if($PythonExecutable){[IO.Path]::GetFullPath($PythonExecutable)}else{Join-Path $BackendDirectory '.venv\Scripts\python.exe'}
if(-not(Test-Path -LiteralPath $python -PathType Leaf)){throw '未找到后端 .venv Python。请填写实际后端目录。'}
if($Port -lt 1 -or $Port -gt 65535){throw '无效后端端口'}
$diagnosticArguments=@((Join-Path $PSScriptRoot 'diagnostics.py'),'--backend-directory',$BackendDirectory,'--environment-file',$EnvironmentFile,'--runtime-url',"http://127.0.0.1:$Port/healthz")
$json=& $python @diagnosticArguments
if($LASTEXITCODE -ne 0){$json;throw '诊断未完成。以上仅含错误码，不要发送 .env 或密钥。'}
$report=($json -join [Environment]::NewLine)|ConvertFrom-Json
if($Mode -eq 'Diagnose'){$json;exit 0}
if(-not $report.configuration.credentials_configured){throw '后端凭证未配置；请只在服务器配置，不要发到聊天。'}
if($report.blockers -contains 'head_job_failed'){throw '队列首项已被平台拒绝，先读取 head_job.error；本工具不会自动重试失败任务。'}
if($report.blockers -contains 'completion_time_unit_required'){throw '首项被截榜时间单位阻断，请先核实单位；本工具不猜单位或跳过队列。'}
$content=[IO.File]::ReadAllText($EnvironmentFile)
$uploadEntries=[regex]::Matches($content,'(?m)^BACKEND_ENABLE_UPLOADS=[^\r\n]*')
if($uploadEntries.Count -gt 1){throw '存在重复上传开关，请先消除歧义。'}
if($report.configuration.uploads_enabled){Write-Output '配置已启用上传。请核对运行中的后端是否已重启。';$json;exit 0}
$updated=if($uploadEntries.Count -eq 1){[regex]::Replace($content,'(?m)^BACKEND_ENABLE_UPLOADS=[^\r\n]*','BACKEND_ENABLE_UPLOADS=true')}else{$content.TrimEnd([char[]]"`r`n")+"`r`nBACKEND_ENABLE_UPLOADS=true`r`n"}
# Backup stays alongside the existing private server configuration. Apply the
# source ACL before any secret bytes are written into the fresh backup.
$backup=$EnvironmentFile+'.world-rank-backup-'+(Get-Date -Format 'yyyyMMdd-HHmmssfff')
if(Test-Path -LiteralPath $backup){throw '配置备份已存在，请保留原备份。'}
$originalBytes=[IO.File]::ReadAllBytes($EnvironmentFile)
$acl=Get-Acl -LiteralPath $EnvironmentFile
New-Item -ItemType File -Path $backup -ErrorAction Stop|Out-Null
Set-Acl -LiteralPath $backup -AclObject $acl
[IO.File]::WriteAllBytes($backup,$originalBytes)
$hasBom=$originalBytes.Length -ge 3 -and $originalBytes[0] -eq 239 -and $originalBytes[1] -eq 187 -and $originalBytes[2] -eq 191
[IO.File]::WriteAllText($EnvironmentFile,$updated,[Text.UTF8Encoding]::new($hasBom))
Write-Output '已启用 BACKEND_ENABLE_UPLOADS=true；其他配置、测试/正式环境和凭证保持原值。'
Write-Output '配置备份留在原 .env 私有目录；不要上传备份。'
Write-Output '请用原来的管理方式重启 LivePlatformBackend 后端进程/服务，然后再次运行 Diagnose。无需重启 IIS 或整台服务器。'
