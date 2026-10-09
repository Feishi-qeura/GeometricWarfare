param([string]$HostExecutable = '')
$ErrorActionPreference = 'Stop'
if (-not $HostExecutable) { $HostExecutable = Join-Path $PSScriptRoot '../../Plugins/DouyinLiveProvider/Binaries/Win64/SdkHost/DouyinSdkHost.exe' }
$info = New-Object Diagnostics.ProcessStartInfo
$info.FileName = [IO.Path]::GetFullPath($HostExecutable)
$info.WorkingDirectory = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../tmp/douyin-sdk-research/host-protocol-work'))
New-Item -ItemType Directory -Path $info.WorkingDirectory -Force | Out-Null
$testLog = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../tmp/douyin-sdk-research/host-protocol.log'))
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
    $process.StandardInput.WriteLine('{broken-json')
    $process.StandardInput.WriteLine('{"op":"init","id":"missing","app_id":"ttTest","token":""}')
    $process.StandardInput.WriteLine('{"op":"ack","id":"early","room_id":"test","msg_id":"消息","msg_type":"live_comment"}')
    $process.StandardInput.WriteLine('{"op":"init","id":"forbidden","app_id":"ttTest","token":"PROTOCOL_TEST_NOT_A_REAL_TOKEN","msg_types":["live_fansclub"]}')
    $process.StandardInput.WriteLine('{"op":"stop","id":"exit"}')
    $process.StandardInput.Flush()
    $frames = New-Object Collections.Generic.List[object]
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    $read = $process.StandardOutput.ReadLineAsync()
    while ([DateTime]::UtcNow -lt $deadline) {
        if (-not $read.Wait(250)) { continue }
        $line = $read.Result
        if ($null -eq $line) { break }
        if ($line.Contains('PROTOCOL_TEST_NOT_A_REAL_TOKEN')) { throw 'Credential leaked to output' }
        if ($line.StartsWith('GWSDK/1 ')) {
            $frame = $line.Substring(8) | ConvertFrom-Json
            $frames.Add($frame)
            if ($frame.request_id -eq 'exit') { break }
        }
        $read = $process.StandardOutput.ReadLineAsync()
    }
    foreach ($expect in @(
        @{ id = 'missing'; code = -1004 }, @{ id = 'early'; code = -1005 }, @{ id = 'forbidden'; code = -1001 }, @{ id = 'exit'; code = 0 }
    )) {
        $match = @($frames | Where-Object { $_.request_id -eq $expect.id -and $_.err_code -eq $expect.code })
        if ($match.Count -ne 1) { throw "Expected exactly one response $($expect.id) code=$($expect.code); frames=$($frames.Count), exited=$($process.HasExited); inspect $testLog" }
    }
    if (@($frames | Where-Object { $_.op -eq 'protocol' -and $_.err_code -eq -1001 }).Count -ne 1) { throw 'Malformed JSON was not rejected' }
    foreach ($initId in @('missing', 'forbidden')) {
        if (@($frames | Where-Object { $_.request_id -eq $initId -and $_.success -eq $false -and $_.state -eq 'failed' }).Count -ne 1) {
            throw 'Initialization rejection did not produce terminal failed state'
        }
    }
    $remainingOutput = $process.StandardOutput.ReadToEndAsync()
    if (-not $process.WaitForExit(10000)) { throw 'SDK host did not exit' }
    if ($remainingOutput.Result.Contains('PROTOCOL_TEST_NOT_A_REAL_TOKEN')) { throw 'Credential leaked to trailing output' }
    if ($stderr.Result.Contains('PROTOCOL_TEST_NOT_A_REAL_TOKEN')) { throw 'Credential leaked to stderr' }
    Write-Output "PASS: protocol framing, malformed JSON, missing token, uninitialized ACK, disabled subscription, graceful stop; frames=$($frames.Count), exit=$($process.ExitCode)"
} finally {
    if (-not $process.HasExited) { $process.Kill() }
    $process.Dispose()
}
