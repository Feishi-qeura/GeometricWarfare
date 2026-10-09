# Live Interaction

UE 5.8 Runtime 插件，游戏仅依赖平台中立事件与提供者接口。默认启动选择 `douyin` 提供者；未注册时状态为 `SDK_UNAVAILABLE`，不会创建模拟观众或接受无会话来源的事件。官方 SDK 宿主和平台接线由独立 `DouyinLiveProvider` 插件负责，中立插件本身不含平台二进制。

## 模式与会话

开发构建必须显式传入 `-GWLocalTest` 才会启用本地模拟。Shipping 永不启用；出现平台 `-token` 或 `-GWCredentialStdin` 启动参数时，即使同时出现本地标志，也强制正式模式。`IsLocalTestMode()` 表示允许本地测试；`IsConnected()` 仅表示正式提供者已建立认证房间会话。自定义 WebSocket 的连通性不代表平台 SDK 连通。

`FLiveComment`、`FLiveLike`、`FLiveShare`、`FLiveGift`、`FLiveFollow`、`FLivePresence`、`FLiveTeamSelection` 包含 `FLiveSession` 来源，`Deliver*` 只接受当前平台、应用、房间、主播、代次、随机 nonce 与模式均匹配的事件。nonce 是插件内的会话标识，不是平台登录 token。切换平台、房间或断开会话会取消头像请求、清空头像缓存并发出 `OnSessionChanged`。游戏也独立校验来源。

事件 `UserId` 保持平台原始稳定 ID。持久权益和头像请求使用 `GetScopedUserId()`：本地保留 `local:<user>`，正式模式按平台与应用使用长度前缀编码，防止不同平台或应用的相同用户串混用。未经认证的正式模式返回空身份键。

## 提供者接口

独立的平台模块通过 `FLiveInteractionProviderRegistry::Register(PlatformId, Factory)` 注册 `ILiveInteractionProvider` 工厂，不修改游戏规则。`StartPlatform()` 创建该平台提供者；`Start()` 可保持连接中。只有已选择的提供者能调用 `BeginProviderSession(*this, AppId, RoomId)` 建立来源，返回的 `FLiveSession` 随实际 SDK 事件提交。断线时调用 `EndProviderSession()`，重连必须取得新的会话。`Deliver*` 在对应游戏消费者尚未订阅时返回 false 且不消费事件 ID；提供者必须有界缓存并在消费者就绪后保留原事件 ID 重投，false 不可履约 ACK。true 仅表示事件已分发，实际 SDK ACK 仍须依据游戏处理结果执行。接口要求游戏线程调用；提供者负责将 SDK 回调调度到游戏线程并依据实际 SDK 核验身份、应用、房间和消息类型。

`BeginProviderSession` 可接受第 4 个参数 `AnchorUserId`。提供者实现 `SendCommand(RequestId, Operation, Payload)`，并以 `ReportCommandResult` 回报实际平台结果；`SubmitCommand` 返回请求 ID 只表示提交成功。`OnCommandReply` 包含原会话、请求 ID、成功状态、错误码和可选 JSON `Data`；旧的 `OnCommandResult` 保持兼容，旧会话回执被拒绝。提供者应在命令返回后异步报告结果。

正式结算先确认 SDK 对局结束，再以 `backend_round` 提交全体真实参与者快照；服务端回执通过完整用户集合、局号、整数指标校验后，才填入 SDK 战绩和直播间榜单。[LiveInteraction] `WorldLeaderboardEnabled` 默认为 true；缺少服务端配置或失败会保留结算等待重试。显式 false 仅适用于单局联调。服务端 Accepted 表示已持久入账，世界榜平台上传另行异步重试，不能据此声称平台世界榜已更新。

游戏在 `Saved/LivePlatform/RoundOutbox` 原子保存整局冻结步骤，用追加日志保存成功回执及服务端指标。重启或重连仅恢复同平台、应用、房间的未完成步骤；跨房间不会注入历史战绩。快照和日志禁止凭证字段，认证由独立提供者持有。最后完整回执后删除记录；损坏文件保留并阻止继续上报，待恢复。回归包含 5000 人体积、十进制 Int64、回执身份集合、灰队零指标和同房重连。

游戏处理后将事件排入待履约队列，HUD 提交绘制后才提交 ACK；NullRHI 不提交。ACK 的异步失败保留原消息并退避重试，成功回执才移除。`NotifyEventHandled` 只接受官方类型 `live_comment/live_like/live_gift/live_follow/live_enter/live_team`；正式分享没有对应 SDK 能力，不发送分享 ACK，也不授予推测的分享武器。对局上报另由游戏 reporter 按实际结果回执串行执行。

此接口没有猜测官方 C ABI。礼物名称仍是现有玩法约定：仙女棒、能力药丸、魔法镜、甜甜圈、能量电池。真实平台适配器应按控制台配置把真实礼物 ID 映射到玩法名称，禁止从评论文本推导付费事件。

## 开发协议与校验

`ConnectRelay()` 仅在本地测试模式可用。状态使用 `DEV_TEST_PROTOCOL_*`；`FLiveDevEventDecoder` 的 `room_id` / `type` / `payload` / `sec_openid` 信封是历史自定义开发协议，**不是官方 SDK 协议**。开发 relay 激活后四种 `Simulate*` 调用拒绝，断开后返回显式本地测试会话。

自定义开发协议保留批次整体校验、64K 字符与 100 条记录上限，本地点赞和礼物增量为 1–100 整数。正式提供者的点赞与礼物保留 int64 正增量，不套用开发协议的 100 上限；点赞恢复最多 20 次即满血，基地药丸批量按常数时间计算。所有事件共用 4096 个事件的有限去重窗口，键包括平台、应用、房间和消息 ID。无效事件不占用 ID。这不是持久履约账本，重启、窗口淘汰后的付费去重与失败补偿仍待独立实现验证。

头像只接受 HTTPS，下载上限 2 MiB、源尺寸上限 1024×1024；CPU 裁剪缩放到 64×64 后创建纹理。缓存上限 128 张，会话切换取消下载，请求票据防止旧结果覆盖新头像。

## 自动化

`LiveInteractionTestAdapter.h` 只在开发自动化测试中提供明确的 `EnableLocalTest`、`Stamp` 和无网络开发 relay 测试入口。现有事件解码、礼物校验、战斗与头像回归保留。新增 `GeometricWarfare.LiveInteraction.ModeAdmission`、`ProviderSessions`、`ConsumerReadiness` 以及 `GeometricWarfare.Arena.ProductionAdmission` 覆盖默认拒绝、缺失提供者、平台/应用身份隔离、旧会话拒绝、生产空场与测试指令禁止。本地测试通过不代表真实平台 SDK、履约、榜单或云启动已完成。
