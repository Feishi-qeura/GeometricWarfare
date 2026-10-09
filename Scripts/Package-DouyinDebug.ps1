param(
    [string]$EngineRoot = 'F:\UNREAL\UE_5.8\UE_5.8',
    [string]$ArchiveDirectory = ''
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
if (-not $ArchiveDirectory) { $ArchiveDirectory = Join-Path $projectRoot 'output/douyin-live-debug-20261004' }
$ArchiveDirectory = [IO.Path]::GetFullPath($ArchiveDirectory)
$sdkHost = Join-Path $projectRoot 'Plugins/DouyinLiveProvider/Binaries/Win64/SdkHost/DouyinSdkHost.exe'
$launcher = Join-Path $projectRoot 'tmp/DouyinLauncherBuild/DouyinLauncher.exe'
foreach ($required in @($sdkHost, $launcher)) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Build the SDK host and credential launcher first: $required" }
}
$uat = Join-Path $EngineRoot 'Engine/Build/BatchFiles/RunUAT.bat'
if (-not (Test-Path -LiteralPath $uat)) { throw 'UE 5.8 RunUAT.bat not found.' }
& $uat BuildCookRun "-project=$projectRoot/GeometricWarfare.uproject" -noP4 -platform=Win64 -clientconfig=Development -build -cook -stage -pak -archive "-archivedirectory=$ArchiveDirectory" -map=/Engine/Maps/Entry -utf8output -unattended -nocompileeditor
if ($LASTEXITCODE -ne 0) { throw "Unreal packaging failed: $LASTEXITCODE" }
$packageRoot = Join-Path $ArchiveDirectory 'Windows'
$rootExe = Join-Path $packageRoot 'GeometricWarfare.exe'
$innerExe = Join-Path $packageRoot 'GeometricWarfare/Binaries/Win64/GeometricWarfare.exe'
$hostInPackage = Join-Path $packageRoot 'GeometricWarfare/Plugins/DouyinLiveProvider/Binaries/Win64/SdkHost/DouyinSdkHost.exe'
foreach ($required in @($rootExe, $innerExe, $hostInPackage)) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Incomplete package: $required" }
}
$backupDirectory = Join-Path $projectRoot 'Saved/PackagingBackups'
New-Item -ItemType Directory -Path $backupDirectory -Force | Out-Null
Copy-Item -LiteralPath $rootExe -Destination (Join-Path $backupDirectory ("GeometricWarfare-bootstrap-{0}.exe" -f (Get-Date -Format 'yyyyMMdd-HHmmss')))
Copy-Item -LiteralPath $launcher -Destination $rootExe -Force
if ((Get-FileHash -LiteralPath $launcher).Hash -ne (Get-FileHash -LiteralPath $rootExe).Hash) { throw 'Credential launcher copy verification failed.' }
@'
几何战争：抖音本地联调包（Development）

直播伴侣请选择本目录 GeometricWarfare.exe。它是凭证启动器；不要选择内层 Binaries/Win64 的 UE exe。
无伴侣启动凭证时会显示“等待直播伴侣启动”，保持空场。没有 GM 面板。
请保留完整目录，不要单独复制 exe。根启动器将令牌经匿名管道传给 UE 和 LiveOpenSDK 宿主。

正式指令：加入；1 红方 / 2 蓝方；入队后 y 圆 / z 方 / c 长 / s 三角；武器1..6。
参与玩法并关注主播可解锁步枪；本地步枪兑换码和分享解锁不用于正式模式。
已接五种配置礼物、进出直播间、关注、快捷选队、对局阵营/战绩/房间榜和履约提交。
一键同玩不实现；互动扩展组件暂不启用；透明背景未启用。

此包供调试，不表示真实直播鉴权、平台履约、排行榜、伴侣接收或云启动已验收。
结算快照和成功步骤已持久保存，可在同应用、同房间恢复；真实断线/崩溃仍需平台联调。
周总榜服务端已实现，按首次成功接收入库的周次累计，重复补交不重复计分。服务尚未部署，BackendUrl 留空会保持结算待提交。
服务端 AppSecret 只放服务器；不要写进游戏配置。跨崩溃付费礼物持久去重尚未实现。
详细接入和验收记录见项目 docs/integration/2026-10-04-live-debug-guide.md。
'@ | Set-Content -LiteralPath (Join-Path $packageRoot '联调说明.txt') -Encoding UTF8
Write-Output "PACKAGE_READY=$packageRoot"
