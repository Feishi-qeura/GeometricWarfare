# 几何战争战斗扩展验证记录

> 历史验证记录：本文对应当时的战斗/演示版本，保留原始证据与操作过程。2026-10-04 后续平台中立迁移已移除游戏内 GM 与模拟输入面板，插件改为 LiveInteraction；本文旧界面、旧插件路径与当时通过结果不代表当前迁移或真实官方 SDK 验收。

日期：2026-10-02。工程：`D:\demo\GeometricWarfare`。本轮八项战斗与观察改动，以及用户更正的固定 BOSS、单向激光、名字上方跟随箭头、胜方 MVP/败方 FMVP 均已实现。最终 Editor/Game 无警告构建成功，UE 八项自动化与原生回归通过；新箭头和奖项的实际录制帧已查看。最终版本的完整 4057 帧 MP4 已重新录制、编码并通过 ffprobe，实施计划全部勾选完成。5000 人独立压力采样覆盖 BOSS 更正后、最后 UI/奖项调整前版本。

## 八项实现与确定规则

| 项目 | 实现与边界 |
| --- | --- |
| 1. 跟随镜头保持锁定 | 右键解除跟随。锁定时滚轮仍可缩放，焦点身份不变；拖地图、小地图、WASD、Home 和全图按钮不将镜头移离焦点，点击另一角色或榜单行可切换焦点。自由视角继续采用鼠标锚点缩放和左键拖动。最新更正将旧金色横线改为名字上方居中、留有间距的金色向下小箭头。 |
| 2. 战斗反馈 | 普通枪械绘制短动态弹道、枪口闪光、后坐力、命中火花；角色、NPC 和 BOSS 受击闪烁。实际生命伤害仍使用固定 128 槽飘字池，上移并淡出。 |
| 3. 分享与霰弹枪 | 已入场观众分享获得霰弹枪，每枪随机 3～8 颗，每颗基础伤害 8、2 发弹匣、2 秒间隔、5 秒基础换弹。本局死亡保留，下一局恢复手枪；重复分享不补弹、不重置冷却。主播保持步枪。 |
| 4. 点赞回血 | 每个有效点赞增量恢复本人当前最大生命的 5%，上限为最大生命，仅已加入且活着的身份生效；不入场、不复活、不恢复护甲。点赞/分享有规范化桥接事件和事件 ID 去重；累计点赞总数不当作增量。 |
| 5. 护甲 | 先计算形状减伤，随后伤害一半扣生命，另一半以 35% 向下取整消耗护甲；100 原始伤害在无形状减伤时扣 50 HP、17 护甲。护甲不足时按未吸收比例补扣生命，不产生负护甲。 |
| 6. 进化 | 第 60/120/180/240 秒各生成五个位置不同的闪电方形包；存活观众可拾取，包含灰色，排除主播。当前与最大生命翻倍、获得 100 护甲、持续 40 秒；重复拾取只刷新时长，死亡立即终止，到期保留生命比例并还原普通上限、移除进化护甲。每 10 秒释放六道均匀方向剑气，基础伤害 60、射程 220、每目标每道仅一次；40 秒到期先于第四次释放。 |
| 7. 主播助战 | 用户确认主播自动移动和战斗。操作者用“主播助红 / 主播助蓝 / 主播中立”按钮调用 `HostAssist`，观众评论不能取得主播身份。额外独立席位不占 5000 观众名额，五角星直径为普通圆 2.5 倍、圆形碰撞包围体、速度 50%、HP 1000、护甲 500；步枪伤害 5、30 发、0.2 秒间隔、3 秒换弹、射程 900。无观众形状被动、不拾分、不上榜、不获 MVP/FMVP；被主播击败的观众持分掉落，复活遵循所属阵营规则并恢复初始护甲。 |
| 8. 中央 BOSS | 每局精确第 30 秒仅出现一次，中心梯形、直径 220、3000 HP，全阶段世界位置固定在出生中心，跳跃仅改变显示高度。三种攻击完成后随机选择下一种；HP≤600 狂暴。末击红/蓝方取得 60 秒新产出积分和伤害各 +20%，尚存基地当前 HP 永久翻倍并按需扩容；已毁基地不复活。灰色末击无队伍奖励，主播末击可奖励本队但无个人分。 |

### BOSS 具体计时和默认值

