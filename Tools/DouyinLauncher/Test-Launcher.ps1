param([string]$BuildDirectory = '')
$ErrorActionPreference = 'Stop'
if (-not $BuildDirectory) { $BuildDirectory = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../tmp/DouyinLauncherBuild')) }
if (-not [Diagnostics.ProcessStartInfo].GetProperty('ArgumentList')) { throw 'Run this test with PowerShell 7 (framework Windows quoting reference)' }
$testRoot = Join-Path $BuildDirectory ('Tests/space path ' + [Guid]::NewGuid().ToString('N'))
$childFolder = Join-Path $testRoot 'GeometricWarfare/Binaries/Win64'
New-Item -ItemType Directory -Path $childFolder -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $BuildDirectory 'DouyinLauncher.exe') -Destination $testRoot
Copy-Item -LiteralPath (Join-Path $BuildDirectory 'FakeChild.exe') -Destination (Join-Path $childFolder 'GeometricWarfare.exe')
function Invoke-LauncherCase([string[]]$Arguments, [int]$ExpectedExit, [bool]$ExpectChild) {
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = Join-Path $testRoot 'DouyinLauncher.exe'
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    foreach ($arg in $Arguments) { $info.ArgumentList.Add($arg) }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $info
    if (-not $process.Start()) { throw 'Cannot start launcher fixture' }
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    try {
        if (-not $process.WaitForExit(10000)) { $process.Kill($true); throw 'Launcher fixture timed out' }
        if ($process.ExitCode -ne $ExpectedExit) { throw "Launcher returned $($process.ExitCode), expected $ExpectedExit" }
        if ($stderr.Result) { throw 'Launcher unexpectedly wrote stderr' }
        if ($ExpectChild) { if ($stdout.Result -ne "PASS fake child exact args/stdin`n") { throw 'Fake child contract failed' } }
        elseif ($stdout.Result) { throw 'Rejected arguments launched a child or produced output' }
    } finally { $process.Dispose() }
}
$normal = @('-testcase=credential', 'a b', 'quote"and\', 'trail \\', '', '中文 参数')
$fake = '假 token "\尾'
$tokenArgument = '-ToKeN=' + $fake
Invoke-LauncherCase (@($tokenArgument, '-gWLoCaLtEsT') + $normal) 37 $true
Invoke-LauncherCase (@('-token', $fake, '-GWLocalTest') + $normal) 37 $true
Invoke-LauncherCase @('-testcase=local', '-gWLocalTest', '-GWCredentialStdin') 37 $true
Invoke-LauncherCase @('-token=fake', '-TOKEN=other') 2 $false
Invoke-LauncherCase @("-token=fake`nextra") 2 $false
Invoke-LauncherCase @('-token=' + ('x' * 16384)) 2 $false
Invoke-LauncherCase @('-token=') 2 $false
Invoke-LauncherCase @('-token') 2 $false
$boundaryArgument = '-token=' + ('x' * 16383)
Invoke-LauncherCase @($boundaryArgument, '-testcase=boundary', '-gWLocalTest=1') 37 $true
Invoke-LauncherCase @('-token=' + ('假' * 5462)) 2 $false
Copy-Item -LiteralPath (Join-Path $BuildDirectory 'FakeChild.exe') -Destination (Join-Path $childFolder 'GeometricWarfare-Win64-Shipping.exe')
Invoke-LauncherCase (@($tokenArgument, '-GWLocalTest') + $normal) 37 $true
Write-Output 'PASS: 11 launcher cases (Development and Shipping); token stripped, stdin exact UTF-8/LF, local flag guarded, Windows quoting including empty/Unicode/quotes/backslashes and executable path spaces, duplicate/newline/missing/oversize rejected, 16 KiB boundary accepted, child exit 37 preserved'
