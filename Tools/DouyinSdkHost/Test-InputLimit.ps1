param([string]$HostExecutable = '')
$ErrorActionPreference = 'Stop'
if (-not $HostExecutable) { $HostExecutable = Join-Path $PSScriptRoot '../../Plugins/DouyinLiveProvider/Binaries/Win64/SdkHost/DouyinSdkHost.exe' }
$info = New-Object Diagnostics.ProcessStartInfo
$info.FileName = [IO.Path]::GetFullPath($HostExecutable)
$info.WorkingDirectory = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../tmp/douyin-sdk-research/host-limit-work'))
New-Item -ItemType Directory -Path $info.WorkingDirectory -Force | Out-Null
$info.Arguments = '-batchmode -nographics -logFile -'
$info.UseShellExecute = $false
$info.CreateNoWindow = $true
$info.RedirectStandardInput = $true
$info.RedirectStandardOutput = $true
$info.RedirectStandardError = $true
$process = New-Object Diagnostics.Process
$process.StartInfo = $info
if (-not $process.Start()) { throw 'Cannot start SDK host' }
$stderr = $process.StandardError.ReadToEndAsync()
$stdout = $process.StandardOutput.ReadToEndAsync()
try {
    $write = $process.StandardInput.WriteLineAsync(('x' * (4 * 1024 * 1024 + 1)))
    if (-not $process.WaitForExit(15000)) { throw 'Oversize rejection did not terminate host' }
    # Closing stdin during rejection can fault the parent's write; observe it.
    try { $write.GetAwaiter().GetResult() | Out-Null } catch { }
    $states = @($stdout.Result -split "`n" | Where-Object { $_.StartsWith('GWSDK/1 ') } | ForEach-Object { $_.Substring(8) | ConvertFrom-Json })
    if (@($states | Where-Object { $_.op -eq 'protocol' -and $_.err_code -eq -1002 -and $_.state -eq 'failed' }).Count -ne 1) { throw 'Input limit did not fail closed' }
    if ($process.ExitCode -ne 0) { throw "Host exited $($process.ExitCode)" }
    Write-Output 'PASS: input record above 4 MiB rejected with failed state; output drains and child exits 0'
} finally {
    if (-not $process.HasExited) { $process.Kill() }
    $process.Dispose()
}
