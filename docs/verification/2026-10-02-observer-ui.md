# 几何战争观察界面验证记录

> 历史验证记录：本文对应当时的战斗/演示版本，保留原始证据与操作过程。2026-10-04 后续平台中立迁移已移除游戏内 GM 与模拟输入面板，插件改为 LiveInteraction；本文旧界面、旧插件路径与当时通过结果不代表当前迁移或真实官方 SDK 验收。

验证日期：2026-10-02。工程：`D:\demo\GeometricWarfare`。本轮围绕用户提出的七项观察界面改进完成实现、Editor/Game 构建、原生测试、真实 HUD 方法集成测试、不同分辨率画面检查和 5000 人离屏概览采样。新版正常速度录像已完成编码和元数据核验；飞书在线文档未修改。

## 七项实现与确认规则

| 用户需求 | 本轮实现 |
| --- | --- |
| 1. 滚轮跟随鼠标位置缩放 | 以鼠标指向的世界位置为缩放锚点，范围为 1～12 倍；缩放解除玩家跟随。地图边缘的相机边界约束优先于锚点保持。竞技场外和小地图上不触发主视图缩放。 |
| 2. 点击排行榜锁定玩家视角 | 轻点排行榜行即锁定并定位该玩家。按下时保存稳定玩家 ID 和当时的行范围，榜单在按下、抬起之间重新排序不会选中另一人；拖动不作为点击。 |
| 3. 阵营人数和容量 | 界面显示红蓝各自人数及上限 2000、灰色人数及上限 1000。人数包含阵亡者，死亡不释放名额，复活不重复计数。满员明确拒绝，不自动把用户转入其他阵营。 |
| 4. 受击数字上移并渐隐 | 头顶飘字显示实际扣除生命值，包含减伤和剩余生命限制；按受伤方阵营着色，上移并渐隐。同一目标短时间命中聚合，显示对象使用固定容量池复用。 |
| 5. 小地图与拖动地图 | 小地图显示阵营分布、基地、焦点玩家和当前视野框，支持点击及拖动定位。主视图左键拖动平移；从按下点位移达到 5 像素即认定为拖动，抬手不再选人。拖动解除跟随，失焦或离开视口取消手势。WASD、Home 等原有操作保留。 |
| 6. 加宽血条并居中显示数值 | 详细视图的血条加宽，在血条中央显示当前生命 / 最大生命；不同窗口高度采用相应布局，焦点玩家卡继续显示生命状态。 |
| 7. 使用简明图像并补充个人积分 | 形状介绍使用矢量几何图标，枪械采用独立枪管表现；生命、弹药、击败和积分分别使用心形、子弹、骷髅和星形图标，保留必要短标签。焦点玩家卡加入当前积分，并显示弹匣、换弹或复活状态。 |

**灰色满员后的入队规则已由用户确认：新观众可直接发送 `1` 或 `2`，加入尚未满员的红方或蓝方。** 不要求先占用一个灰色名额；目标阵营已满时拒绝该请求并保留原有身份状态。红蓝各 2000、灰色 1000，总容量仍为 5000。

实现对照：[HUD 绘制](D:/demo/GeometricWarfare/Source/GeometricWarfare/ArenaHUD.cpp)、[HUD 交互](D:/demo/GeometricWarfare/Source/GeometricWarfare/ArenaHUDInput.cpp)、[镜头与拖动数学](D:/demo/GeometricWarfare/Source/GeometricWarfare/Simulation/ArenaView.h)、[战斗规则](D:/demo/GeometricWarfare/Source/GeometricWarfare/Simulation/ArenaMatch.h)、[伤害显示池](D:/demo/GeometricWarfare/Source/GeometricWarfare/Simulation/DamageNumbers.h)。

## 构建和测试结果

主流程已完成 UE 5.8 Windows Development 的 Editor 和 Game 构建，两者均成功。这是工程构建验证，不代表已完成烘焙打包发布。

| 验证项 | 结果 | 主要覆盖 |
| --- | --- | --- |
| 原生 Match | 30,165 项断言通过，0 失败组 | 红蓝灰容量、满员拒绝和直接入队、身份与计数一致性、死亡与复活、实际伤害值、既有对局边界及 5000 人规模；[测试脚本](D:/demo/GeometricWarfare/Scripts/Test-Match.ps1) |
| 原生 DamageNumbers | 11 项断言通过 | 上移与渐隐、短时间同目标聚合、不同目标区分、失效与重置、无效输入，以及 5000 次伤害下固定容量复用；[测试脚本](D:/demo/GeometricWarfare/Scripts/Test-DamageNumbers.ps1) |
| 原生 ArenaView | 23 项断言通过 | 鼠标锚点、缩放上下限、地图边界、平移方向、小地图映射、5 像素拖动阈值、取消及抬手处理；[测试脚本](D:/demo/GeometricWarfare/Scripts/Test-View.ps1) |
| Unreal 自动化 | 5 通过，0 警告，0 失败，0 未运行 | `HUDInput`、`ViewerLifecycle`、`AvatarOrdering`、`Decode`、`Deduplicate`；[JSON 报告](D:/demo/GeometricWarfare/Saved/ObserverUIAutomation/index.json)，报告标记 `2026.10.02-12.32.57` |

[HUDInput 集成测试](D:/demo/GeometricWarfare/Source/GeometricWarfare/ArenaHUDInputTests.cpp) 创建实际 GameInstance 世界、GameMode 和 HUD，通过本地评论加入玩家，再直接调用真实 `BeginPointer`、`UpdatePointer`、`EndPointer`、`CancelPointer` 和 `ZoomAtCursor`。验证包括榜单重排仍选原 ID、拖回原点不误选、同一帧按下与抬起时的最终位移、缩放锚点、解除跟随、小地图输入优先级、边缘约束，以及取消或离开视口后不选人。