| 机制 | 普通 | 狂暴 |
| --- | --- | --- |
| 固定火地 | 原中心半径 300，减速 20%，每满 1 秒连续暴露受 1 基础环境伤害 | 仍为每秒 1 点 |
| 炮弹 | 等待 5 秒发射，直径 35.2、速度 1000、射程 4000、伤害 100，首个命中后消失 | 等待 2.5 秒、伤害 200 |
| 激光 | 跟踪蓄力 5 秒后锁方向；预警及激光从 BOSS 中心只沿前方单一方向到地图首个边界，宽 52.8；激活后第 1/2/3 秒各扣 50 基础伤害，背后单位不受激光伤害 | 蓄力 3 秒，每次伤害 100，同样只向前方 |
| 震地 | 原地跳跃 1 秒，落回出生中心，冲击波在 1 秒内扩张到半径 450，每单位每波伤害 45 并击退，之后休息 2 秒 | 每波伤害 90，无休息，世界位置仍固定 |

跳跃和波扩张各 1 秒是对未指定动画时长的演示默认值。按用户更正，BOSS 不向目标移动，跳跃与落点、火地及冲击波中心均固定为原出生中心。离开火地清空未满一秒的暴露累积。震地包含观众、主播及中立 NPC，NPC 位移同步空间桶，环境击杀不发个人奖励。BOSS 自身和基地不受 BOSS 技能伤害；炮弹和激光攻击存活观众及主播。激光除单向端点外，还排除中心位于后方半平面的单位，避免紧贴 BOSS 后方的单位被起点圆头碰撞误伤。死亡后关闭火地并清除残留炮弹。

队伍积分加成仅作用于自然球/NPC 等新产出，小数部分累积；玩家持分转移、掉落积分回收保持守恒。用户已确认基地奖励是当前生命翻倍、必要时提高最大生命，永久保留到本局结束。灰色末击不发队伍 buff 是本地演示的明确默认。

### 最新 MVP / FMVP 更正

MVP 只从胜方队内选择积分最高的观众，FMVP 只从败方队内选择积分最高的观众。队内积分相同先比较击败数，再按较小稳定玩家 ID；主播和灰色均排除，空阵营不产生该队奖项。主流程确认保持原胜负裁决：阵营总分相同先比阵营击败数，再相同则按局号轮换红蓝优先，正常流程仍有胜方，不引入新平局规则；仅在 `winnerTeam` 不是红/蓝的防御分支中不发两奖。新奖项规则取代全场积分第一和旧贡献公式；原生回归先失败再通过，最终构建、UE 集成与实际结算画面均已通过检查。

## 原生与集成测试状态

下表记录各项最新证据：Match/Expansion 已重跑新奖项逻辑，Editor/Game 和 UE 自动化均为最终箭头/奖项版本。

| 验证项 | 当前结果 | 证据与范围 |
| --- | --- | --- |
| BOSS 原生 | 用户更正后 444 断言通过，0 失败，MSVC `/W4` 无警告 | [Test-Boss.ps1](D:/demo/GeometricWarfare/Scripts/Test-Boss.ps1)、[ArenaBossTests.cpp](D:/demo/GeometricWarfare/Tests/ArenaBossTests.cpp)：新增普通/狂暴每阶段位置固定、原地起落、八方向警告端点、前方命中及远/近背后不受激光伤害；保留精确 30 秒/单次出生、所有计时/狂暴、护甲/死亡、NPC 跨格索敌、奖励/守恒和整轮复位。更正前新增测试先观察到 5 组失败，修复后全绿。 |
| 核心扩展原生 | 最新奖项更正后重跑 5376 断言通过 | [Test-Expansion.ps1](D:/demo/GeometricWarfare/Scripts/Test-Expansion.ps1)、[ArenaExpansionTests.cpp](D:/demo/GeometricWarfare/Tests/ArenaExpansionTests.cpp)；保留剑气出生时不提前推进等回归。 |
| Match 原生 | 最新奖项更正后 30,174 断言通过 | [Test-Match.ps1](D:/demo/GeometricWarfare/Scripts/Test-Match.ps1)，核心实现分任务确认奖项测试先失败再通过，包含胜/败方范围、排序与排除规则，并保持既有容量、守恒和阶段边界回归。 |
| Physics 原生 | 0 失败 | [Test-Physics.ps1](D:/demo/GeometricWarfare/Scripts/Test-Physics.ps1)，主流程用本轮代码重跑。 |
| ArenaView 原生 | 25 断言通过 | [Test-View.ps1](D:/demo/GeometricWarfare/Scripts/Test-View.ps1)，包含跟随时缩放保留焦点和自由镜头锚点规则。 |
| LikeDelta | 11 断言通过 | [Test-InteractionRules.ps1](D:/demo/GeometricWarfare/Plugins/DouyinLiveBridge/Tests/Test-InteractionRules.ps1)，主流程重跑增量解析专项。 |
| Unreal 自动化 | 最终 UI/奖项版 8 成功，0 警告，0 失败，0 未运行 | [CombatExpansionAutomation/index.json](D:/demo/GeometricWarfare/Saved/CombatExpansionAutomation/index.json)：HUDInput、SocialCombat、ViewerLifecycle、AvatarOrdering、Decode、Deduplicate、InteractionDecode、InteractionDeduplicate。最新报告标记 `2026.10.02-15.33.05`，香港时间 23:33:05。 |
| Editor / Game | 最终 UI/奖项版两目标均 Succeeded、无警告 | 主流程统一重新构建；工程构建通过不代表已完成烘焙打包发布。 |

