# 包体与性能设置验证（2026-10-05）

## 发布候选包

- 目录：`output/douyin-live-release-20261005-ready/Windows`。
- ZIP：`output/douyin-live-release-20261005-ready.zip`，115.83 MiB。
- 解压目录：245.64 MiB，205 文件；原 Development 目录最终复测 1122.59 MiB（包含历次运行文件），按目录对比减少约 78.1%。证据 `Saved/PerformanceSettings-FinalSize.json`。**解压目录没有达到缩小 90% 的目标**，ZIP 大小不能当作目录减幅。
- ZIP SHA-256：`9A1F965B3603842806238C635691C0BC551C9EB1A0F2444650F5E007E94C27FC`。
- 启动直播伴侣时选择根目录 `GeometricWarfare.exe`，它保留令牌匿名管道保护，并选择内层 Shipping exe。原联调包和各阶段候选包保留。

优化包括 Shipping 编译、禁止分发 PDB、压缩 cooked 内容、关闭默认启用的无用引擎插件、仅编译 DX12 SM5、关闭 Lumen/光追/路径追踪/Nanite/虚拟纹理/距离场/体积等 3D 功能，使用 Forward Renderer。Canvas 图形和 DX12 Spout 不依赖这些功能。

原包主程序 317.59 MiB、PDB 355.31 MiB；新主程序 96.68 MiB，没有 PDB、ONNX/DirectML/Vulkan 调试层。官方 LiveOpenSDK Unity 宿主整体仍为 103.97 MiB，运行库完整保留，未对 SDK DLL 进行未经验证的删除。**UE 主程序与 SDK 宿主合计 200.65 MiB**，当前运行架构下，仅删除美术资源无法把完整目录压至约 112 MiB。

## 设置

右上角“设置”按钮或 Esc 打开/关闭，点击各行轮换选项，即时生效并保存到 UE 的 `GameUserSettings.ini`。

| 选项 | 可用值 | 默认 |
|---|---|---|
| 垂直同步 | 开/关 | 关 |
| 帧率上限 | 30/60/90/120/不限 | 60 |
| 战场动效 | 自动/完整/精简 | 自动 |
| 战场姓名、血条数量 | 0/40/80/180 | 180 |
| 飘字数量 | 0/32/64/128 | 128 |

自动档以已加入玩法的人数判断，999 人以下完整，1000 人起精简；完整和精简支持手动覆盖。精简档把普通姓名/血条限制为最多 40，飘字最多 32，小地图每 0.5 秒刷新，光环保持静态颜色/轮廓，取消装饰光晕、战场震动及部分火焰粒子。手动数量可进一步下调。关注玩家/主播/英雄的姓名血条与关注/主播飘字优先，允许超过普通装饰预算。所有角色实体、武器、技能判定、危险预警、BOSS、礼物提示和履约消息继续保留；不修改模拟数据或积分。

设置面板不暂停对局；打开时阻止战场点击、滚轮、WASD、Home 和右键操作。关闭后恢复相机输入。设置只应用非分辨率参数，不改 Spout 固定 1920×1080 约定。垂直同步开启时帧率同时受屏幕刷新率限制；不限并不保证设备达到某个帧率。

## 验证证据

- `Saved/PerformanceSettings-Editor-FinalBuild.log`：Editor 构建成功。
- `Saved/PerformanceSettings-Shipping-Ready.log`：Shipping 构建、cook、stage、archive 和 ZIP 成功。
- `Saved/PerformanceSettings-FinalAutomation/index.json`：25 成功、0 失败、0 警告、0 未运行。新增设置测试覆盖真实 HUD 按钮/行点击、拖动取消、输入遮挡、所有帧率档位、保存重载、非法配置恢复和 999/1000/5000 人预算；同时运行全部原有玩法与平台层回归。
- 凭证启动器 11 个用例通过，含 Shipping 和 Development 子进程路径、令牌去日志/参数、UTF-8 管道及引号/边界检查。
- `Saved/Screenshots/PerformanceSettings-UI-Final.png`：1920×1080 下设置面板中文、按钮字形与布局实测，进程正常退出。
- 精简 SM5 Shipping Spout 验证：初版 `Saved/PerformanceSettings-Shipping-Spout.log` 1906 新帧；最终可交付包 `Saved/PerformanceSettings-Ready-Spout.log` 1903 新帧、尺寸错误 0，DXGI 格式 24；`Saved/Screenshots/PerformanceSettings-Ready-Spout.bmp` 包含中文 HUD、设置入口和无凭证 0 人等待状态。验证后主动停止测试进程；没有声称 Shipping 正常关机测试通过。
- 根启动器与编译结果 SHA 一致；打包 SDK 的 `Assembly-CSharp.dll` 与已验证源宿主一致。

此处证明本地运行与 GPU 共享输出，真实抖音房间/凭证/礼物/榜单服务联调仍待完成。Shipping 没有 GM 面板，不接受本地模拟观众模式。该包是发布候选，未上传或上线。

## 复现

```powershell
./Scripts/Package-DouyinRelease.ps1
./Scripts/Test-PresentationPerformance.ps1 -Mode Full
./Scripts/Test-PresentationPerformance.ps1 -Mode Auto
```

性能脚本仅运行显式本地测试，串行比较 5000 人加主播、BOSS、500 把霰弹枪和五名进化玩家的总览场景；禁用 Spout 和帧率上限以避免同步等待掩盖 HUD 耗时。不能把这个场景的表现当作所有设备和真实直播伴侣的帧率保证。

RX 7900 XTX / i5-13600KF，1920×1080，Editor offscreen DX12 SM5 单次测量：

| 指标（毫秒） | 完整档 | 自动精简 |
|---|---:|---:|
| HUD 平均 | 13.560 | 6.094 |
| HUD P95 | 24.908 | 11.426 |
| 整帧平均 | 20.104 | 10.187 |
| 整帧 P95 | 37.561 | 17.036 |

证据：`Saved/PerformanceSettings-Stress-Full.json`（996 样本）和 `Saved/PerformanceSettings-Stress-Auto.json`（1964 样本）。两次为动态对局，存活人数范围分别 1864–4774 / 2039–4691，不是逐帧完全相同的重放。此次 HUD 均值降低约 55.1%，仅说明这一测试配置的实际收益；模拟每帧耗时也受帧时长、分步数和对局变化影响，不能解读为改动了模拟规则。
