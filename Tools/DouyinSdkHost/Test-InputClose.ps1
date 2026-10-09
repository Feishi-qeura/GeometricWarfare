param([string]$HostExecutable = '')
$ErrorActionPreference = 'Stop'
if (-not $HostExecutable) { $HostExecutable = Join-Path $PSScriptRoot '../../Plugins/DouyinLiveProvider/Binaries/Win64/SdkHost/DouyinSdkHost.exe' }
$info = New-Object Diagnostics.ProcessStartInfo
$info.FileName = [IO.Path]::GetFullPath($HostExecutable)
$info.WorkingDirectory = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../tmp/douyin-sdk-research/host-eof-work'))
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
try {
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    $read = $process.StandardOutput.ReadLineAsync()
    $ready = $false
    while ([DateTime]::UtcNow -lt $deadline) {
        if (-not $read.Wait(250)) { continue }
        if ($null -eq $read.Result) { break }
        if ($read.Result.StartsWith('GWSDK/1 ')) {
            $frame = $read.Result.Substring(8) | ConvertFrom-Json
            if ($frame.state -eq 'ready') { $ready = $true; break }
        }
        $read = $process.StandardOutput.ReadLineAsync()
    }
    if (-not $ready) { throw 'Host never became ready' }
    $process.StandardInput.Close()
    $tail = $process.StandardOutput.ReadToEndAsync()
    if (-not $process.WaitForExit(10000)) { throw 'Host did not exit after parent input closed' }
    $states = @($tail.Result -split "`n" | Where-Object { $_.StartsWith('GWSDK/1 ') } | ForEach-Object { $_.Substring(8) | ConvertFrom-Json })
    if (@($states | Where-Object { $_.op -eq 'stop' -and $_.state -eq 'stopped' -and $_.success }).Count -ne 1) { throw 'Missing graceful EOF stop response' }
    if ($process.ExitCode -ne 0) { throw "Host exited $($process.ExitCode)" }
    Write-Output 'PASS: parent stdin EOF triggers host shutdown, output drains, child exits 0'
} finally {
    if (-not $process.HasExited) { $process.Kill() }
    $process.Dispose()
}
