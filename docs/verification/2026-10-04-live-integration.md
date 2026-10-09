# 直播平台接入验证记录

应用 `YOUR_APP_ID`；UE 5.8 Win64 Development；官方 LiveOpenSDK 2.7.12。此记录区分本地实现验证与真实平台验收，不代表玩法已上线。

## SDK、GM 移除及画面链路首轮结果

在新增周总榜服务端之前，以下结果已实际完成：

| 检查 | 结果与证据 |
| --- | --- |
| Editor / Game 构建 | 成功；`Saved/Spout-Final3-Editor-Build.log`、`Saved/Spout-Final3-Game-Build.log` |
| UE 自动化 | 22 成功，0 失败，0 警告，0 未运行；`Saved/Spout-Final3-Automation/index.json`，报告时间 UTC 2026-10-04 15:29:31 |
| 原生玩法回归 | Weapons 4013、Expansion 12930、View 28 项断言通过 |
| 凭证启动器 | MSVC `/W4 /WX` 编译，10 项假子进程测试通过；不使用真实令牌 |
| Unity SDK 宿主 | 实际 Player 构建、无凭证协议、stdin 关闭和输入限额测试通过；详见同目录宿主记录 |
| 固定分辨率 Spout | 独立接收器收到 491 个新帧，均为 1920×1080，错误尺寸 0；本机预览实际 640×360 → 320×180 → 640×360 |
| Spout 内容 | 已查看 `Saved/Screenshots/Spout-Final-Resize-Received.png`，含游戏和 Canvas HUD；日志 `Saved/Spout-Final-Resize-GPU.log`、`Saved/Spout-Final-Resize-Receiver.log` |
| 云启动参数 | 本地实际运行两种参数形式，均生成 1080×1920 画面并正常退出；不是官方云端验收，也未重排竖屏 HUD |
| 首轮完整联调包 | UAT `BUILD SUCCESSFUL`、exit 0；`Saved/Douyin-Debug-Package.log`。包含完整 Unity Player、Mono 和 Spout 依赖 |
| 包内根启动器 | 已替换原 UE bootstrap 并比对 SHA256；正常根入口无令牌启动，截图 `Saved/Screenshots/Douyin-Package-NoToken.png` 为 0 位观众、等待直播伴侣、无 GM；`Saved/Douyin-Package-NoToken.log` 有正常关闭记录 |

根启动器 SHA256：`4A236E27FC8F0F5A859F9FA0BEB09DD4C67F5B2F8AA6E04C18220B6264E3C400`。打包目录为 `output/douyin-live-debug-20261004/Windows`。以上首轮包尚不包含之后增加的周总榜后端链路，须重新构建和打包后用于当前联调。

本轮回归曾发现并修复：持久局号文件移动 API 的布尔成功值判断错误；JSON Number 经 TryGetStringField 转换后绕过 Int64 精度保护；正式 HUD 的换形提示仍显示旧数字。前两项已进入首轮 22 项通过结果；最后一项须随后续包重新核验。

## 周总榜扩展

用户确认保留总榜并实现服务端，按每周日香港时间 23:00 重置；累计最终个人积分、胜场数和连续胜场，灰队不计入。后端部署优先采用现有上海地域 Windows 腾讯云服务器，2 核 4GB，现有 IIS 站点，域名 `https://example.com/`。这些配置来自用户确认；空闲资源、IIS ARR/URL Rewrite 模块、实际站点绑定及远程部署方式尚未核验。

内部合同见 `docs/integration/live-backend-contract-v1.md`。公开平台合同与来源见 `docs/integration/fixtures/douyin-world-rank-public-contract.json`。用户随后明确：跨周迟到局计入服务器首次成功接收的周次，后续重试沿用原局归属，不重复累计。

## 2026-10-05 最终本地回归与打包

