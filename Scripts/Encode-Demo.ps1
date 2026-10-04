[CmdletBinding()]
param(
    [string]$FfmpegPath,
    [string]$FramesPath,
    [string]$OutputPath,
    [ValidateRange(1, 120)][int]$FrameRate = 12,
    [ValidateRange(0, 99999)][int]$ExpectedFrames = 0
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
if (-not $FramesPath) { $FramesPath = Join-Path $projectRoot 'Saved\DemoFrames' }
if (-not $OutputPath) { $OutputPath = Join-Path $projectRoot 'Saved\Videos\GeometricWarfare-Demo.mp4' }
if (-not $FfmpegPath) {
    $candidates = @(Get-ChildItem -LiteralPath (Join-Path $projectRoot 'Saved\VideoTools') -Filter 'ffmpeg.exe' -Recurse -File -ErrorAction SilentlyContinue)
    if ($candidates.Count -ne 1) { throw 'Pass -FfmpegPath with ffmpeg.exe, or place exactly one FFmpeg distribution in Saved/VideoTools.' }
    $FfmpegPath = $candidates[0].FullName
}
$FfmpegPath = (Resolve-Path -LiteralPath $FfmpegPath).Path
$ffprobePath = Join-Path (Split-Path $FfmpegPath -Parent) 'ffprobe.exe'
if (-not (Test-Path -LiteralPath $ffprobePath -PathType Leaf)) { throw "ffprobe.exe must be beside ffmpeg.exe: $ffprobePath" }
if (-not (Test-Path -LiteralPath $FramesPath -PathType Container)) { throw "Frame directory does not exist: $FramesPath" }
$FramesPath = (Resolve-Path -LiteralPath $FramesPath).Path
$OutputPath = [IO.Path]::GetFullPath($OutputPath)
if ([IO.Path]::GetExtension($OutputPath) -ne '.mp4') { throw 'OutputPath must use the .mp4 extension.' }

$frames = @(Get-ChildItem -LiteralPath $FramesPath -File -Filter 'Frame_*.png' | Sort-Object Name)
if ($frames.Count -eq 0) { throw "No Frame_00000.png sequence found in $FramesPath" }
for ($index = 0; $index -lt $frames.Count; $index++) {
    $expectedName = 'Frame_{0:D5}.png' -f $index
    if ($frames[$index].Name -cne $expectedName) { throw "Missing or incorrectly named frame: expected $expectedName, found $($frames[$index].Name)." }
    if ($frames[$index].Length -eq 0) { throw "Empty frame: $expectedName" }
}
if ($ExpectedFrames -gt 0 -and $frames.Count -ne $ExpectedFrames) {
    throw "Expected $ExpectedFrames frames, found $($frames.Count). Finish recording before encoding."
}

$outputDirectory = Split-Path $OutputPath -Parent
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
$temporaryOutput = Join-Path $outputDirectory (([IO.Path]::GetFileNameWithoutExtension($OutputPath)) + '.encoding-' + [Guid]::NewGuid().ToString('N') + '.mp4')
$metadataPath = [IO.Path]::ChangeExtension($OutputPath, '.metadata.json')
$framePattern = Join-Path $FramesPath 'Frame_%05d.png'
$ffmpegArguments = @(
    '-hide_banner', '-loglevel', 'warning', '-nostdin', '-y',
    '-framerate', "$FrameRate", '-start_number', '0', '-i', $framePattern,
    '-frames:v', "$($frames.Count)", '-an', '-c:v', 'libx264',
    '-preset', 'medium', '-crf', '18', '-pix_fmt', 'yuv420p',
    '-movflags', '+faststart', $temporaryOutput
)

try {
    Write-Host "Encoding $($frames.Count) frames at $FrameRate fps..."
    & $FfmpegPath @ffmpegArguments
    if ($LASTEXITCODE -ne 0) { throw "FFmpeg failed with exit code $LASTEXITCODE." }
    if (-not (Test-Path -LiteralPath $temporaryOutput -PathType Leaf) -or (Get-Item -LiteralPath $temporaryOutput).Length -eq 0) {
        throw 'FFmpeg did not produce a nonempty MP4.'
    }

    $probeLines = & $ffprobePath '-v' 'error' '-show_format' '-show_streams' '-of' 'json' $temporaryOutput
    if ($LASTEXITCODE -ne 0) { throw "FFprobe failed with exit code $LASTEXITCODE." }
    $probe = ($probeLines -join "`n") | ConvertFrom-Json
    $videoStreams = @($probe.streams | Where-Object codec_type -eq 'video')
    if ($videoStreams.Count -ne 1) { throw 'Expected exactly one video stream.' }
    $video = $videoStreams[0]
    if ($video.codec_name -ne 'h264' -or $video.pix_fmt -ne 'yuv420p') { throw 'Encoded video is not H.264 with yuv420p pixels.' }
    if ([int]$video.nb_frames -ne $frames.Count) { throw "Encoded frame count $($video.nb_frames) does not match source count $($frames.Count)." }
    if ($video.avg_frame_rate -ne "$FrameRate/1") { throw "Unexpected encoded frame rate: $($video.avg_frame_rate)" }

    # Reject a recording that changed while FFmpeg was reading it.
    $currentFrames = @(Get-ChildItem -LiteralPath $FramesPath -File -Filter 'Frame_*.png' | Sort-Object Name)
    if ($currentFrames.Count -ne $frames.Count) { throw 'Frame count changed while encoding. Wait for recording to finish and run again.' }
    for ($index = 0; $index -lt $frames.Count; $index++) {
        if ($currentFrames[$index].Name -cne $frames[$index].Name -or $currentFrames[$index].Length -ne $frames[$index].Length -or $currentFrames[$index].LastWriteTimeUtc -ne $frames[$index].LastWriteTimeUtc) {
            throw 'Source frames changed while encoding. Wait for recording to finish and run again.'
        }
    }

    Move-Item -LiteralPath $temporaryOutput -Destination $OutputPath -Force
    $probe.format.filename = $OutputPath
    $metadata = [ordered]@{
        createdUtc = [DateTime]::UtcNow.ToString('o')
        sourceFrames = $FramesPath
        sourceFrameCount = $frames.Count
        presentationFramesPerSecond = $FrameRate
        expectedDurationSeconds = $frames.Count / [double]$FrameRate
        outputPath = $OutputPath
        ffmpegPath = $FfmpegPath
        ffprobePath = $ffprobePath
        ffprobe = $probe
    }
    $metadata | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $metadataPath -Encoding UTF8
    Write-Host "Video: $OutputPath"
    Write-Host "Metadata: $metadataPath"
    [pscustomobject]@{ Video = $OutputPath; Metadata = $metadataPath; Frames = $frames.Count; Width = $video.width; Height = $video.height; DurationSeconds = $probe.format.duration }
}
finally {
    if (Test-Path -LiteralPath $temporaryOutput -PathType Leaf) { Remove-Item -LiteralPath $temporaryOutput -Force }
}
