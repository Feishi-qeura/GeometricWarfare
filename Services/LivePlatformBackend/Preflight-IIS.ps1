$ErrorActionPreference = 'Stop'
$backendAppCmd = Join-Path $env:WINDIR 'System32\inetsrv\appcmd.exe'
$backendModules = @()
if (Test-Path -LiteralPath $backendAppCmd) {
    $backendModules = & $backendAppCmd list modules
}
[pscustomobject]@{
    IISAppCmdPresent = Test-Path -LiteralPath $backendAppCmd
    URLRewriteModuleDetected = [bool]($backendModules -match 'RewriteModule')
    ARRModuleDetected = [bool]($backendModules -match 'ApplicationRequestRouting')
    ProxyAndAllowedVariables = 'Operator must inspect existing site/server configuration; this script makes no changes.'
    PublicHTTPS = 'https://example.com/ certificate/binding and existing business paths require server-side inspection.'
}
