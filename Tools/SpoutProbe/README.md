# Spout 接收验证工具

这个自写探针使用仓库 `Plugins/LiveSpoutOutput/Source/ThirdParty` 中的 Spout 头文件、`SpoutDX12.lib` 和 `SpoutDX12.dll`，不依赖 `tmp/` 中的预编译文件或抖音 SDK。第三方许可见该插件的 ThirdParty 目录及仓库 `THIRD_PARTY_NOTICES.md`。

需要 Windows x64、Visual Studio C++ 构建工具和 Windows SDK。可在仓库根目录的 x64 Developer PowerShell 运行：

```powershell
./Tools/SpoutProbe/Build.ps1
./Saved/SpoutProbe/receiver-package-probe.exe --list
./Saved/SpoutProbe/receiver-package-probe.exe --sender mate_spout_local --output ./Saved/SpoutProbe/capture.bmp
```

构建脚本也会尝试通过 `vswhere` 查找 MSVC。所有编译产物及所需 DLL 留在 `Saved/SpoutProbe/`；复制工具时保留相邻的 `SpoutDX12.dll`，并安装适用的 Microsoft Visual C++ x64 运行库。

`--sender` 默认 `mate_spout_local`，`--output` 默认当前目录的 `spout-received.bmp`。输出目录须已存在。`--list` 仅枚举发送器，不接收画面，也不启动 UE；`--help` 显示参数。

正式接收时，探针必须在 50 秒内收到指定发送器至少 **240 个新帧**，所有接收纹理尺寸均为 1920×1080，且采样 BMP 保存成功、像素 Alpha 全为 255，才返回 0。重复读取同一帧不会推进新帧计数。采样点为第 91、120、240 个新帧；同时输出原始、RGB 和黑底合成 BMP，Alpha 检查仅覆盖这些采样，不代表逐帧检查所有像素。失败返回 1，参数或设备初始化错误返回 2。

两个包验证脚本会先构建探针，检查指定名称是否已有活动发送器，再启动所选包：

```powershell
./Scripts/Test-ReviewPackageSpout.ps1 -PackageRoot ./output/GeomeWar_1.1.17 -Version 1.1.17 -SenderName mate_spout_local
./Scripts/Test-DualHandPackageSpout.ps1 -PackageRoot ./output/GeomeWar_1.1.17 -SenderName mate_spout_local
```

当前项目在 `Game.ini` 的 `DouyinLiveProvider.AppId` 为空或为占位值时使用 `mate_spout_local`，配置真实 AppId 后使用 `mate_spout_<AppId>`。验证旧包时应根据其实际配置显式传入 `-SenderName`，不要根据版本推测名称。脚本不会关闭预先存在的发送器。包脚本涉及真实渲染；单独构建及 `--list` 成功仅证明工具可构建、可加载和可枚举，不证明包画面、音频或平台验收通过。
