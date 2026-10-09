# 开源开发入门

本文面向 Windows 开发者，先运行本地模拟，再按需接入直播平台。作者版本通过审核，不代表克隆后的新应用自动取得同样的权限、账号配置或审核结果。正式开播必须使用开发者自己的应用、SDK、服务和平台授权。

## 1. 环境与仓库

| 用途 | 依赖 |
| --- | --- |
| 本地画面、UE 构建 | Windows x64、Unreal Engine **5.8**、Visual Studio C++ 工具链及 UE 5.8 要求的 Windows SDK |
| 纯 C++ 测试 | MSVC `cl.exe`，支持 C++20；不需要 UE 运行时 |
| 官方 SDK 宿主，可选 | Unity **2022.3** Windows Editor / Win64 构建支持；本集成基线为官方 **LiveOpenSDK 2.7.12**，自行从有权限的官方渠道取得 |
| 独立后端，可选 | Python **3.12**，项目提供依赖锁定文件 |
| 本机直播画面输出，可选 | 支持 D3D12 的设备、Spout 接收端或具备对应权限的直播伴侣 |

安装 Visual Studio 的“使用 C++ 的游戏开发”或相应桌面 C++ 组件，并按自己安装的 UE 5.8 要求选择工具链。以下命令在 **x64 Native Tools / Developer PowerShell** 中执行，先确认 `Get-Command cl.exe` 成功。普通 PowerShell 仅安装了编辑器但找不到 `cl.exe` 时，测试脚本无法自动找到编译器。

```powershell
git clone https://github.com/Feishi-qeura/GeometricWarfare.git
Set-Location GeometricWarfare
$ProjectRoot = (Get-Location).Path
$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8' # 改成自己的安装目录
Get-Command cl.exe
Test-Path "$EngineRoot\Engine\Build\BatchFiles\Build.bat"
```

`GeometricWarfare.uproject` 的引擎关联为 `5.8`。仓库不提供 UE 安装、生成的 UE 模块、个人缓存或官方 Unity SDK 宿主成品。

公开默认配置使用 `YOUR_APP_ID`、空后端地址及礼物 ID 占位符，世界榜默认关闭。先运行本地模式，无需填这些配置。不要把原作者的应用 ID、域名或礼物映射直接用于自己的应用。

## 2. 构建并运行本地模拟

```powershell
.\Scripts\Build-Project.ps1 -EngineRoot $EngineRoot
.\Scripts\Start-Demo.ps1 -EngineRoot $EngineRoot
```

第一条构建 `GeometricWarfareEditor Win64 Development`，失败时脚本会终止。第二条打开独立玩法窗口，显式传入 `-GWLocalTest`，默认加入 96 名模拟观众；它不连接真实直播间。地图使用引擎自带 `/Engine/Maps/Entry`，玩法由 C++ 创建，不需要另找关卡文件。

演示脚本带 `-NoSound`。需要试听时，关闭演示窗口后直接启动一个开发实例：

```powershell
& "$EngineRoot\Engine\Binaries\Win64\UnrealEditor.exe" `
  "$ProjectRoot\GeometricWarfare.uproject" /Engine/Maps/Entry `
  -game -GWLocalTest -NoSpout -windowed -ResX=1280 -ResY=720 -log
```

右上角“设置”或 Esc 可切换布局和性能选项；点击角色或榜单可跟随，右键解除，解除后拖动战场或使用小地图定位。矩形视口保持角色等比，短轴可裁剪视野；改变开播比例不会改变 2000×2000 世界物理尺寸。

正常启动不带 `-GWLocalTest` 时等待真实平台，不会自动回退模拟。Shipping 构建禁用本地模拟；不要用 Shipping 包验证这条开发命令。GM 默认关闭，不是本地入门的必要条件。

## 3. 测试与常用构建

纯 C++ 测试不必先构建 UE，例如：

```powershell
.\Scripts\Test-Physics.ps1
.\Scripts\Test-Match.ps1
.\Scripts\Test-View.ps1
.\Scripts\Test-RoundUpgrade.ps1
.\Scripts\Test-Weapons.ps1
.\Scripts\Test-Feed.ps1
.\Scripts\Test-Leaderboard.ps1
```

这些脚本用 MSVC C++20 编译并运行对应 `Tests/*.cpp`，产物在 `Saved/*Tests`；编译或断言失败会返回错误。其他规则测试见 `Scripts/Test-*.ps1`，其中名称包含 `Visuals`、`Performance`、`Package` 的脚本不是纯 C++ 单元测试，通常需要 UE 或现成包。请先读参数，不要把全部 `Test-*.ps1` 无差别批量执行。

