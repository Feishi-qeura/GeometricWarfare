param(
    [string]$EngineRoot = 'F:\UNREAL\UE_5.8\UE_5.8',
    [string]$ArchiveDirectory = '',
    [switch]$DebugGM
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
if (-not $ArchiveDirectory) { $ArchiveDirectory = Join-Path $projectRoot ('output/douyin-live-release-' + (Get-Date -Format 'yyyyMMdd-HHmmss')) }
$ArchiveDirectory = [IO.Path]::GetFullPath($ArchiveDirectory)
if (Test-Path -LiteralPath (Join-Path $ArchiveDirectory 'Windows')) { throw 'Choose a fresh archive directory to preserve existing packages.' }
& (Join-Path $projectRoot 'Tools/DouyinLauncher/Build.ps1')
$sdkHost = Join-Path $projectRoot 'Plugins/DouyinLiveProvider/Binaries/Win64/SdkHost/DouyinSdkHost.exe'
if (-not (Test-Path -LiteralPath $sdkHost)) {throw 'Build the official SDK host first.'}
$gameConfigPath=Join-Path $projectRoot 'Config/DefaultGame.ini'
$originalGameConfig=[IO.File]::ReadAllBytes($gameConfigPath)
try {
    if($DebugGM) {
        $gameConfigText=[IO.File]::ReadAllText($gameConfigPath)
        if($gameConfigText -notmatch 'EnableGM=False') {throw 'Debug package requires an explicit disabled GM source default.'}
        [IO.File]::WriteAllText($gameConfigPath,$gameConfigText.Replace('EnableGM=False','EnableGM=True'),[Text.UTF8Encoding]::new($false))
    }
    & (Join-Path $EngineRoot 'Engine/Build/BatchFiles/RunUAT.bat') BuildCookRun "-project=$projectRoot/GeometricWarfare.uproject" -noP4 -platform=Win64 -clientconfig=Shipping -build -cook -stage -pak -compressed -nodebuginfo -archive "-archivedirectory=$ArchiveDirectory" -map=/Engine/Maps/Entry -utf8output -unattended -nocompileeditor
    if ($LASTEXITCODE -ne 0) {throw "Shipping packaging failed: $LASTEXITCODE"}
} finally {
    if($DebugGM){[IO.File]::WriteAllBytes($gameConfigPath,$originalGameConfig)}
}
$packageRoot = Join-Path $ArchiveDirectory 'Windows'
$innerExe = Join-Path $packageRoot 'GeometricWarfare/Binaries/Win64/GeometricWarfare-Win64-Shipping.exe'
$sdkHostInPackage = Join-Path $packageRoot 'GeometricWarfare/Plugins/DouyinLiveProvider/Binaries/Win64/SdkHost/DouyinSdkHost.exe'
foreach($required in @($innerExe,$sdkHostInPackage)) {if(-not (Test-Path -LiteralPath $required)){throw "Incomplete release: $required"}}
$launcher = Join-Path $projectRoot 'tmp/DouyinLauncherBuild/DouyinLauncher.exe'
Copy-Item -LiteralPath $launcher -Destination (Join-Path $packageRoot 'GeometricWarfare.exe') -Force
$symbols=Get-ChildItem -LiteralPath $packageRoot -Recurse -File -Filter '*.pdb'
if($symbols){throw 'Debug symbols unexpectedly staged in release.'}
@'
几何战争 Shipping 发布候选包
直播伴侣选择根目录 GeometricWarfare.exe，保留完整目录。
右上角“设置”按钮或 Esc：垂直同步、帧率上限、动效档位、姓名/能量条和飘字数量，自动保存。
设置支持自动/PC/移动端布局，以及跟随窗口、16:9、4:3、9:16、20:17、19:9。
竖屏重新排布战场和榜单，保持角色、圆形与头像等比显示。移动端指开播画面布局。
右上角“主播助战”：红方、蓝方、灰方；复用同一五角星角色，自动跟随，不占观众名额，不参与个人排名与评奖。
助战仅在平台已连接并接受本局开始后可用，结算期间暂停切换。
默认 60 FPS、关闭垂直同步、自动动效。1000 人起精简装饰动效，不改变模拟或技能/积分。
Spout 仍输出固定 1920×1080。没有 GM 面板，Shipping 不允许本地模拟观众。
在固定横屏输出中选择竖屏时，两侧留边；在伴侣中按实际开播画面裁切/等比填充，避免强制拉伸。
SDK 宿主和凭证管道完整保留。无伴侣凭证时等待真实平台启动。
画面常驻五种礼物图标、效果说明及“一键摇人”指引：小摇杆 → 召集 → 发起召集。
仙女棒：阵亡时首件复活；存活时每件基础生命 +30，本次生命有效、死亡清零；批量剩余件继续强化。
平台审核测试礼物正常展示效果；测试权益仅在当前会话有效，测试数据不提交正式战绩和排行榜。
服务端配置和真实直播联调仍需完成；此包不代表平台验收已通过。
'@ | Set-Content -LiteralPath (Join-Path $packageRoot '使用说明.txt') -Encoding UTF8
if($DebugGM) {
    @'

本次为未上线 GM 联调包（EnableGM=True）：玩法窗口取得焦点后按 G 打开，Esc 或按钮关闭。
可按数量加入红 / 蓝 / 灰人机。无凭证可调试人机，真实观众仍需平台连接并发送“加入”。
礼物目标输入真实 SDK OpenID，或先输入公开抖音号、选择对应已入场真实观众、点击绑定。
没有自动抖音号查询；不要用昵称匹配账号。绑定仅在当前会话有效。
五种礼物使用同一玩法处理函数；可先令目标阵亡/摧毁基地，测试仙女棒/能力药丸。
GM 礼物为本次运行模拟，不扣费、不生成官方履约、不保存永久武器权益。
人机不提交平台用户分组/个人结算。当前调试对局不要当作正式比赛验收。
主播五角星使用当前直播间主播头像，加载失败时暂用占位头像。
本地 Spout 帧已修复透明度；BGM、主播助力音乐及音效均为原增益2倍。
这份联调说明取代上文“没有 GM 面板”的正式包说明；上线前使用不带 DebugGM 的打包命令。
'@ | Add-Content -LiteralPath (Join-Path $packageRoot '使用说明.txt') -Encoding UTF8
}
$files=Get-ChildItem -LiteralPath $packageRoot -Recurse -File
$size=($files|Measure-Object Length -Sum).Sum
@{package=$packageRoot;bytes=$size;MiB=[math]::Round($size/1MB,2);files=$files.Count;configuration='Shipping'} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $ArchiveDirectory 'package-size.json') -Encoding UTF8
Write-Output "PACKAGE_READY=$packageRoot"
Write-Output "PACKAGE_MIB=$([math]::Round($size/1MB,2))"
$zipPath = $ArchiveDirectory + '.zip'
if (Test-Path -LiteralPath $zipPath) {throw "Archive exists: $zipPath"}
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($packageRoot,$zipPath,[IO.Compression.CompressionLevel]::Optimal,$false)
Write-Output "PACKAGE_ZIP=$zipPath"
Write-Output "PACKAGE_ZIP_MIB=$([math]::Round((Get-Item -LiteralPath $zipPath).Length/1MB,2))"
