# 后端接入包与 OpenAPI 自查收件工具验证

## 已完成

- `Config/DefaultGame.ini` 的 `BackendUrl` 已设置为 `https://example.com/live-api`；服务器 AppSecret 未进入游戏配置。
- 实测公网 HTTPS GET `/live-api/healthz` 返回 `ready`，`uploads_enabled=false`，`complete_time_unit=pending_verification`，`publication_state=completion_configuration_required`。总榜上传未启用，周完成时间单位仍需平台确认。
- Shipping 构建/cook/stage/archive 成功，日志 `Saved/BackendDebug-Shipping-Package.log`。临时 Zen 重试后最终成功，不把中间日志当作失败结论。
- 从新游戏的 `.pak` 提取 `DefaultGame.ini`，确认 HTTPS 地址确实打包；证据 `tmp/BackendDebug-PakInspect/GeometricWarfare/Config/DefaultGame.ini` 和 `Saved/BackendDebug-PakInspect.log`。
- 根凭证启动器和 SDK `Assembly-CSharp.dll` 与构建源输出 SHA 一致。保留全部 SDK 依赖，ZIP 不含 `.env`、SQLite、PDB、日志或服务端 settings.json。
- 后端客户端真实回环 HTTP/模拟响应合同测试通过：早期认证、base path、会话复用/过期、房间代际隔离、5000 用户、Int64、限流/取消及脱敏错误。证据 `Saved/BackendDebug-HttpTests.log`。
- 新包体无凭证启动实际收到 1889 个 1920×1080 Spout 新帧，尺寸错误 0；截图显示等待 SDK/伴侣连接、0/5000 人、无模拟观众，保留设置和主播助战。证据 `Saved/BackendDebug-Shipping-Smoke.log`、`Saved/Screenshots/BackendDebug-Shipping-Smoke.bmp`。测试后终止本次启动的进程树；不宣称 Shipping 正常退出已验证。

## 交付

- 用户上传游戏：`output/GeomeWar_1.1.6.zip`，121466342 bytes（115.84 MiB），205 个条目，根 `GeometricWarfare.exe` 为凭证启动器。
- 游戏 SHA-256：`DE03FE8B14D19852DD711BFE0A725451F6168A045A42FEB40473BD98E9A7BF46`。
- 独立服务器部署包：`output/douyin-selftest-deploy-20261005.zip`。这是测试收件服务，不能作为游戏包上传。
- 收件部署包 SHA-256：`AC975EA14C40D8F6F1DAC151BAC694C6C2E9A25C6519891D08CEF13325B682D0`。

## 测试收件服务

`Services/DouyinSelfTest` 使用 Python 标准库，监听 127.0.0.1:8766。计划以独立 IIS HTTPS 站点 `test.example.com` 接入，配置/SQLite 在独立保护目录。仅保存模拟消息，不调用 SDK/鉴权/成绩/榜单/履约 API。

11 项测试通过，包括官方 MD5/base64 签名向量、UTF-8、篡改、过期签名头、房间/类型隔离、重复/冲突消息、批次事务回滚、Int64、大容量限制、实际 HEAD 200 / POST 收件 / 本地 CLI 检查和公共消息接口不存在。证据 `Saved/SelfTest-Final.log`。3 个部署 PowerShell 文件均通过语法解析，IIS/证书/任务安装尚未在腾讯云服务器执行。

该测试密钥默认 `default`，是公开模拟值，不证明发送者为平台；只用于隔离模拟收件箱。服务要求受限的虚拟 roomID `1000000000000000000`、消息类型和签名。HTTP 200 表示已保存，**不表示游戏渲染/履约通过**。当前没有把 HTTP 模拟数据注入 SDK 游戏的 provider；游戏仍需直播伴侣和真实测试直播间验证。

## 未完成的外部步骤

- 测试子域名 DNS A 记录和匹配的可信 HTTPS 证书尚未核验/创建；未证明测试域名公网 HEAD 可用。
- 没有服务器远程管理入口，因此未在服务器部署独立收件服务。
- Chrome/Edge 自动化报 `nodeRepl.fetch request failed`。尝试打开 in-app 控制台后，库存确认页面已存在，但绑定已打开页面仍超时；没有进入自查工具保存配置，没有以截图冒充实时状态。
- 游戏尚未由用户上传；真实 SDK 房间鉴权、整局提交、伴侣验收和平台榜单仍待测试。
- `AllowPlatformTestGifts=False` 保持已确认规则，不能宣称本包允许自查模拟礼物直接触发效果。

## 官方资料

- https://developer.open-douyin.com/docs/resource/zh-CN/interaction/develop/test-tools/openAPI-tool
- https://developer.open-douyin.com/docs/resource/zh-CN/interaction/develop/server/live-room-scope/data-open/data-open-desc
- https://developer.open-douyin.com/docs/resource/zh-CN/interaction/develop/unity-sdk/live-unity-sdk-support/direct-push-ability