Editor 构建后，可在 UE 的 Session Frontend → Automation 运行 `GeometricWarfare` 测试组。命令行方式如下，日志和报告输出到本地 `Saved`：

```powershell
& "$EngineRoot\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" `
  "$ProjectRoot\GeometricWarfare.uproject" /Engine/Maps/Entry `
  -dx12 -unattended -nosplash -NoSpout -RenderOffscreen `
  '-ExecCmds=Automation RunTests GeometricWarfare' `
  '-TestExit=Automation Test Queue Empty' `
  "-ReportExportPath=$ProjectRoot\Saved\OpenSourceAutomation" `
  "-abslog=$ProjectRoot\Saved\OpenSourceAutomation.log"
```

这些测试注册在 EditorContext，自动化命令不要加 `-game`。检查报告中的实际执行数量、失败项及进程退出状态，不能把零项测试当作通过。需要检查实际布局时，运行一个已有截图场景并打开 PNG：

```powershell
.\Scripts\Test-ReviewVisuals.ps1 -EngineRoot $EngineRoot -Only 'Review-PhoneAuto' -Prefix 'OpenSource'
```

预期输出是 `Saved/Screenshots/OpenSource-PhoneAuto.png`。截图脚本需要真实图形渲染，生成文件不等于文字和遮挡已经人工验收。`-NullRHI` 不能验证像素、Spout 或显示后履约。

`Build-Project.ps1 -EngineRoot $EngineRoot -Game` 只构建 Development 游戏目标，不会生成完整可分发包。正式发布脚本见第 6 节，它要求 SDK 宿主，不能作为无 SDK 的首次构建命令。

## 4. 可选：官方 SDK 与 Unity 伴随进程

UE 负责玩法与画面；`LiveInteraction` 定义中立事件，`DouyinLiveProvider` 通过匿名管道调用 Unity 宿主。没有 SDK 或平台权限时，继续使用本地模拟即可。缺少宿主不会阻止 UE 提供者模块编译，但正式启动会显示未就绪，正式打包脚本也会拒绝缺失宿主。

1. 使用自己的开发者账号，按照平台当前官方文档通过 BGDT 等官方流程取得 SDK。不要从本仓库或来源不明的网盘寻找受限 SDK 二进制。
2. 保留一个已由 Unity 正常解析依赖的获取工程。构建脚本需要其中的 `Packages/com.bytedance.liveopensdk/package.json`、`Packages/manifest.json`、`Packages/packages-lock.json`、`Library/PackageCache` 和 `ProjectSettings/ProjectVersion.txt`。
3. 使用与该工程一致的 Unity 2022.3 Editor 构建宿主；覆盖脚本内原开发机的默认路径：

```powershell
.\Tools\DouyinSdkHost\Build.ps1 `
  -UnityEditor 'C:\Program Files\Unity\Hub\Editor\YOUR_UNITY_VERSION\Editor\Unity.exe' `
  -AcquisitionProject 'C:\dev\YourOfficialSdkAcquisition'
```

脚本创建独立的 `tmp/DouyinSdkHostBuild`，运行协议契约和宿主调度检查，再输出到 `Plugins/DouyinLiveProvider/Binaries/Win64/SdkHost`。保留整个目录，不要只复制 `DouyinSdkHost.exe`。升级 SDK / Unity 后需重新核对公共接口并回归，不应把集成基线版本理解为平台永远要求该版本。

接入自己的应用时，再配置 `Config/DefaultGame.ini` 的 `[DouyinLiveProvider] AppId=YOUR_APP_ID`、自己的 HTTPS `BackendUrl`、`[LiveInteraction]` 阵营 ID 及 `[DouyinLiveProvider.Gifts]` 的官方加密 SecGiftId。不要从评论生成付费事件，也不要把本地模拟当成真实礼物履约。

启动令牌不写配置、环境示例、日志或 UE 命令行。正式平台必须选择包根目录凭证启动器，由它把令牌通过匿名 stdin 交给 UE / 宿主。不要手工给内层 UE exe 或编辑器拼接真实 `-token`。详细边界见 [Provider](../../Plugins/DouyinLiveProvider/README.md)、[SDK Host](../../Tools/DouyinSdkHost/README.md) 和 [Launcher](../../Tools/DouyinLauncher/README.md)。

## 5. 可选：后端与 Spout

独立后端处理认证、结算归档和世界榜上传，不替代 SDK 的评论 / 点赞 / 礼物推送。本地玩法无需启动它。需要开发后端时：

```powershell
Push-Location .\Services\LivePlatformBackend
py -3.12 -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements-dev.txt
.\.venv\Scripts\python.exe -m unittest discover -s tests -v
# 首次配置时复制；已有 .env 时不要覆盖。
Copy-Item .env.example .env
Pop-Location
```

