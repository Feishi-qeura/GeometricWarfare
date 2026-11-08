# 几何战争 · 独立版

使用 Unreal Engine 5.8 开发的 Win64 独立游戏。安装 UE 5.8 和 Windows C++ 工具链后，打开 `GeometricWarfare.uproject`，构建 `GeometricWarfareEditor` 的 Win64 Development 目标，然后在编辑器里点击 **Play**。

`FUOnlineSession` 随项目提供，负责 LAN、Steam 和 Epic Online Services 的会话接入。Steam 配置里的 `480` 是开发测试 App ID。EOS 需要填写自己的项目配置。相关设置见 [插件文档](Plugins/FUOnlineSession/README.md)。

## 在编辑器里预览和开发

- `Content/Sketch` 包含可直接打开的网格、材质、贴图和 Niagara 特效。`Content/Audio/GeometricWarfare` 包含音频资产。
- 项目使用引擎自带的 `/Engine/Maps/Entry`。点击 **Play** 后，`GWController` 生成 `GWWorld` 来绘制场景和单位，`GWHUD` 通过 C++ 绘制界面。主游戏目前没有对应的可编辑关卡、角色蓝图或 Widget Blueprint。
- 在内容浏览器设置中开启 **显示 C++ 类**。选择 **添加 → 蓝图类 → 所有类**，可以用 `GWWorld` 创建蓝图子类。要让运行中的游戏使用该子类，需要把 `GWController::BeginPlay()` 里的生成类改为可配置的类引用，并暴露要从蓝图调整的属性或事件。

## 仓库范围

独立版分支排除了根目录的 `douyinkaifa/`、`Tests/` 和 `Scripts/`。游戏运行所需的 UE 源码、插件、配置和资产随仓库提供。旧抖音直播版可从 [1.1.17 提交](https://github.com/Feishi-qeura/GeometricWarfare/tree/65e45afe812a25eae678c9afd348bb98c823d658) 查看。

项目授权和第三方内容见 [LICENSE](LICENSE) 与 [第三方声明](THIRD_PARTY_NOTICES.md)。
