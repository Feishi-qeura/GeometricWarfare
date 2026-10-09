# 抖音 LiveOpenSDK 官方获取与接口核验

核验日期：2026-10-04，2026-10-05 更新宿主后台通道。范围：官方公开文档、官方 BGDT 下载包、本机 Unity 安装位置、已取得的 LiveOpenSDK 2.7.12，以及主任务登录控制台后提供的现场核验结果。完整包已取得；尚未确认 UE 5.8 可用的直接原生 ABI，尚未完成真实直播间联调。观众一键同玩不在本次范围内。

## 控制台能力现场核验

证据来源：主任务在已登录的应用控制台核验后传回，区别于下文公开 web 文档。已开通、已开启均不代表客户端已经实现或联调成功。本次没有修改这些控制台开关。

| 能力 | 控制台状态 | 当前实施边界 |
| --- | --- | --- |
| 评论、点赞、礼物 | 已开通且开启 | SDK 已取得，待真实提供者联调 |
| 关注互动 | 已开通且开启 | 2.7.12 合约已核验，待真实消息联调 |
| 粉丝团 | 未开通 | 不启动粉丝团推送任务 |
| 快捷选队 | 已开通且开启 | 公共 SDK / UE 接线已实现，待真实选队、阵营与履约联调 |
| 用户战绩与排行榜 | 已开通且开启 | 对局结算和榜单上报已接线，待真实返回成功证据 |
| 玩法扩展组件 | 已开通，当前关闭 | 当前不启用；不推定已有扩展组件实现 |
| 同局主播连线 | 未开通 | 当前不启用 |
| 观众一键同玩 | 用户明确排除 | 不接入、不申请 |
| Spout（控制台基础能力项） | 已开通且开启 | SDK/包体接入契约待核验 |
| 进入/离开直播间 | 已开通且开启 | 2.7.12 载荷已核验；主任务官方正文确认 1 进 / 2 离，待真实事件联调 |
| 玩法数据履约上报 | 已开通且开启 | 待 SDK 的接收/完成履约联调 |
| 玩法云启动 | 已开通且开启 | 平台开通不代表已通过云端包体测试 |
| 透明画布 | 已开通，当前关闭 | 当前不启用 |
| 云同步 | 未开通 | 当前不启用 |

开发配置页现场状态：AppSecret 未启用；调试成员为空；互动协议及路径等配置项均显示“暂无数据”。这与能力开关是独立条件，仍需按取得的 SDK 接入方案完成相关配置；此记录不包含任何凭证。

## 官方获取路径与本机条件

