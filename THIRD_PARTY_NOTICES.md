# 第三方内容与授权范围

根目录 `LICENSE` 为本项目原创代码和文档的 MIT 许可证。2026-10-09 经项目负责人授权更新；不改变第三方文件自带条款，也不撤销此前版本已授出的许可。

| 内容 | 来源与范围 | 获取 / 许可说明 |
| --- | --- | --- |
| Unreal Engine 5.8 | 引擎、编辑器与构建工具 | 不随仓库分发；开发者自行安装并遵守 [Unreal EULA](https://www.unrealengine.com/eula/unreal)。根许可证不授权 Epic 技术。 |
| Unity Editor / Runtime | 官方 SDK 伴随进程的构建和运行环境 | 不随源码分发；开发者自行获取并按 Unity 条款使用。 |
| 抖音 LiveOpenSDK | 项目集成时使用 2.7.12；宿主通过其官方 Unity API 接入 | 官方包、获取工程、缓存和宿主二进制不随源码分发。按自己的平台账号与权限获取，见 [宿主说明](Tools/DouyinSdkHost/README.md)。 |
| Spout UE 集成 | GPUbrainStorm/UE5_Spout2_DX12，提交 `ac226a0b282ce07b7dbd25bc0d433ab7c45d353c` | 保留 [MIT 声明](Plugins/LiveSpoutOutput/Licenses/GPUbrainStorm-MIT.txt)；本项目包含改造后的发送端。 |
| Spout 原生 SDK | `Plugins/LiveSpoutOutput/Source/ThirdParty` 下头文件、Win64 DLL 与导入库 | 保留 [BSD 2-Clause 声明](Plugins/LiveSpoutOutput/Licenses/Spout-BSD-2-Clause.txt) 和头文件声明；见 [来源](Plugins/LiveSpoutOutput/UPSTREAM.md)、[文件哈希](Plugins/LiveSpoutOutput/NATIVE-SHA256.txt)。这套原生依赖是有意纳入的构建输入。 |
| 平台礼物图标 | `Content/GiftIcons` 下五种礼物的 PNG 与导入资产；由项目负责人提供用于平台礼物说明 | 不声明这些平台素材由本项目创作，不将其纳入项目 MIT 授权；复用者应自行确认目标平台和用途下的素材授权，或替换图标。 |
| 玩法音频 | `Assets/Audio/GeometricWarfare`、对应 UE 资产 | 由项目音频生成工具生成；相关脚本位于 `Tools/ArenaAudio`。 |

开源的是项目实现及接入边界，不包含平台私有 SDK、引擎源码、应用密钥、直播身份或正式服务使用权。安装 Python、Unity 等工具依赖时，它们仍适用各自许可证。
