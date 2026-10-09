# 几何战争 · FEISHI

基于 **Unreal Engine 5.8 / Windows** 的抖音直播互动玩法源码：评论加入与选队、五种礼物效果、500 位观众、独立主播助战、七分钟对局、积分榜，以及横竖屏 Canvas HUD。

**2026-10-09，项目负责人确认已通过平台审核并上线，实际使用没有问题。** 本仓库公开 1.1.17 阶段的实现、修复过程和开发工具。上线状态来自项目负责人反馈；本案例不是平台官方模板，也不保证其他项目沿用后必定过审。

- [从提审失败到成功上线：经验总结](docs/case-studies/2026-10-09-douyin-live-launch.md)
- [克隆、构建、本地演示与平台接入](docs/development/OPEN_SOURCE_SETUP.md)
- [1.1.17 修复与验证记录](docs/verification/2026-10-08-gift-rules-1.1.17.md)
- [公开源码整理记录](docs/verification/2026-10-09-open-source-publication.md)

## 先运行本地演示

安装 UE 5.8、Visual Studio C++ 工具链及 Windows SDK，在仓库根目录的 PowerShell 中运行。替换引擎路径；测试脚本需要能够找到 `cl.exe`。

```powershell
$env:UE_ENGINE_ROOT = 'C:\Unreal\UE_5.8'
.\Scripts\Build-Project.ps1
.\Scripts\Start-Demo.ps1
```

演示显式使用 `-GWLocalTest`，加入模拟观众；它不需要平台启动凭证或 Unity SDK 宿主。普通启动保持空场，等待平台连接。演示脚本默认带 `-NoSound`；需要试听时按入门文档的直接启动命令运行。

公开配置没有正式服务地址，AppId 和五种 SecGiftId 都是占位值，跨局总榜默认关闭。接入自己的应用时配置 `Config/DefaultGame.ini`，部署自己的后端并单独获取官方 SDK。**AppSecret 只放服务端 `.env`；不放客户端、命令行或 Git。**

## 可以复用什么

| 目录 | 用途 |
| --- | --- |
| [Source/GeometricWarfare/Simulation](Source/GeometricWarfare/Simulation) | 独立 C++ 模拟、角色与礼物生命周期、矩形相机数学 |
| [Source/GeometricWarfare](Source/GeometricWarfare) | UE 玩法、Canvas HUD、通知队列、积分条、存档与集成测试 |
| [Plugins/LiveInteraction](Plugins/LiveInteraction) | 平台中立事件、会话校验、身份、去重与测试适配 |
| [Plugins/DouyinLiveProvider](Plugins/DouyinLiveProvider) | 抖音事件转换与伴随进程通信 |
| [Tools/DouyinSdkHost](Tools/DouyinSdkHost) | C# SDK 宿主源码与构建工具；不含官方 SDK |
| [Tools/DouyinLauncher](Tools/DouyinLauncher) | 根目录启动器，通过匿名管道转交启动凭证 |
| [Plugins/LiveSpoutOutput](Plugins/LiveSpoutOutput) | Windows D3D12 的固定 1920×1080 画面输出 |
| [Tools/SpoutProbe](Tools/SpoutProbe) | 可从源码构建的 Spout 接收与帧检查工具 |
| [Services/LivePlatformBackend](Services/LivePlatformBackend) | 平台后端、结算接收、幂等与榜单上传队列 |
| [Tests](Tests) / [Scripts](Scripts) | 模拟回归、诊断、构建和打包脚本 |

平台审核测试礼物可以触发当次玩法效果，但不会污染正式永久权益和战绩；重复事件不重复应用效果。五种礼物的图标、效果、限制及召集指引常驻画面。仙女棒存活时每件增加 30 基础最大生命，本次生命有效、死亡清零。具体边界和源码入口见案例总结。

界面通过独立的视口宽高适配手机、平板和 PC，世界投影保持等比；短轴采用裁剪、拖动和小地图浏览。顶部两队积分和个人排行榜使用同组统一标尺的进度条，完整分数居中；没有新增固定获胜分数。

## 验证与交付边界

历史 1.1.17 记录包含模拟回归、38 项 UE 回归、多比例实图、Shipping 构建、205 文件 ZIP 校验及包内 Spout 240 新帧检查。详见[原始验证记录](docs/verification/2026-10-08-gift-rules-1.1.17.md)，其中“平台待验”是 10 月 8 日当时的状态；10 月 9 日的上线反馈单独记入案例，不倒改旧证据。

公开仓库不包含本机日志、数据库、存档、官方 SDK 获取工程或提审 ZIP。历史文档中引用的 `Saved/`、`tmp/`、`output/` 路径属于原开发机产物，不能当作克隆后已有文件。公开源码改用自己的应用配置后，需要重新完成平台联调与提审。

完整玩法细节与旧开发记录见[历史开发说明](docs/development/PROJECT_NOTES.md)。该文件保留阶段性描述；当前启动与配置以本页及开源入门为准。

## 许可证与第三方内容

项目原创代码和文档采用 [MIT](LICENSE)，2026-10-09 经项目负责人同意从原 GPLv3 调整。本次提交不撤销此前版本已经授出的许可。

Unreal Engine、Unity、抖音官方 SDK 和平台礼物图标不属于项目原创代码的 MIT 授权。Spout 保留其上游 MIT/BSD 声明；完整范围、来源与依赖获取方式见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
