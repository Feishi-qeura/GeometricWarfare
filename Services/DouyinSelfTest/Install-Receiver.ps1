param(
    [string]$TestDomain = 'test.example.com',
    [string]$CertificateThumbprint = '',
    [string]$PythonExecutable = 'C:\Services\LivePlatformBackend\.venv\Scripts\python.exe',
    [string]$ServiceDirectory = 'C:\Services\DouyinSelfTest',
    [string]$WebDirectory = 'C:\inetpub\GeometricWarfareSelfTest',
    [string]$RoomId = '1000000000000000000',
    [string]$PushSecretFile = ''
)
$ErrorActionPreference = 'Stop'
$siteName = 'GeometricWarfare-DouyinSelfTest'
$taskName = 'GeometricWarfare-DouyinSelfTest'
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
if (-not ([Security.Principal.WindowsPrincipal]::new($identity)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Run from an Administrator Windows PowerShell window.' }
if ($TestDomain -notmatch '^test\.[a-z0-9][a-z0-9.-]+\.[a-z]{2,}$') { throw 'Use a separate test.<your-domain> hostname.' }
if ($RoomId -notmatch '^1[0-9]{18}$') { throw 'Mock RoomId must be 19 digits and begin with 1.' }
if (-not (Test-Path -LiteralPath $PythonExecutable -PathType Leaf)) { throw 'Existing backend Python not found; pass -PythonExecutable.' }
Import-Module WebAdministration
if ((Get-Website -Name $siteName -ErrorAction SilentlyContinue) -or (Test-Path "IIS:\AppPools\$siteName") -or (Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue)) { throw 'This test site/task already exists. Inspect it instead of overwriting it.' }
foreach ($target in @($ServiceDirectory,$WebDirectory)) {
    if (Test-Path -LiteralPath $target) { throw "Choose a fresh deployment directory: $target" }
}
if (@(Get-WebGlobalModule | Where-Object name -eq 'RewriteModule').Count -eq 0) { throw 'IIS URL Rewrite is required.' }
if (-not (Get-WebConfigurationProperty -PSPath 'MACHINE/WEBROOT/APPHOST' -Filter 'system.webServer/proxy' -Name enabled).Value) { throw 'The existing IIS ARR reverse proxy must be enabled first.' }
if (@(Get-WebBinding | Where-Object bindingInformation -Like "*:$TestDomain").Count -gt 0) { throw 'The requested test hostname already has an IIS binding.' }
$eligible = @(Get-ChildItem Cert:\LocalMachine\My | Where-Object {
    $cert = $_
    $cert.HasPrivateKey -and $cert.NotBefore -lt (Get-Date) -and $cert.NotAfter -gt (Get-Date) -and
    (@($cert.EnhancedKeyUsageList | Where-Object {$_.ObjectId.Value -eq '1.3.6.1.5.5.7.3.1'}).Count -gt 0) -and
    (@($cert.DnsNameList | Where-Object {$TestDomain -like $_.Unicode}).Count -gt 0)
})
if ($CertificateThumbprint) { $eligible = @($eligible | Where-Object Thumbprint -eq $CertificateThumbprint.Replace(' ','')) }
if ($eligible.Count -ne 1) { throw 'Install a valid HTTPS certificate for the test hostname into LocalMachine/My, then pass -CertificateThumbprint. No site has been changed.' }
$secret = 'default'
if ($PushSecretFile) { $secret = [IO.File]::ReadAllText([IO.Path]::GetFullPath($PushSecretFile)).Trim() }
if (-not $secret -or $secret.Length -gt 8192) { throw 'Push secret file is empty or invalid. This is the data-push key, not AppSecret.' }
$serviceFull = [IO.Path]::GetFullPath($ServiceDirectory)
$webFull = [IO.Path]::GetFullPath($WebDirectory)
if ($serviceFull.StartsWith($webFull.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase) -or $serviceFull -eq $webFull) { throw 'Keep service/config/data outside the web directory.' }
$sourceFiles = @('receiver.py','Start-Receiver.ps1','Inspect-Receiver.ps1','README.md')
foreach ($name in $sourceFiles) { if (-not (Test-Path -LiteralPath (Join-Path $PSScriptRoot $name))) {throw "Missing deployment source: $name"} }
$listener = Get-NetTCPConnection -State Listen -LocalPort 8766 -ErrorAction SilentlyContinue
if ($listener) {throw 'Port 8766 is already in use; no service/site has been changed.'}
New-Item -ItemType Directory -Path $ServiceDirectory,$WebDirectory | Out-Null
foreach ($name in $sourceFiles) {Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination (Join-Path $ServiceDirectory $name)}
$settings = @{room_id=$RoomId;push_secret=$secret;database=(Join-Path $ServiceDirectory 'data/mock-inbox.sqlite3');port=8766}
$settings | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $ServiceDirectory 'settings.json') -Encoding UTF8
# Admin/SYSTEM only; IIS only needs the separate web directory containing the proxy rule.
& icacls.exe $ServiceDirectory /inheritance:r /grant:r '*S-1-5-18:(OI)(CI)F' '*S-1-5-32-544:(OI)(CI)F' | Out-Null
if ($LASTEXITCODE -ne 0) {throw 'Service directory ACL setup failed.'}
@'
<?xml version="1.0" encoding="UTF-8"?>
<configuration><system.webServer>
<rewrite><rules><rule name="MockInbox" stopProcessing="true"><match url="^(.*)$" />
<conditions><add input="{HTTPS}" pattern="^on$" /></conditions>
<action type="Rewrite" url="http://127.0.0.1:8766/{R:1}" appendQueryString="false" />
</rule></rules></rewrite>
<security><requestFiltering><requestLimits maxAllowedContentLength="1048576" /></requestFiltering></security>
</system.webServer></configuration>
'@ | Set-Content -LiteralPath (Join-Path $WebDirectory 'web.config') -Encoding UTF8
$createdSite = $createdPool = $createdTask = $false
try {
    New-WebAppPool -Name $siteName | Out-Null
    $createdPool = $true
    Set-ItemProperty "IIS:\AppPools\$siteName" -Name managedRuntimeVersion -Value ''
    New-Website -Name $siteName -Port 443 -HostHeader $TestDomain -Ssl -PhysicalPath $WebDirectory -ApplicationPool $siteName | Out-Null
    $createdSite = $true
    Set-WebBinding -Name $siteName -Protocol https -BindingInformation "*:443:$TestDomain" -PropertyName sslFlags -Value 1
    (Get-WebBinding -Name $siteName -Protocol https).AddSslCertificate($eligible[0].Thumbprint,'My')
    $taskAction = New-ScheduledTaskAction -Execute $PythonExecutable -Argument ('"'+(Join-Path $ServiceDirectory 'receiver.py')+'" serve --config "'+(Join-Path $ServiceDirectory 'settings.json')+'"') -WorkingDirectory $ServiceDirectory
    $taskSettings = New-ScheduledTaskSettingsSet -RestartCount 10 -RestartInterval (New-TimeSpan -Minutes 1) -ExecutionTimeLimit ([TimeSpan]::Zero) -MultipleInstances IgnoreNew -StartWhenAvailable
    Register-ScheduledTask -TaskName $taskName -Action $taskAction -Trigger (New-ScheduledTaskTrigger -AtStartup) -Settings $taskSettings -User SYSTEM -RunLevel Highest | Out-Null
    $createdTask = $true
    Start-ScheduledTask -TaskName $taskName
    $ready = $false
    for ($attempt=0;$attempt -lt 10;$attempt++) {
        try {$localStatus=Invoke-RestMethod 'http://127.0.0.1:8766/healthz' -TimeoutSec 2; $ready=$localStatus.status -eq 'mock_receiver_ready'; if($ready){break}} catch {}
        Start-Sleep -Seconds 1
    }
    if (-not $ready) {throw 'Mock receiver failed its local readiness check.'}
    Write-Output "LOCAL_READY=http://127.0.0.1:8766/healthz"
    Write-Output "SELFTEST_DOMAIN=https://$TestDomain"
    Write-Output 'NEXT: Confirm DNS and a public HTTPS HEAD 200 before saving this domain in the Douyin self-test tool.'
} catch {
    # Roll back only the new resources created by this invocation; preserve files for diagnosis.
    if ($createdTask) {Stop-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue; Unregister-ScheduledTask -TaskName $taskName -Confirm:$false}
    if ($createdSite) {Remove-Website -Name $siteName}
    if ($createdPool) {Remove-WebAppPool -Name $siteName}
    throw
}