测试使用假身份和模拟传输，不需要真实凭证。启动前编辑仅供自己使用的 `.env`：设置 `YOUR_APP_ID`、可写的本机绝对数据库路径；开发阶段保持上传关闭。AppSecret 只能在服务端配置，不能进入 UE、SDK 宿主源码、客户端包或 Git。

```powershell
.\Services\LivePlatformBackend\Start-Backend.ps1
```

服务默认绑定 `127.0.0.1:8765`，单进程运行；无 AppSecret 时不能进行真实认证。正式部署还需自己的 HTTPS 域名、反向代理、持久数据库和服务进程管理。配置示例可使用 `https://your-domain.example/live-api`，不应连接作者服务器。世界榜只有在自己的认证、上传权限和服务链路准备好后才启用。详见 [后端说明](../../Services/LivePlatformBackend/README.md)。

Spout 插件及其匹配的 Win64 头文件、导入库、DLL 与许可说明随仓库保留；不需要 Unity SDK 才能测试本机纹理输出。在普通本地演示命令中把 `-NoSpout` 改为 `-Spout -dx12`，再使用独立接收端检查画面。发送器读取自己的应用配置，未配置时使用 `mate_spout_local`，正式应用使用对应的 `mate_spout_{AppId}`。

本机 Spout 路径输出固定 1920×1080，云端启动另按平台宽高参数处理；Spout 接收成功不等于云端画面或直播伴侣权限通过。详细来源、哈希和许可见 [LiveSpoutOutput](../../Plugins/LiveSpoutOutput/README.md)、[UPSTREAM](../../Plugins/LiveSpoutOutput/UPSTREAM.md) 与 `Plugins/LiveSpoutOutput/NATIVE-SHA256.txt`。

## 6. 可选：制作正式包

先构建官方宿主，完成自己的应用和服务配置，再运行：

```powershell
.\Scripts\Package-DouyinRelease.ps1 -EngineRoot $EngineRoot `
  -ArchiveDirectory "$ProjectRoot\output\my-release-1.1.17"
```

使用不存在的输出目录，脚本会保护已有包。它构建凭证启动器、运行 UE Shipping 的 cook / stage / archive，并检查宿主是否完整随包。直播伴侣选 `Windows/GeometricWarfare.exe`，保留完整目录；不要选 `Binaries/Win64` 内层 exe。`-DebugGM` 是单独的联调选项，上线包不应携带该开关。

每个开发者需要重新验证真实鉴权、观众事件、礼物效果与履约、榜单、断线恢复、输出画面，以及自己应用的提审要求。仓库中的历史验证记录描述当时的作者环境，不能替代新机器、新应用或改动后的验收。

## 7. 目录、常见问题与许可

- `Source/GeometricWarfare/Simulation`：不依赖 UE Actor 的核心规则；`Source/GeometricWarfare/ArenaHUD*.cpp`：画面；`Tests`：纯 C++ 回归。
- `Plugins/LiveInteraction`、`Plugins/DouyinLiveProvider`、`Tools/DouyinSdkHost`：分别是中立接口、UE 提供者、官方 SDK 宿主。
- `Saved`、`Intermediate`、`Binaries`、`tmp`、`output`：本地产物 / 运行数据，不是克隆后的预置结果。
- `cl.exe` 找不到：使用 x64 Developer PowerShell 并安装 C++ 工具链。引擎路径错误：显式传 `-EngineRoot`，它应包含 `Engine/Build/BatchFiles/Build.bat`。
- 开窗但没有观众：确认使用 Development / Editor 且显式 `-GWLocalTest`；正式模式没有凭证时等待平台是预期行为。
- 提示 SDK host 缺失：本地模拟可继续；正式链路按第 4 节取得 SDK 并构建完整宿主。不要以删除检查或伪造成功状态解决。
- Spout 库链接失败：核对克隆完整性以及 `Source/ThirdParty` 下匹配的 include / lib / bin；不要混用其他版本的 DLL 和头文件。

项目原创代码和文档采用仓库根 [LICENSE](../../LICENSE) 中的 **MIT** 许可证；2026-10-09 经项目负责人同意由原 GPLv3 调整，不撤销此前版本已授出的许可。UE、Unity、官方 SDK、Spout 与平台礼物图标等第三方内容仍适用自己的来源和条款，不因根许可证调整而重新授权。具体范围见 [THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md)。保留第三方许可及版权说明，自行取得需要账号或授权的组件，不要把 `.env`、令牌、数据库、正式运行日志或获取工程上传到公共仓库。
