# Douyin SDK 宿主构建与管道核验

日期：2026-10-04，2026-10-05 更新后台 HTTP 通道与重建回归。本记录验证当前真实 Unity SDK 宿主的编译、进程和本地协议边界，不构成有效直播间 token、真实推送、履约、阵营或榜单成功证据。

## 实际组件

- 官方 BGDT cp 安装取得 `com.bytedance.liveopensdk` 2.7.12；公共入口 `ByteDance.LiveOpenSdk.Runtime.LiveOpenSdk.Instance`。
- Unity 2022.3.62f1c1，Windows x64 Mono player；通过 SDK 公开接口直接编译，没有反射 internal、伪装 friend assembly 或自造 SDK 原生 ABI。
- 源码 `Tools/DouyinSdkHost/Runtime/DouyinSdkHost.cs`、构建 `Tools/DouyinSdkHost/Build.ps1`；隔离工程 `tmp/DouyinSdkHostBuild`；官方获取工程未修改。
- 实际输出 `Plugins/DouyinLiveProvider/Binaries/Win64/SdkHost/DouyinSdkHost.exe`，同时包含完整 `_Data`、`MonoBleedingEdge`、`UnityPlayer.dll` 和官方构建回调复制的 `parfait_crash_handler.exe`。

## 编译证据

最后实际构建脚本返回 0；Unity `tmp/DouyinSdkHostBuild/build.log` 显示 `Exiting batchmode successfully now`、return code 0，目标 exe 存在。官方 SDK 的 `LiveOpenSdkEnv.AppId` 为只读，使用 `Initialize(appId)`；可写 `Env.Token` 从 stdin 设置。所有 Round DTO 按实际公开接口实现并通过编译。

初次隔离副本把 registry 依赖嵌入 Packages 导致 NUnit 测试源参与编译；最终保留 Unity registry PackageCache 分类。官方 SDK 包中的 UGUI 类型还要求补齐 `com.unity.ugui=1.0.0`，仅修改独立工程 manifest。以上处理未修改 SDK 源码。

## 本地协议与进程回归

实际执行 `Test-Protocol.ps1` 返回 0：

`PASS: protocol framing, malformed JSON, missing token, uninitialized ACK, disabled subscription, graceful stop; frames=6, exit=0`

验证 malformed JSON 返回 -1001；无 token 的 init 返回 -1004；未认证房间的 ACK 返回 -1005；请求未开通 fansclub 返回 -1001；stop 返回成功且进程退出 0。固定假凭证仅用于禁止能力验证，该请求在 SDK 初始化前被拒绝，输出与 stderr 未出现假凭证文本。无任何请求取得真实房间或调用平台网络上报。

实际执行 `Test-InputClose.ps1` 返回 0：

`PASS: parent stdin EOF triggers host shutdown, output drains, child exits 0`

父进程关闭 stdin 后，宿主输出 stopped 并退出；测试继续排空 stdout/stderr，防止 Unity 关闭诊断塞满管道。

实际执行 `Test-InputLimit.ps1` 返回 0：

`PASS: input record above 4 MiB rejected with failed state; output drains and child exits 0`

最后构建同时执行 Editor-only 公开接口合同检查，`tmp/DouyinSdkHostBuild/protocol-contract-checks.txt` 记录 `PASS: exact Int64 count strings, SDK gift/follow/presence fields, room/source envelope`。验证点赞 Int64.MaxValue、礼物 2^53+1 均输出十进制字符串，关注目标为 OpenId 字符串，进房字段及消息房间来源完整。运行时没有伪造事件注入入口。协议脚本还确认无凭证/未开通订阅的初始化拒绝为 `success=false,state=failed`。

## 关键运行约束

`-batchmode -nographics -logFile -` 是当前实测所需参数。Unity 会把标准输出句柄重定向到 logfile；`NUL` 或实际文件均导致协议离开匿名输出管道。协议以 `GWSDK/1 ` 为前缀，通过 Win32 stdout 句柄写入，UE 过滤其余 native diagnostics。当前输入/输出单记录限制 4 MiB UTF-8；普通输入队列限 256 条，ACK 独立队列限 8192 条，两者共享 16 MiB；输出队列限制 1024 条及 16 MiB，独立写线程避免管道背压阻塞 Unity 主线程。每帧优先处理最多 128 个 ACK，不受普通 SDK await 的 busy 阻塞，然后最多执行 128 个同步完成的普通命令；普通异步命令保持 FIFO，后台 HTTP 独立执行。所有命令、晚完成的房间等待、后台请求和关闭任务均有异常观察。早期 1 MiB/8 MiB 限制和 256 条合并输入队列只属历史版本。

SDK LogSource 未订阅；不调用会自动挂日志订阅的 `LiveOpenSdk.Init` 包装，而调用 Instance.Initialize；Unity managed logging 在 splash 前关闭。token 不进入子进程命令行、环境变量、协议返回或异常正文。官方 Runtime 有自动 envs.log 的环境/路径写入和可选 profile 文件代码，尚无已确认公共开关能取消所有 SDK 自有文件；不可宣称无任何文件日志。测试工作目录已隔离，本次测试在仓库根生成的 envs.log 已清理，未读取或记录其正文。

所有后续命令校验 room_id；room 由 SDK 返回后才发出；连接状态 disconnected 阻止推送事件，connected 重发官方 room；换房清空未履约消息。ACK 只接受当前房间已发出的消息，完成应用/渲染之后由 UE 发送；SDK 的 void ReportAck 返回只证明本地已提交。对局上报返回实际 IRoundDataRes.ErrCode，SDK 异常只传固定错误码。

