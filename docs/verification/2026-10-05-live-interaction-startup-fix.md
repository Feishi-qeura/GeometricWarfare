# 2026-10-05 直播互动初始化修复

## 已确认的故障

原 `Config/DefaultGame.ini` 写的是未加引号的 `BackendUrl=https://example.com/live-api`。
UE 5.8 配置解析启用 `SwallowDoubleSlashComments`，实际读到的值是 `https:`。
宿主创建 `LiveBackendClient` 时 URL 校验抛出参数异常，初始化返回 `-1001/failed`，在 SDK 初始化和消息订阅前结束。
这条启动路径解释了游戏画面正常、互动不起作用和不能建立平台对局的表现。

证据：

- `Saved/LiveBackendUrl-Red.log` 的运行时测试明确记录：预期完整 HTTPS 地址，实际 `https:`。
- 正式包启动链路生成的安全日志记录 `credential_received`、`host_started`、`init_queued`，随后 `init -1001/failed`；无房间就绪。
- 同一个 Unity 宿主直接接收完整后端地址时可完成 SDK 初始化并进入 `waiting_room`。
- 本机安装的 1.1.6 启动器、Shipping 主程序、SDK 宿主程序集与原上传 ZIP 的 SHA256 相同，排除这三项文件拿错版本。

## 修改

- 后端 URL 改为带引号的 INI 值，避免 `//` 被当成注释。
- 修正另一个启动顺序缺陷：全部期望消息订阅成功前，不向 UE 发出房间就绪，不提前开始平台对局。
- 新增宿主和 UE 侧独立诊断 JSONL，只记录已知阶段、错误码、消息类型和计数。
  不记录 token、AppSecret、用户 ID、房间 ID、评论内容、原始响应或异常正文；单文件上限 1 MiB。

## 验证

- UE 同一运行时配置测试由失败转为 Success：`Saved/LiveBackendUrl-Green/index.json`。
- Unity 编辑器测试通过：部分订阅成功不得发房间就绪、全部成功才发房间、订阅失败清除会话。
- Unity ACK 延迟调度和边界测试、诊断隐私及文件上限测试通过。
- 实际宿主协议测试通过：JSON 拒绝、缺凭证、未初始化 ACK、禁止订阅、正常退出。
- Shipping 编译、烘焙和最终包启动链路结果见最终交付时的 `Saved/LiveLaunchRouting-Green.txt`。

## 平台现场核验

- 调试版本 `1.1.6_`，没有线上版本。
- 调试成员包含 `绯世` 与 `xhcxhmy`。用户确认绯世的抖音号是 `98332982686`，xhcxhmy 开播。
- 用户快捷选队能力已开启，三队 Group_ID 为 `Red`、`Blue`、`Grey`，与包内一致。
- 评论、点赞、礼物已开通；关注推送开关开启；评论配置含 `加入`、`1`、`2`。
- 没有修改开发平台开关、成员或 HTTP 推送配置。

## 仍需实播验证

以上完成本机故障复现和修复验证。没有使用真实主播启动凭证重放会话，尚未证明新包实播的
认证、六类订阅、平台开局、评论消费和手机端快捷选队展示全部成功。
快捷选队按官方 SDK 文档只在平台已开始对局的状态下生效，不能把能力开关开启等同于手机按钮恢复。
服务器无需为本次修复重新部署，自查 HTTP 收件箱仍只用于 HTTP 模拟数据测试。

实播步骤：上传 1.1.7，部署完成后由 xhcxhmy 启动新调试包；等游戏已连接；
绯世进入直播间，先发送 `加入` 再发 `1`，核对入场和红队，再测试快捷选队。
若仍失败，读取 `%LOCALAPPDATA%/GeometricWarfare/Saved/LivePlatform` 下本次生成的
`provider-diagnostics-*.jsonl` 和 `HostSessions/*/host-diagnostics.jsonl`，无需发送密钥。

## 最终交付检查

- 上传包：`D:/demo/GeometricWarfare/output/GeomeWar_1.1.7.zip`。
- 206 个文件，121,477,368 bytes，115.85 MiB。
- SHA256：`083452CEED684473870F2121135820D50B43C708250835DE97750506C23EAD36`。
- ZIP 保持上一版的单个版本目录结构，根目录启动器、Shipping 程序、SDK 宿主齐全。
- 最终文件夹实际运行通过：`backend_configured` -> `sdk_available` -> `token_loaded` -> `sdk_initialized` -> `waiting_room`。
  测试凭证不是实际主播凭证，未创建认证游戏会话；仅证明初始化故障已排除。
- 最终 SDK 程序集与构建结果 SHA256 一致；包中没有 envs.log、运行诊断日志、.env、SQLite 数据库或 PDB。
- 包体使用说明给出两个已绑定账号的实播步骤和安全日志位置。