| 检查 | 实际结果 |
| --- | --- |
| UE Editor / Game | `Saved/LiveBackend-Editor-Build2.log` 成功 6.61 秒，`Saved/LiveBackend-Game-Build.log` 成功 35.41 秒 |
| UE 完整自动化 | `Saved/LiveBackend-Automation/index.json`：24 成功，0 失败、警告或未运行；UTC 2026-10-04 16:20:25 |
| 5000 人结算 | 2000 红 / 2000 蓝 / 1000 灰；最大 fixture 快照 2,192,159 UTF-8 字节，持久 outbox 4,663,302 字节；完整 ID 集合、整数指标、同房恢复和跨房隔离通过 |
| 后端独立复跑 | `Saved/LiveBackend-Final37-Python-Tests.log`：37 项通过，1.339 秒；覆盖身份、幂等、跨周时钟/等待写锁、首次接收归周、乱序连胜、灰队、发布顺序、限额、WAL 在线备份和恢复 |
| 实际 HTTP 服务 | `Saved/Backend-Native-HTTP-Smoke.json`：真实 uvicorn 回环运行，明文鉴权 403、可信回环 HTTPS 转发无凭证 401、健康状态未配置；没有真实平台调用 |
| 后端本机基准 | `Saved/Backend-5000-Benchmark.json`：5000 人首次入账 0.1629 秒、重复提交 0.0275 秒；102 个待发布任务。仅本机工作站结果，不代表腾讯云机器或平台上传速度 |
| 最新 SDK 宿主 | 最终 Unity Player 构建返回 0；HTTP mock/实际本机 TCP、协议/EOF/4 MiB 限额、SDK 字段及延迟 SDK + 600 ACK 调度回归通过，详见宿主记录 |
| 最终完整包 | `Saved/LiveBackend-Final-Package.log`：UAT 成功，exit 0，84.02 秒；仍位于 `output/douyin-live-debug-20261004/Windows` |
| 最新包实际运行 | 根启动器无令牌运行 exit 0；`Saved/LiveBackend-Package-Smoke.json`。已查看 `Saved/Screenshots/LiveBackend-Package-NoToken.png`：等待伴侣、0 位观众、无 GM、正式字母换形提示 |
| 宿主随包一致性 | 源与包内 `Assembly-CSharp.dll` SHA256 均为 `80B06B1BB295E8FEF2C3DD103444E983F476CB255116EC8E67304695C787656A` |
| 后端部署包 | `output/live-backend-deploy-20261005-003650.zip`，52,670 字节、32 个文件；白名单打包并检查无 `.env`、数据库、日志、虚拟环境或缓存。SHA256 `08598F2C40CD5B67F0890B10C5238ACA77FEC5A48C13B41A077568CE72CF151B` |

独立只读审查发现并关闭三处 P1：未来 30 秒的客户端结束时间可能提前激活新周；迟到局原只归档而漏计；普通 SDK await 阻塞 ACK 导致 256 条合并队列溢出。后端前两项已由独立内存数据库检查复核；ACK 已分离为 8192 条队列、普通队列 256 条，共享 16 MiB，真实 Host 调度红绿测试验证 600 ACK 与普通 FIFO。审查结论是可进入真实联调，并非平台或生产验收通过。

本机最初普通沙箱的 ASGI/线程测试出现等待；在批准的正常 Windows 执行上下文，同一正式测试套件通过。一次 UE 编译失败来自 5.8 JSON shared-string API 差异，已改为公共 TryGetField 及显式字符串转换，再完成上述两目标构建和 24 项回归。

客户端 outbox 的自动隔离边界是 platform/app/room；测试/正式环境切换需执行部署指南中的独立用户目录或测试数据隔离流程。普通互动的未展示/未 ACK 重投不是冻结结算恢复保证，真实同进程掉线也需要单独验收。

## 平台验收仍待完成

- 有权限测试主播、调试成员、直播伴侣实际启动与真实 SDK 房间鉴权。
- 实际评论、点赞、关注、快捷选队、五种礼物、平台履约结果和单局榜。
- 服务器部署、服务端 AppSecret 安全配置、真实房间令牌换票、周总榜上传及版本切换。
- 世界榜 `complete_time` 秒/毫秒单位：公开接口只标 Int64，未写明单位；配置和联调核验前不宣称截榜成功。
- 直播伴侣实际 Spout 接收、官方云启动及云节点可用日志路径。
- 高人数平台频控、上传耗时与现有 2 核 4GB 服务器容量。

跨崩溃的付费礼物履约账本尚未实现；有界内存去重与 ACK 提交记录不能证明进程崩溃后不会重复消费。未启用互动扩展组件和透明背景；观众一键同玩按用户要求不实现。