**上述测试没有注入操作系统鼠标事件，不能记为真实鼠标输入链已经端到端自动化验收。** 主流程另核对了本机 UE 源码中的可见鼠标、捕获和离开视口行为；这提供实现依据，但不能替代实际 OS 输入验证。

头像缩略图与独立物理模块本轮未改，未重复运行其专项测试。它们的既有结果见[完整演示验证记录](D:/demo/GeometricWarfare/docs/verification/2026-10-02-full-demo.md)，不计作本轮新测结果。

## 真实渲染画面

以下均为本轮实际 UE 渲染截图，主流程已逐图检查。本记录另核对了图片文件的像素尺寸。

| 画面 | 分辨率 | 证据 |
| --- | --- | --- |
| 焦点玩家与完整观察界面 | 1920×1080 | [Observer-Focus.png](D:/demo/GeometricWarfare/Saved/Screenshots/Observer-Focus.png) |
| 较矮窗口的紧凑布局 | 1280×720 | [Observer-720.png](D:/demo/GeometricWarfare/Saved/Screenshots/Observer-720.png) |
| 小窗口结算布局 | 1024×768 | [Observer-Results.png](D:/demo/GeometricWarfare/Saved/Screenshots/Observer-Results.png) |
| 5000 人全图概览 | 1920×1080 | [Observer-5000.png](D:/demo/GeometricWarfare/Saved/Screenshots/Observer-5000.png) |
| 实际战斗伤害飘字 | 1920×1080 | [Observer-Damage.png](D:/demo/GeometricWarfare/Saved/Screenshots/Observer-Damage.png) |

检查范围包括焦点玩家数据、图标、血条、排行榜、小地图、短窗口播报和结算显示。这是指定画面与尺寸下的视觉检查结果，不扩大为所有窗口尺寸和交互路径均已验证。

## 5000 人 Unreal 概览采样

证据：[ObserverUIStress.json](D:/demo/GeometricWarfare/Saved/ObserverUIStress.json)。本机配置由本轮 Unreal 自动化报告记录：Intel Core i5-13600KF、AMD Radeon RX 7900 XTX、32 GB 内存、Windows 11、SM6。

压力测试在没有其他构建或录像并发时单独执行。工况为 1920×1080 离屏渲染的全图概览、本地自动战斗；预热 5 秒后采样 20 秒，共 4496 个样本。

**5000 是包含阵亡者的参与身份总数。采样期间同时存活人数为 3340～4498，不是持续 5000 个存活角色同时更新。**

| JSON 指标 | 平均 | P95 |
| --- | ---: | ---: |
| frame | 4.449 ms | 5.643 ms |
| simulation | 1.254 ms | 2.444 ms |
| hud | 1.521 ms | 2.097 ms |

这些是本机短时离屏概览数据，不构成直播 FPS 承诺。可见窗口、放大视图、极端拥挤、长时间运行、真实事件与头像网络流量，以及直播编码和上传可能改变表现。本轮未用独立物理耗时代替完整帧耗时，也未将离屏数据换算为开播帧率保证。

为了限制 5000 人的额外显示成本，伤害事件最多保留 512 条，飘字显示池固定 128 槽，同一目标 0.18 秒内聚合、1.05 秒后失效；小地图使用 64×64 网格，每 0.2 秒更新分布。既有碰撞邻居预算仍适用，极密场景可能暂时穿插，索敌采样也不保证遍历范围内每个目标。

## 录像和外部事项

**本轮新版 1× 录像已完成并核验。** [GeometricWarfare-Demo-1x.mp4](D:/demo/GeometricWarfare/Saved/Videos/GeometricWarfare-Demo-1x.mp4) 已更新为本轮界面，包含完整 300 秒对局、30 秒结算和下一局开场。本轮[视频元数据](D:/demo/GeometricWarfare/Saved/Videos/GeometricWarfare-Demo-1x.metadata.json)创建时间为 `2026-10-02T12:44:44.4102528Z`（香港时间 20:44:44）；ffprobe 确认 4057 帧、338.083333 秒、1920×1080、H.264、yuv420p、12 fps。12 fps 是录像采样率，游戏按正常 1× 时间运行，不代表实时渲染帧率。已检查实际伤害、玩家跟随与积分、胜利结算及下一局基地恢复画面。

Chrome 连接曾多次尝试，包含会话重置和直接 URL 连接，仍未恢复。因此用户提供的飞书在线文档（私有文档链接已省略）本轮没有修改，也没有上传或替换其附件。[本地提案 DOCX](D:/demo/GeometricWarfare/docs/proposal/【提案评估】几何战争_草稿.docx)及[飞书待同步文字](D:/demo/GeometricWarfare/Saved/ProposalQA/feishu-update-snippet.txt)已更新为本轮结果，保留开发者 FEISHI、社交玩法及原模板结构；ZIP/XML、内容和模板结构通过校验，记录见 [expansion-qa.json](D:/demo/GeometricWarfare/Saved/ProposalQA/expansion-qa.json)。本机缺少 Word/LibreOffice，DOCX 最终分页尚未完成视觉核验。

当前演示继续使用本地模拟评论和礼物效果。真实抖音 SDK、生产直播间权限、线上事件投递及真实礼物履约仍未完成端到端联调。