待履约集合与已提交幂等历史分别限 8192 条。已提交同 ID/type 的 ACK 重试返回 `submitted`，避免 UE 等待超时后重复调用 SDK；已提交消息重推不重复发给 UE。换房清空两集合。初始化/订阅失败与 token 过期禁用事件；SDK 连接回调先发 connected 状态，再在已有官方房间时重发 room，以恢复 UE 来源会话。停止对推送停止任务最多等 750 ms，再反初始化；输出队列最多排空 750 ms。完整网络断线/回连仍需真实房间验证。

## 原生凭证入口核验

UE 引擎在提供者初始化前记录启动参数，因此直接给 UE `-token=` 会绕过宿主侧管道保护。已新增 [原生启动器](../../Tools/DouyinLauncher/README.md)，本机 MSVC x64 `/W4 /WX` 编译返回 0，输出 `tmp/DouyinLauncherBuild/DouyinLauncher.exe`。它剥离官方 token 参数，以 64 KiB 匿名管道向 suspended 子 UE 先写完整 UTF-8/LF（最多 16 KiB），关闭 writer 后再恢复进程；失败只终止自有子进程。

实际执行 `pwsh -NoProfile -File Tools/DouyinLauncher/Test-Launcher.ps1` 返回 0：

`PASS: 10 launcher cases; token stripped, stdin exact UTF-8/LF, local flag guarded, Windows quoting including empty/Unicode/quotes/backslashes and executable path spaces, duplicate/newline/missing/oversize rejected, 16 KiB boundary accepted, child exit 37 preserved`

假子进程只读取并比较固定测试值，不输出凭证。测试确认子 argv 无 token，凭证存在时移除大小写不同的本地测试开关，调用者伪传 stdin 标志被删除，精确转发普通参数并保留退出码。此记录不证明已替换发布包根 bootstrap 或 UE 已成功读取真实平台凭证。

## 2026-10-05 后台 HTTP 通道

2026-10-05 00:05 再次运行 `Build.ps1` 成功，Unity 返回 0；包含 `LiveBackendClient.cs` 的新 Mono Player 已输出。隔离副本客户端源码 SHA256 为 `771660F7DBAEDAD3EACFF32774F4722FE4B32A671CC254D4D42475A314E83C07`，与工作区一致。新 Player 的 Protocol、InputClose、InputLimit 三脚本重新执行全部返回 0，Editor SDK 字段合同检查仍通过。

后台 HTTP 通道按 [服务合同](../integration/live-backend-contract-v1.md) 实现，SDK 房间认证后立即异步 `/sessions`，不等 420 秒对局结束；失败独立为 backend_pending，不伪造 SDK 房间失败。验明 app/room/anchor 后仅内存缓存本服务 bearer，最大 12 小时；过期不自动重用平台启动票。HTTPS 保留默认证书验证、禁重定向、15 秒整操作超时、2 MiB 响应上限；显式开发 HTTP 只准 localhost/127.0.0.1。错误只发固定/数值码；不转发原始响应或凭证。旧房间在等待锁或请求在途时重置后均拒绝回执。

实际运行 `Test-BackendHttp.ps1` 返回 0；模拟 handler 合同与临时本地 TCP HTTP 服务均通过，后者验证实际 Mono HttpClient 的路径、Authorization、UTF-8 请求与完整回执。覆盖提前认证、5000 人完整快照（原始含展示字段快照大于 1 MiB）、`/live-api` 基址、完整 Int64 分值、仅首次使用启动票、后续使用本服务票、身份不匹配、过期拒绝、排队/在途代数重置、307/401 固定错误、已知/未知长度 2 MiB 超限、取消和错误脱敏。临时 listener 因 Windows 沙盒限制使用批准执行，所有凭证均为固定假数据；未访问真实平台。

## 2026-10-05 ACK 调度修复

2026-10-05 额外修复 ACK 调度阻塞：独立审查发现普通 SDK round 等请求 await 时原合并队列停止出队，257 条 ACK 可触发上限。先在 Editor-only `HostSchedulingTests.cs` 直接调用真实宿主 Update/Execute，公共 SDK 假实现控制 round Task 等待；修复前 Unity 返回 1，实际失败于第 256 条 ACK 入队（另有一条普通命令）。修复后同测试证明 600 条 ACK 在 round 未完成前调用 ReportAck，首帧仅 128 条、后续普通 user_group 仍等待，ACK 不清普通 busy；8192 ACK、256 普通条数与共享 16 MiB 满时均拒绝。

此修复再次实际重建 Unity Player 返回 0；`host-scheduling-checks.txt` 为 `PASS: real host Update delayed SDK round + 600 ACKs, 128/frame budget, normal FIFO/busy isolation, 8192 ACK and 256 normal limits, shared 16MiB`。新 Runtime 源与隔离构建副本 SHA256 均为 `1ECF87160A03953884E94C2FC1765A9EAD2E1B714761E279001863780C32EBCD`。新 Player 的 Protocol、InputClose、4 MiB InputLimit 再次全部通过。假实现只位于 Editor，并未反射 SDK 私有接口或增加运行时模拟入口。

独立 reviewer 重新检查 inputLock 的两队列计数/字节、ACK 与普通 busy 分离、普通 FIFO 和实际 Update/Execute 延迟用例后确认原 P1 关闭；reviewer 为代码审查，未独立运行构建。

## 未验证

尚未使用真实启动 token 验证 AppId 鉴权、订阅成功、进出房/关注/礼物/选队消息、官方履约、阵营上报和结算榜单。Spout 上屏、技术白名单、包体上线/云启动许可与完整 UE 提供者联调也未由本记录验证。