- [Unity SDK 接入](https://developer.open-douyin.com/docs/resource/zh-CN/interaction/develop/unity-sdk/unity-sdk-access)：Unity 2020 及以上 LTS、C# 8.0+、Mono/IL2CPP；先导入 BGDT，再从 `ByteGame > ByteGame Develop Tools` 安装 `LiveOpenSDK`。渠道应为 `cp`；企业/权限决定可下载工具；显示 `Apply` 的内部测试版本需要申请权限，批准后 `Reload`。
- 完整包应位于 Unity 项目的 `Packages/com.bytedance.liveopensdk`，应包含 `Samples`、`Runtime` 和 `ByteDance.LiveOpenSdk.Api` 公共 API 程序集。官方不保证 `Runtime` 引擎集成层的兼容性，不能把其内部实现当成稳定 UE 接口。
- [BGDT 安装页](https://developer.open-douyin.com/docs/resource/zh-CN/mini-game/develop/guide/game-engine/rd-to-SCgame/BGDT-handbook/install)公开版本：`3.0.271`。[官方安装包](https://lf3-stark-cdn.bdgp.cc/obj/ide-updateserver-bytegame/bgdt/com.bytedance.bgdt/3.0.271/com.bytedance.bgdt-cp-3.0.271.unitypackage)。
- 已从官方 CDN 下载到 `tmp/douyin-sdk-research/bgdt-3.0.271.official.unitypackage`：279959 字节，SHA256 `21D568AC3C6B1306F21F060FB036FD76913E6839312819D3C794637131F021B7`，与用户 Downloads 内同名版本一致。
- 对官方安装归档的首次检查仅解包及检查元数据，当时没有执行二进制。包内实际资产仅 `package.json`、`Editor/bgdt.dll`、`Editor/bgdt.data`；两文件是有 CLR COM Descriptor 的 x64 托管程序集，后者标识为 `bgdt.core.dll`。此包是编辑器下载工具，未包含 LiveOpenSDK runtime、C/C++ 头文件或导入库；随后由主任务导入 Unity 执行官方获取流程。
- 本机已装 Unity：注册表 `HKLM\SOFTWARE\Unity Technologies\Installer\Unity 2022.3.62f1c1` 的 `Location x64` 为 `D:\unityhub\2022.3.62f1c1`；已验证 `D:\unityhub\2022.3.62f1c1\Editor\Unity.exe` 存在，版本 `2022.3.62f1c1`。建议用独立 Unity 项目获取官方包，再核验依赖与授权范围；不能由 Unity 可安装推导 UE 可直接加载。
- 未找到官方公开文档支持脱离 Unity 的 headless LiveOpenSDK 包获取方式。没有使用第三方镜像、私有网络 API 或登录令牌逆向获取。
- 主任务首次启动 Unity 批处理时遇到 `No valid license`。后续已完成 Unity 激活，官方 BGDT 已导入独立项目 `tmp/DouyinSdkAcquisition`，用户经 Unity 界面 `cp` 渠道安装后已取得完整 `Packages/com.bytedance.liveopensdk`，`package.json` 版本为 `2.7.12`。许可证与获取阻塞已解除。

## 实际 2.7.12 包与宿主边界

以包内源码、XML 公共 API 文档及只读元数据反射核验，没有反编译私有登录协议。公开类型导出保存为 `tmp/douyin-sdk-research/public-api-2.7.12.txt`。

- `ByteDance.LiveOpenSdk.Api.dll` 与 `Impl.dll` 是 netstandard 托管程序集；Api 引用 `ByteCloudGameSdk`，Impl 还引用 `dyCloudUnitySDK`、Refit、Newtonsoft.Json 和消息库。`ByteCloudGameSdk` 引用 UnityEngine.CoreModule/UIModule，`dyCloudUnitySDK` 引用 UnityEngine，因此核心是托管 DLL 不等于整个 SDK 独立于 Unity。
- `LiveOpenSdkImpl` 元数据为 internal、`IsPublic=false`、`IsVisible=false`；仅对官方 Runtime、CliDemo、Test 等程序集开放内部可见性。不能把 XML 中的实现类说明当成公开实例化入口，不能据此宣称任意 .NET 程序可合法直接构造。
- 官方公共入口是 `Runtime/LiveOpenSdk.cs` 的 `ByteDance.LiveOpenSdk.Runtime.LiveOpenSdk.Instance`。该 Unity 集成层还绑定 CloudSync、PerfMonitor 和 DebugUtils，依赖真实 Unity 运行时。实际 Unity SDK 伴随宿主与 UE 的匿名 stdin/stdout JSONL 已实现且构建通过，UE 保留渲染与玩法；本地构建/协议结果不代表真实直播间已接通。
- 包中另有云游戏、云同步、安全与性能相关原生 DLL/Wrapper.lib；未发现可据公开头文件直接映射为全部 LiveOpenSDK 功能的 C++ 导出契约。
- 2.7.12 包内说明：新增关注关系变化消息，进房消息新增分享来源和关注状态；2.7.9 新增进出房消息。网页更新日志停留在更早版本，实际接口以本次包核验为准。

## UE / C++ 支持的证据边界

[玩法云启动能力](https://developer.open-douyin.com/docs/resource/zh-CN/interaction/jierushuoming/pingtaijichu/cloud)明确提到 Unity/UE 玩法，因此 UE 玩法可作为平台运行包体；这不等于存在已确认的 C++ LiveOpenSDK ABI。

[互动工具开发者指南](https://developer.open-douyin.com/docs/resource/zh-CN/live-interactive-tools/development/tutorial/live-tool-guide)提供 Windows/C++17 的 `PipeSDK.dll`、`CreatePipeClient`、`IPipeClient`，但属于另一产品“直播互动工具”，用于直播伴侣管道、素材和 `OPEN_LIVE_DATA`。其 `--pipeName`、`--maxChannels` 启动参数不能替代直播玩法的启动 token；文档没有证明其覆盖 LiveOpenSDK 的对局、阵营、排行榜及履约接口。

## 扩展组件、透明背景与云环境参数

[玩法扩展组件](https://developer.open-douyin.com/docs/resource/zh-CN/interaction/jierushuoming/advanced-interaction/gameplay-expansion)要求关联同企业的独立抖音小游戏。组件通过 `tt.getLaunchOptionsSync` / `tt.onShow` 的 query 获取 `roomid/openid`，通过 `tt.login` 服务端登录返回的 `binding_danmu_openid` 关联弹幕身份。这不是已经确认的单个 LiveOpenSDK 调用；本次不开启或实现该组件。

[透明背景指南](https://developer.open-douyin.com/docs/resource/zh-CN/interaction/jierushuoming/pingtaijichu/transparent-bg-guide)要求游戏窗口自身透明并在伴侣允许透明窗口；提供 Unity Win32 layered-window 示例，其他引擎需自行实现和验证 exe。文档要求伴侣 10.5+，不适用于移动端主播启动。本次控制台透明开关关闭，没有据 SDK 中云视频接口推断 UE 已支持透明输出。

2.7.12 公开 Runtime `SdkEnv` 按 key/value 分离的 argv 解析 `-cloud-game`、`-mobile`、`-screen-width`、`-screen-height`、`-screen-fullscreen` 的整数并缓存；云/mobile 判断值是否为 1。UE 提供者向宿主转发前四个经过校验的非凭证参数，宿主固定 `-screen-fullscreen 0`，结合 batchmode/nographics 保留 UE 主窗口。传入云参数仅保证 SDK 环境识别条件，仍需按[玩法云启动能力](https://developer.open-douyin.com/docs/resource/zh-CN/interaction/jierushuoming/pingtaijichu/cloud)验证云视频、包体与平台启动。

实际公开 `ILiveStreamApi` 有 `InitLiveStreamAsync(ILiveStreamMsgCallbacks)`、`SendVideoFrame(long)` 和 `SendCustomMessageAsync(CustomMessageState)` 等云视频接口；未验证该 long 的 GPU 句柄互操作约束，不能把它当成已可用的 UE 图像 ABI、扩展组件或透明背景开关。

## Spout 上屏官方说明

证据来源：主任务已读取控制台关联的官方 Spout 飞书说明（私有文档链接已省略），以下记录正文要求，尚未通过直播伴侣联调。

- UE 参考实现为 [kessoning/Spout-UE5](https://github.com/kessoning/Spout-UE5) 与 [GPUbrainStorm/UE5_Spout2_DX12](https://github.com/GPUbrainStorm/UE5_Spout2_DX12)。两仓库是官方文档推荐的参考实现，不能据此描述为官方 LiveOpenSDK 或已验证适配 UE 5.8；本次未下载/编译这两个仓库。
- 固定目标 RenderTarget 为 `1920×1080`，采用 GPU 图像共享；可从 FinalColor 或 BackBuffer 取得最终画面。当前游戏的 Canvas HUD 必须同时上屏，不能仅用 SceneCapture 捕获场景而漏掉 HUD。具体 UE 5.8 渲染调用与图像同步仍需实现后验证。
- 正文有 `mate_spout_appid` 命名示例及作者 9 月 3 日更正；UE 章节仍残留 `ProductID_ProcessID` 的不同命名方式。该矛盾待直播伴侣实际接收要求核验，不把任一种命名宣称为已经联调通过。
- 实际技术联调需要技术白名单。Spout 已开通不代表白名单已获批或伴侣已收到图像。

## 可实施的公开 API 契约

以下是官方 Unity/C# 公共 API 的语义契约；C++ 函数名、调用约定、内存所有权、线程与事件载荷仍必须用实际授权包核验。

### 启动与直播间

[玩法客户端 exe](https://developer.open-douyin.com/docs/resource/zh-CN/interaction/jierushuoming/pingtaijichu/zipmanage/)规定启动参数 `-token=xxx`，为单横线。应安全解析，日志不得输出令牌。

UE 自身会在游戏提供者启动前记录 argv，故已实现[原生启动器](../../Tools/DouyinLauncher/README.md)作为打包根入口：剥离 token 和有凭证时的本地测试开关，token 经匿名 stdin 进入 suspended UE，写完并关闭 writer 后恢复；UE 再以宿主 init 帧传递。子 UE 和 SDK 宿主 argv 均无 token。官方原始参数仍存在于原生入口的 OS 进程元数据，启动器本身不落盘、不打印。MSVC 构建和十项假子进程回归已通过，实际发布包与平台启动尚待验证。

[SDK](https://developer.open-douyin.com/docs/resource/zh-CN/interaction/develop/unity-sdk/live-unity-sdk-support/sdk)：`LiveOpenSdk.Instance` 实现 `ILiveOpenSdk`；初始化前配置 `Env` 和回调同步上下文；文档示例 `Initialize(appId)`，自动从命令行获取 `Env.Token`；退出 `Uninitialize()`。[更新日志](https://developer.open-douyin.com/docs/resource/zh-CN/interaction/develop/unity-sdk/update-log)同时写明 2.6.0 初始化接口增加参数，实际签名以取得版本为准。

[直播间数据](https://developer.open-douyin.com/docs/resource/zh-CN/interaction/develop/unity-sdk/live-unity-sdk-support/live-data)：等待 `GetRoomInfoService().WaitForRoomInfoAsync()`，内部重试；强制刷新 `UpdateRoomInfoAsync()`。返回 `IRoomInfo.RoomId`、`Anchor.OpenId`、`Anchor.AvatarUrl`、`Anchor.Nickname`。

[服务端直播信息](https://developer.open-douyin.com/docs/resource/zh-CN/interaction/develop/server/live-room-scope/live-info)区分启动 token 和应用 access_token；启动 token 有效期 30 分钟。服务端调用使用 POST `/api/webcastmate/info`、body `token`、header `x-token` 为应用 access_token。不得混用两个凭证或把服务端 AppSecret 放进 UE 客户端。

### 指令直推与履约

[指令直推](https://developer.open-douyin.com/docs/resource/zh-CN/interaction/develop/unity-sdk/live-unity-sdk-support/direct-push-ability)：直播间就绪后订阅 `OnMessage`、`OnConnectionStateChanged`，分别调用 `StartPushTaskAsync(msgType,pushType)`。类型 `live_comment`、`live_like`、`live_gift`、`live_fansclub`；公共消息有 `MsgId`、`MsgType`、`Timestamp`（毫秒）。`SinglePush=1` 为 SDK 单推；2.7.4+ 的 `HTTPWithSDK=2`、`DyCloudWithSDK=3` 是可选双推，不能要求自建 HTTP 中转作为必经步骤。

停止每种消息任务 `StopPushTaskAsync(msgType)`；停止后仍可能回调，需同时取消订阅与控制玩法接收状态。游戏退出反初始化。

[履约上报](https://developer.open-douyin.com/docs/resource/zh-CN/interaction/develop/unity-sdk/live-unity-sdk-support/ack-ability)：SDK 自动报告接收，玩法在完成处理/渲染后调用 `GetMessageAckService().ReportAck(msgId,msgType)` 或 `ReportAck(message)`。UE 游戏线程实际应用事件后才回传完成，不得在仅排入队列时宣称履约。无公开证据支持 share 推送类型。

### 对局、阵营和快捷选队

[礼物进阶互动](https://developer.open-douyin.com/docs/resource/zh-CN/interaction/develop/unity-sdk/live-unity-sdk-support/gift-interaction-upgrade)：`UpdateRoundStatusInfoAsync` 参数为递增 `RoundId`（同 RoomId）、秒级 `StartTime/EndTime`、`Status`（1 开始 / 2 结束），结束必须有 `GroupResultList`，其中 `GroupId` 与后台一致、`Result`（1 胜 / 2 负 / 3 平）。加入或变更阵营时 `UpdateUserGroupInfoAsync` 上报 `RoundId/OpenId/GroupId`。

返回 `IRoundDataRes.ErrCode=0` 成功；`40001` 参数错误、`4014034` 频控、`40004` token 过期。需保留失败、重试与成功状态，不能仅以本地 HTTP/进程连接判定官方连接。

[快捷选队](https://developer.open-douyin.com/docs/resource/zh-CN/interaction/develop/unity-sdk/live-unity-sdk-support/live-team)：后台单独开启能力；`StartPushTaskAsync(PushMessageTypes.LiveTeam)`；仅已开始对局可选队。收到 `ITeamMessage` 的 `Sender.OpenId/GroupId`，执行玩法加入、阵营上报与履约。此能力独立于观众一键同玩。能力开启后提审检查成功结束对局和上报阵营 API。

[礼物能力配置](https://developer.open-douyin.com/docs/resource/zh-CN/interaction/jierushuoming/hudongshuju/liwushuju)：最多 8 种礼物、4 个阵营；各能力间 `group_id` 必须一致；配置与包体版本同步审核。礼物 ID 应使用实际后台配置，不能用模拟名称替代已开通配置。

### 关注与进出直播间的文档边界

控制台为关注互动提供官方飞书说明（私有文档链接已省略）。公开 web 工具未能读取正文；主任务随后通过已登录浏览器读取：2026-09-10 上线，可区分未关注、已关注、互关，仅针对当前直播间主播。进一步从本次 2.7.12 公共 API 核验如下：

| SDK 类型 / 常量 | 实际公共契约 |
| --- | --- |
| `PushMessageTypes.LiveFollow` | `live_follow` |
| `IFollowMessage` | `Sender:IUserInfo`、`FollowUser:IUserInfo`（当前主播）、`UserFollowAction:FollowAction` |
| `FollowAction` | `Follow=1`、`Unfollow=2`、`FollowBack=3` |
| `PushMessageTypes.LiveEnterRoom` | `live_enter`，同时承载进出房 |
| `IEnterRoomMessage` | `SecOpenId/AvatarUrl/NickName/GradeLevel/InviterGatherOpenid/InviterGatherNickname/InviterGatherAvatarUrl:string`，`IsOldPlayer/EnterRoomType/FollowStatus:long`，`EnterRoomScene:int` |
| `EnterRoomScene` | 0 未知 / 1 召集点击进房 / 2 分享进房 |
| `FollowStatus` | 0 无关系 / 1 已关注主播 / 2 互关 / 3 被主播关注；旧服务未下发默认 0 |
| `PushMessageTypes.LiveTeam` | `live_team`；`ITeamMessage` 为 `Sender:IUserInfo`、`AppId:string`、`RoomId:long`、`GroupId:string` |

以上消息仍继承 `IPushMessage` 的 `MsgId/MsgType:string`、`Timestamp:long`。XML 未说明 `EnterRoomType` 的进入/离开数值映射；主任务随后读取官方飞书正文确认 1 为进入、2 为离开，仍未有真实推送联调。进房中的分享来源不等于发送分享动作的观众消息，不能据此自动发放分享奖励。

[直播玩法开放能力概述](https://developer.open-douyin.com/docs/resource/zh-CN/interaction/introduction/introduction/capabilitieslist)公开确认可获取关注状态并监听关注/取消关注。该说明证明平台能力，不提供 UE/C++ 调用契约。

另一产品[直播互动工具 App+伴侣指南](https://developer.open-douyin.com/docs/resource/zh-CN/live-interactive-tools/development/tutorial/app-interaction-guide)有 `live_follow`、`use_follow_action`（1 关注 / 2 取关）和 JS 卡片订阅 API；当前使用实际 LiveOpenSDK 类型，不借用该产品字段。未据进出事件擅自实现自动加入或退出删除角色。

### 直播间对局榜

[用户战绩与排行榜 SDK](https://developer.open-douyin.com/docs/resource/zh-CN/interaction/develop/unity-sdk/live-unity-sdk-support/user-records-rankings)明确为纯客户端 SDK，不需要抖音云或服务端；不支持世界榜。

对局结束：`UpdateUserRoundListInfoAsync` 上传所有有战绩用户，每批最多 50（Top150 也必须包含）；`UpdateRoundRankListInfoAsync` 一次上传按 Score 排序的最多 150 名榜单，再次调用覆盖整榜；最后 `UpdateRoundResultInfoAsync(RoundId,CompleteTime)` 标记完成，否则本局榜不展示。

用户数据字段：`OpenId/Rank/RoundResult/Score/WinPoints/WinStreakCount/GroupId`；没有胜点或连胜传 0，排名大于 1000 可传 1000 展示 `999+`。完成时间为秒级。

### 控制台“总榜”对应世界榜，不能由单局 SDK 自动生成

主任务新增控制台现场证据：榜单 1 为“总榜”，个人积分 + 连胜，每周日 23 时截榜，无奖励；榜单 2 为“单局榜单”，个人积分 + 胜点。此处是已登录 UI 的配置证据，尚未验证展示或上传成功。

[官方能力配置页](https://developer.open-douyin.com/docs/resource/zh-CN/interaction/jierushuoming/advanced-interaction/user-rank)在能力配置中称“单局榜/总榜”，同页 API 接入把总榜明确对应四个“世界榜单”接口。因此这里的总榜就是 Unity SDK 不支持的世界榜，不是另一个自动累计的 SDK 榜单。配置的个人积分决定排名，连胜/胜点是次要展示值，均由开发者上报；周截榜配置不是自动累加能力。

[服务端接入说明](https://developer.open-douyin.com/docs/resource/zh-CN/interaction/develop/server/live-room-scope/user-scores-rank/user-record)要求开发者累计当前统计周期的战绩并排序上传。总榜用户数据和 Top150 分开存储，两类上传均为覆盖写；更新用户累计数据不会自动更新 Top150，平台不替开发者排序。单局接口以 room/round 为维度，与 world_rank_version 的总榜空间不同。

全部世界榜端点均为 `POST https://webcast.bytedance.com/api/gaming_con/world_rank/…`，Scope 为 `interactive_rank_config`：

| 操作 | endpoint 后缀 / 官方契约 | 核验来源 |
| --- | --- | --- |
| 生效版本 | `set_valid_version`；app_id、is_online_version、world_rank_version，自定义版本；每玩法一个生效版本，最多 3 次/秒 | [版本接口](https://developer.open-douyin.com/docs/resource/zh-CN/interaction/develop/server/live-room-scope/user-scores-rank/world-list-version) |
| 总榜 Top150 | `upload_rank_list`；app_id、is_online_version、world_rank_version、rank_list；整榜覆盖；最多 5 次/秒 | [榜单列表接口](https://developer.open-douyin.com/docs/resource/zh-CN/interaction/develop/server/live-room-scope/user-scores-rank/upload-data) |
| 用户累计 | `upload_user_result`；app_id、is_online_version、world_rank_version、user_list；每批最多 50，按用户+版本覆盖；最多 100 次/秒 | [累计战绩接口](https://developer.open-douyin.com/docs/resource/zh-CN/interaction/develop/server/live-room-scope/user-scores-rank/user-world-scores) |
| 截榜完成 | `complete_upload_user_result`；app_id、is_online_version、world_rank_version、complete_time:Int64；截榜后完成累计上传再标记 | [完成接口](https://developer.open-douyin.com/docs/resource/zh-CN/interaction/develop/server/live-room-scope/user-scores-rank/user-world-record) |

累计及榜单用户项为 `open_id/rank/score/winning_points/winning_streak_count`。`is_online_version=false` 为测试，true 为正式；上传不传 world_rank_version 时会写当前生效空间，实际实施应显式传周期版本以防切周串榜。世界榜 `complete_time` 页面未说明时间单位，不能仅凭单局接口单位推定。

应用调用凭证通过[直播玩法 getAccessToken](https://developer.open-douyin.com/docs/resource/zh-CN/interaction/develop/server/interface-request-credential/get-access-token)取得：POST `https://developer.toutiao.com/api/apps/v2/token`，body appid/secret/grant_type=client_credential；有效 2 小时，AppSecret 只能在开发者服务器使用。这不是 launcher/LiveOpenSDK 启动 token，不能把 AppSecret 放进 UE 或 Unity 宿主。增加总榜后端不要求将现有 SDK 评论/点赞/礼物直推改为 HTTP 中转。

当前缺失：应用 secret 未启用；没有权威跨房间累计存储、幂等对局入账、周周期版本切换/截榜任务或上述四接口的服务端实现。现有 SDK 2.7.12 的公开 IRoundApi 及 XML 只覆盖对局 API，没有世界榜契约；`Source/GeometricWarfare/ArenaLiveRounds.cpp` 当前 win_points/win_streak 均为 0，尚无有意义的胜点/连胜记录。需先明确积分累计公式、胜点计算、连胜在输/平/换房时的规则及截榜时区，再实现后端；这些业务规则不能由配置名称推导。当前 SDK 路径覆盖单局榜接线，不代表已实现配置中的总榜或其连胜指标。

### 官方服务端源码核验与后端合同夹具

用户已明确保留总榜并继续实施后端。2026-10-04 后续核验，[官方 SDK 总览](https://developer.open-douyin.com/docs/resource/zh-CN/interaction/develop/server/sdk-overview)指向 [ByteDance 官方 Go SDK](https://github.com/bytedance/douyin-openapi-sdk-go)。已只读下载公开 `client/client.go`，观察 revision `a50593c5ccdc31ab5b7cb2153947dfad612bbed9`，源文件 SHA256 `EE759CDE6BE040F742D4CB793E74565E02AF3BAB4F9CD99A8488927C66A2290D`。

源码解决 headers 冲突：`WebcastmateInfo` 与四个 world_rank 方法均设置 `x-token=request.AccessToken`、Content-Type=application/json，并使用 HTTPS。HTTP curl 模板中的 access-token 不用于这五个端点；`TaskGet` 则确实使用 access-token。不存在一个适用于所有 OpenAPI 的统一 header 名。

公开 request DTO 表明 app_id/world_rank_version 为 string，is_online_version 为 bool；用户 open_id 为 string，rank/score/winning_points/winning_streak_count 均 int64；complete_time 为 int64。公开 HTTP 请求中是 JSON integer，不是字符串，Python 必须避免经过 double。SDK 标记 world_rank_version 必填，虽然 HTTP 文档允许省略；实现统一显式传。SDK 的 rank_list.winning_streak_count 标记必填，无记录仍传 0。complete_time 的秒/毫秒单位在公开文档和 SDK 源码中仍未明；作为明确配置项并保持未验状态，不猜接口成功。

后端/客户端可用的[公开合同夹具](fixtures/douyin-world-rank-public-contract.json)已保存，含精确 headers、四操作请求、Int64 边界、失败响应和直播间身份比对案例。全部凭证是固定假值。JSON 本地解析验证 room_id=7214015683695250235、score=9007199254740993 精确保留，四个总榜操作齐全；未请求真实平台。

直播间置换 `POST https://webcast.bytedance.com/api/webcastmate/info` 使用服务端应用 x-token，body 为 `{"token":"启动token"}`；官方返回 data.info.room_id:int64 与 anchor_open_id:string。公开响应 DTO 没有 app_id，后端 app_id 来自固定服务端凭证上下文；不能把客户端 app_id 当认证证据，也不能宣称获得平台返回的 app_id。服务端成功置换后应绑定/比对 room 与 anchor，再签发随机服务会话凭证；游戏共享 API key 不能替代真实启动 token 验证。跨应用 token 是否拒绝仍需实际鉴权核验。

官方正常房间示例只有 data，没有 err_no；[官方 util](https://github.com/bytedance/douyin-openapi-util-go/blob/main/client/client.go)将缺失错误码默认 0，但会识别 err_no/errcode/error_code/errno/error/err.err_code/extra.error_code 的非零错误。因此房间置换可接受没有 error envelope 的完整正常数据，仍须拒绝非零错误、缺失房间/主播或身份不一致。2026-10-07服务器单次响应诊断实证：world_rank/set_valid_version返回HTTP200、JSON、errcode整数0且无err_no；不能强制只接受err_no。世界榜仍要求至少一个已知整数错误码且全部为0；空对象、仅消息、非数字或任意非零错误仍拒绝。

启动 token 文档仅明确有效 30 分钟，未承诺只首次兑换受限，也未提供启动 token 刷新契约。2.7.12 公共 Env.Token 是可写值，当前宿主写入原始输入；未发现公共刷新事件/获取新启动 token 方法。12 小时随机 bearer 是本服务设计 TTL，不延长平台 token、不证明持续开播，到期需重新启动授权。可选[任务查询](https://developer.open-douyin.com/docs/resource/zh-CN/interaction/develop/server/live-room-scope/data-open/query-task-status)为 GET /api/live_data/task/get（appid/roomid/msg_type），status 1 不存在、2 未启动、3 运行；这是推送任务状态而非房间在线接口，且尚未确认 SDK SinglePush 的任务在此可查。本产品独立断播回调未找到权威公开契约，不借用其他直播 SDK 的 room.status_change。

### 后端托管选择

[直播玩法抖音云指南](https://developer.open-douyin.com/docs/resource/zh-CN/interaction/develop/douyincloud/guide)明确对比“服务器部署在第三方云服务上”与抖音云：第三方后端自行接启动 token、调用 OpenAPI 置换直播间并维护应用 access_token。因此已有腾讯云服务器可以承载这个 HTTPS 后端，抖音云不是总榜必需条件。

抖音云提供容器 Dockerfile、多语言、Node.js 函数、存储组件和免鉴权/内网接入便利；指南列出 Redis/MySQL/MongoDB，并要求外网接口授权路径，否则 403。云弹性运行服务与数据存储分开，不能把实例本地 SQLite 当作已获确认的持久数据库方案。当前采用已有服务器时，HTTPS、应用 secret 服务端保管、持久目录/备份与定时任务是实施条件；无服务器规格及流量证据，不作成本数字比较。单局榜与 SDK 指令推送仍保留现有客户端路径。

## 宿主实施与尚待联调

`Tools/DouyinSdkHost` 已实现并成功构建真实 Unity 伴随宿主，使用官方公共入口，未反射 internal 或伪装 friend assembly。其匿名 stdin/stdout JSONL 协议已与 UE 提供者约定：首帧 `init` 含凭证；`GWSDK/1 ` 前缀输出 `room/status/event`；每个后续操作核对当前 room_id；凭证不进入子进程命令行，SDK 日志正文不订阅。对局、阵营、战绩批次、房间榜和完成使用对应的公开 SDK API；ACK 的本地成功仅表示 void 方法已调用，不能冒充服务端确认。实际构建与联调状态见[宿主核验记录](../verification/2026-10-04-douyin-sdk-host.md)。

构建使用独立 `tmp/DouyinSdkHostBuild`，完整 SDK 与 Unity registry PackageCache 从获取工程只读复制；本次补齐 `com.unity.ugui=1.0.0` 以满足官方包内 CloudSync 的编译依赖，不代表调用云同步。获取工程未修改。`LiveOpenSdkEnv.AppId` 实际为只读，通过 `Initialize(appId)` 设置；`Env.Token` 可写。

2026-10-05 宿主已新增本服务 HTTP 通道并重建 Player，通过模拟合同与本地 loopback HTTP 回归。官方房间认证后立即异步换本服务票；请求/响应核对 app_id/room_id/anchor_open_id，12 小时本服务票仅存宿主内存；backend_round 返回完整 5000 人回执，并拒绝房间重置前等待/在途请求的回执。HTTPS 默认验书、禁 redirect、15 秒整操作超时、2 MiB 响应上限，宿主管道单帧扩大为 4 MiB、双向队列各 16 MiB。此为 [本项目服务合同](live-backend-contract-v1.md)，并非官方 SDK 新接口；accepted 仅证明本服务持久接受，实际 AppSecret、平台 room token 和世界榜上传仍待配置与联调。

Windows Player 实测将 `Console.Out` 与标准输出句柄重定向到 `-logFile` 目标，故必须使用 `-logFile -` 保持管道，不能使用 `NUL` 或文件。UE 只接受固定协议前缀并持续排空诊断直到子进程退出。Unity managed logging 在 splash 前禁用；SDK 日志正文没有订阅。官方 Runtime 同时包含自动 `envs.log` 环境/路径诊断写入及可选性能文件写入，未发现公开接口可关闭所有 SDK 自有文件，不能宣称零文件日志。宿主使用专用工作目录；启动凭证只从 stdin 传入，未进入环境变量或命令行。没有改动 SDK 私有实现。

1. 完整 `com.bytedance.liveopensdk` 2.7.12 已取得；确认伴随宿主的公开入口、部署依赖和许可范围，完成实际 SDK 生命周期验证。
2. 公共 `ILiveOpenSdk` 已核验为 `Initialize()` 和 `Initialize(string)` 两个重载；完成 UE 与宿主的会话、消息、履约及上报链路，避免虚构原生 DLL 接口。
3. 配置真实 AppId、基础互动、礼物/group_id、快捷选队和对局榜权限，再以直播伴侣启动 token 联调；完成官方指令、实际玩法履约和阵营/结算成功证据。

[测试与提审](https://developer.open-douyin.com/docs/resource/zh-CN/interaction/introduction/zn/test%26arraignment)：后台配置开播测试账号与调试成员，伴侣开播后通过游戏玩法小扳手选择本地 exe。当前未执行完整真实直播测试，不能宣称已正式接通。

## 项目迁移边界

当前事件边界为 `Plugins/LiveInteraction`，正式提供者为 `Plugins/DouyinLiveProvider`；游戏内 GM 与模拟输入面板已删除。默认启动为正式模式，未带平台启动凭证时显示 `LAUNCH_TOKEN_MISSING`，不自动制造观众。开发演示/录像必须显式 `-GWLocalTest`；Shipping 和平台凭证启动不可进入本地模拟模式。正式 token 应通过原生 launcher 入口，不直接给内层 UE exe。

旧 `live:<UserId>` 权益存档保留，但缺少可信平台与应用来源，不会自动迁入新的 platform/app 隔离键。新增中立接口、旧逻辑回归与控制台开关核验均不能替代真实 SDK 接通证明。