SocialCombat 接线前的 [CombatSocialRed/index.json](D:/demo/GeometricWarfare/Saved/CombatSocialRed/index.json) 记录 0 成功、1 失败、6 条预期缺失接线断言；补齐实现后，同一测试在最终八项报告中通过。这个先失败再通过的证据确认测试能够捕获社交事件没有到达 GameMode 的问题。

HUD 测试直接调用实际 `BeginPointer`、`UpdatePointer`、`EndPointer` 和 `ZoomAtCursor` 等方法；SocialCombat 通过桥接模拟事件进入实际 GameMode。它们未注入操作系统鼠标事件，也未连接真实平台 SDK。此文档任务没有另外启动构建，以上最终构建及重跑结果来自主流程。

## 渲染与规模验证状态

以下真实 UE 画面均已由主流程查看。技能画面来自 BOSS 更正后的展示预置，5000 人概览来自当时独立压力运行；这些图早于最后 UI/奖项调整。`FollowArrow.png`、`TeamAwards.png` 则复制自最终版本完整录像的新帧，专门验证最新两项更正；此前同路径原始帧的旧人物与分数不再引用。

| 画面 | 分辨率 | 验收与证据 |
| --- | --- | --- |
| 单向激光与完整 HUD | 1920×1080 | [Combat-laser.png](D:/demo/GeometricWarfare/Saved/Screenshots/Combat-laser.png)：BOSS 固定中心，激光仅朝前方发射。 |
| 狂暴与紧凑 HUD | 1280×720 | [Combat-rage.png](D:/demo/GeometricWarfare/Saved/Screenshots/Combat-rage.png)：狂暴状态、固定中心及紧凑布局正常。 |
| 原地震地冲击波 | 1920×1080 | [Combat-wave.png](D:/demo/GeometricWarfare/Saved/Screenshots/Combat-wave.png)：冲击波、BOSS 和火地同心，固定位置正确，主播受击飘字可见。 |
| 进化与剑气 | 1920×1080 | [Combat-evolution.png](D:/demo/GeometricWarfare/Saved/Screenshots/Combat-evolution.png)：早期展示帧可见六向剑气、闪电进化包、主播生命/护甲、枪械与 NPC 命中反馈。 |
| 5000 观众加主播概览 | 1920×1080 | [Combat-5000.png](D:/demo/GeometricWarfare/Saved/Screenshots/Combat-5000.png)：红/蓝/灰 2000/2000/1000 加独立主播席位正确，单向 BOSS 预警可见。 |
| 最终跟随箭头 | 1920×1080 | [FollowArrow.png](D:/demo/GeometricWarfare/Saved/Screenshots/FollowArrow.png)：锁定“麦芽3”，积分 548；金色向下小箭头居中于名字上方并留有间距，旧横线已移除。 |
| 最终队伍奖项 | 1920×1080 | [TeamAwards.png](D:/demo/GeometricWarfare/Saved/Screenshots/TeamAwards.png)：红方 4760、蓝方 12365，蓝胜；MVP 为蓝方“小鹿4”（4036 分、8 击败），FMVP 为红方“云朵2”（1975 分、10 击败），胜/败方文案正确。 |

这仅代表列出的画面与窗口尺寸经过检查；不能扩大为所有状态或窗口尺寸均已视觉验收。炮弹的发射时间、射程和命中已由原生测试覆盖；本次另拍的炮弹静帧中出膛位置被 BOSS 遮挡，因此没有将它列作清晰飞行炮弹的展示证据。

新增显示与仿真上限：普通弹道 512 条、剑气 256 条、BOSS 炮弹 16 槽、伤害事件 512 条、飘字 128 槽；BOSS 按空间网格检索附近玩家，激光/冲击波按真实几何进一步筛选，逐目标记录每波命中。主播为第 5001 个独立席位。已有局部碰撞/索敌预算继续适用，极端拥挤可能短暂穿插。

### 更正后 5000 观众加主播的独立采样

本次已在没有其他 UE、构建或 FFmpeg 进程并发的情况下独立运行 `GWStressTest` 与 `GWCombatStress`，成功退出。证据：[CombatStress5000.json](D:/demo/GeometricWarfare/Saved/CombatStress5000.json)，记录生成于香港时间 22:42:31；[运行日志](D:/demo/GeometricWarfare/Saved/CombatStress5000.log)确认 Intel Core i5-13600KF、AMD Radeon RX 7900 XTX、约 32 GB 内存、Windows 11，1920×1080 离屏概览。

