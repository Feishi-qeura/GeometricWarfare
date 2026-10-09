# Douyin Live Provider

Windows UE 5.8 提供者模块，依赖平台中立的 `LiveInteraction`。它不让游戏依赖 Unity API：实际官方 LiveOpenSDK 2.7.12 由 `Tools/DouyinSdkHost` 的 Unity 伴随进程承载。SDK 的公共入口依赖 Unity，没有伪造 UE 原生 DLL 接口。

## 启动与协议

打包根目录使用 `Tools/DouyinLauncher` 生成的凭证启动器。平台 `-token=...` 不转发到 UE 命令行；UE 只收到 `-GWCredentialStdin` 和匿名 stdin 中的 UTF-8 令牌行。SDK 宿主再由提供者创建，只从匿名管道的 `init` 帧取得令牌。不要直接把凭证传给 UE 内层 exe 或编辑器，它们在插件初始化前就可能记录启动参数。

SDK 宿主固定路径是本插件 `Binaries/Win64/SdkHost/DouyinSdkHost.exe`。`Build.cs` 将完整 Player/Data/Mono/native 依赖以 NonUFS 随包部署。先构建宿主，再进行 UE 打包；缺文件显示明确状态，绝不回退模拟。

UE 工作线程处理 UTF-8 完整行，stdout 只接收 `GWSDK/1 ` 帧；Unity 诊断行被丢弃。输入、输出、未处理事件与请求队列均有限额。只在 SDK 初始化、连接成功、取得匹配 AppId 的真实房间及主播身份后创建正式会话。断线使旧会话失效；永久失败停止自有宿主，保留失败状态。

订阅 `live_comment/live_like/live_gift/live_enter/live_follow/live_team`。点赞与礼物 Count 用十进制字符串保留完整 Int64，避免 JSON double 精度损失。事件来源限定当前房间；一键同玩的观众收礼分支不进入本玩法。

## 配置

`Config/DefaultGame.ini` 中 `[DouyinLiveProvider]` 配置 AppId；`[LiveInteraction]` 配置 RedGroupId、BlueGroupId、GrayGroupId。当前已核实为 Red、Blue、Grey。`WorldLeaderboardEnabled=True` 保留周总榜要求；服务部署并核验后，将 `BackendUrl` 设置为实际 HTTPS 服务基址。地址留空会使总榜结算保持待提交，不会伪造成功。仅非 Shipping 且显式 `AllowInsecureBackendLoopback=True` 才允许本机回环 HTTP 开发地址。

`[DouyinLiveProvider.Gifts]` 每项为 `玩法礼物名=官方加密 SecGiftId`，ID 作为值可保留 base64 的 `=`。五种礼物从控制台链接的官方礼物池逐项读取。未知 ID 不产生效果、不伪造 ACK，状态显示礼物映射缺失。

官方 `IsTestData=true` 礼物包括版本审核的互动表现测试，必须执行玩法并在展示后提交履约。此标记贯穿到游戏：测试武器只保留到本次会话结束，不写正式权益或自动装备选择；受测试礼物影响的会话仅提交阵营和对局生命周期（测试回合以平局关闭），不提交个人战绩、直播间榜单或世界榜后端。旧的 `AllowPlatformTestGifts` 开关不再使用。本地 `-GWLocalTest` 仍走独立身份空间。

`gift_delivery` 诊断仅记录五种已知礼物名称、数量、测试标记、投递结果和拒绝原因；不记录用户或消息原文。`mapping_missing`、`other_audience`、`invalid_count`、`invalid_test_flag`、`delivery_rejected` 可区分真实回调在哪一层未进入玩法。

命令 `ack/round/user_group/user_results/room_rank/complete` 通过请求 ID 与会话关联回执，30 秒超时返回失败。`backend_round` 预留 45 秒，覆盖首次会话换票与结算提交两个 HTTP 请求。包含 JSON 数据的回执仍须通过请求和当前会话校验，不能替代业务层的结算身份校验。ACK 的成功只代表官方 void 方法已提交，不等于服务端远端确认。游戏处理并提交 HUD 绘制后才发 ACK。

SDK 宿主承载后端 HTTP 客户端，服务端持有 AppSecret。UE 的 HTTP 调试日志可能记录请求头，故后端 bearer 不经过游戏或 UE HTTP 层；不得在配置文件里放 AppSecret、平台启动令牌或后端 session_token。

## 边界

当前是可供真实联调的实现；本地编译和协议测试不能证明平台鉴权或审核通过。游戏已保存冻结结算及成功步骤回执，可在相同平台、应用、房间恢复；这不等于跨崩溃的付费礼物履约账本。测试/正式切换须隔离测试存档与旧 outbox。真实令牌、测试主播、直播伴侣权限和官方诊断仍需实测。参见项目 `docs/integration/2026-10-04-live-debug-guide.md`。