采样版本已包含固定中心 BOSS 与单向激光，但早于最后的跟随箭头、MVP/FMVP 调整。最后两项修改后没有重复压力采样；此处数字保留对应版本及工况范围，不宣称测得最终 UI/奖项版本的专门耗时。

工况从对局第 60 秒的预置状态开始，包含 500 把霰弹枪、5 名进化观众和一名主播；预热 5 秒后采样 20 秒，共 3318 个样本。所有 3318 个样本中 BOSS 均活跃；普通弹道最多达到 512 条、剑气最多 12 条。

**5000 是包含阵亡者的观众身份数，另有 1 名主播。采样帧中的同时存活单位为 3443～4494，不代表持续 5000 个存活单位同时更新。**

| JSON 指标 | 平均 | P95 |
| --- | ---: | ---: |
| frame | 6.030 ms | 7.253 ms |
| simulation | 1.844 ms | 3.112 ms |
| hud | 1.791 ms | 2.263 ms |

这是本机指定短时离屏概览工况的结果，未换算为开播帧率承诺。真实可见窗口、放大视角、长时间运行、极端拥挤、头像/事件网络流量及直播编码上传均可能改变表现。此前移动/双向 BOSS 版本的采样，以及上一轮 `Saved/ObserverUIStress.json`，均不计作本轮最终结果。

## 录像与外部边界

**最终箭头及奖项版本的 [完整 1× MP4](D:/demo/GeometricWarfare/Saved/Videos/GeometricWarfare-CombatExpansion-1x.mp4) 已重新录制、编码并通过 ffprobe，编码脚本退出码为 0。** 它按正常 1× 时间推进，覆盖完整 300 秒对局、30 秒结算及约 8 秒下一局。以下仅列本次新录制已查看的实际帧；此前同路径的旧分数、人名和蓝方 BOSS 奖励描述已移除。

| 实际录制帧 | 观察到的状态 |
| --- | --- |
| [Frame_00900.png](D:/demo/GeometricWarfare/Saved/DemoFrames/Frame_00900.png) | 锁定“麦芽3”，积分 548；名字上方的金色向下箭头居中且没有旧横线。 |
| [Frame_03660.png](D:/demo/GeometricWarfare/Saved/DemoFrames/Frame_03660.png) | 蓝胜，红方 4760、蓝方 12365；MVP 为蓝方“小鹿4”（4036 分、8 击败），FMVP 为红方“云朵2”（1975 分、10 击败）。 |
| [Frame_04056.png](D:/demo/GeometricWarfare/Saved/DemoFrames/Frame_04056.png) | 第二局剩余 04:52，红/蓝重新计分 612/390，双方基地已恢复。 |

[最终元数据](D:/demo/GeometricWarfare/Saved/Videos/GeometricWarfare-CombatExpansion-1x.metadata.json)创建于 `2026-10-02T15:42:06.5563459Z`（香港时间 23:42:06）。以下属性已从本批次元数据及实际文件核对：

| 视频属性 | 最终结果 |
| --- | --- |
| 原始 / 编码帧数 | 4057 / 4057 |
| 编码与像素格式 | H.264 / yuv420p |
| 分辨率与帧率 | 1920×1080，12/1 fps |
| ffprobe 容器时长 | 338.083008 秒 |
| 按帧数计算的时长 | 4057÷12 = 338.0833333333333 秒；与容器时长仅有微小舍入差异 |
| 文件大小 | 32,753,544 字节 |

12 fps 是视频采样率，游戏按 1× 时间运行，不是实际开播帧率。最终 MP4 已覆盖此前含旧横线/旧奖项的暂存战斗扩展视频；此处证据只对应最终录制。另保留 [GeometricWarfare-Demo-1x.mp4](D:/demo/GeometricWarfare/Saved/Videos/GeometricWarfare-Demo-1x.mp4) 作为上一轮观察界面证据；其历史元数据为 4057 帧、338.083333 秒、1920×1080、H.264、yuv420p、12 fps，不计作本轮新视频核验。

本轮未修改本地提案 DOCX、待同步片段，也未访问或编辑飞书、上传或替换附件。它们仍保留上一轮观察界面内容。此前 DOCX 结构和内容校验通过，Word/LibreOffice 缺失导致分页未完成视觉核验，这个历史限制仍然存在。

真实抖音 SDK 尚未接入。点赞、分享、主播按钮和礼物仍是本地模拟或自定义适配服务边界；不能据此宣称线上点赞/分享投递、直播间权限或真实礼物履约已完成端到端联调。
