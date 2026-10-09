# 抖音 OpenAPI 自查工具测试收件服务

这是独立的 **HTTP 模拟数据收件服务**，可与已部署的后端共用一台腾讯云 Windows 服务器。它不持有 AppSecret，不调用平台成绩/榜单/履约接口，不修改真实 SDK 的 SinglePush 接入。**HTTP 200 只表示测试消息已保存，不能作为游戏效果、SDK 鉴权或履约通过的证据。模拟角色不会自动进入当前游戏。**

## 服务器部署

1. 在管理 `example.com` 的域名解析控制台添加 A 记录：主机记录 `test`，记录值为当前腾讯云服务器公网 IPv4。不要修改已有 `@` / `www` 记录。结果为 `test.example.com`。
2. 为这个测试域名申请/安装可信 HTTPS 证书；安装到服务器 LocalMachine/My，并包含私钥。原来的证书只有在包含测试域名或 `*.example.com` 时才能复用。不得跳过证书验证。服务器已有 HTTPS 443 入站权限可复用；8766 不对公网开放。
3. 解压本包到服务器桌面等临时目录，以管理员 **Windows PowerShell 5.1** 进入解压目录，执行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Install-Receiver.ps1
```

默认复用 `C:\Services\LivePlatformBackend\.venv\Scripts\python.exe`，无需 pip 安装第三方依赖。若 Python 路径不同，传 `-PythonExecutable`。若匹配的 HTTPS 证书不唯一，传 `-CertificateThumbprint` 选择已安装证书。

安装器先检查证书、IIS Rewrite/ARR、端口和资源冲突，再创建**独立站点** `GeometricWarfare-DouyinSelfTest`、计划任务、`C:\Services\DouyinSelfTest` 和 `C:\inetpub\GeometricWarfareSelfTest`。原站点和 `/live-api` 路由无需编辑。配置/SQLite 仅管理员和 SYSTEM 可访问，公共网站不提供消息列表。新站点创建失败只回滚本次创建的任务/站点/应用池；保留文件方便检查，不覆盖已有目录。脚本语法已本机核验；真实服务器部署尚未执行。

默认测试数据密钥 `default` 对应截图中的自查配置，属于公开测试值，不证明发送者身份，只适用于隔离模拟收件箱。若自查工具显示其他数据推送密钥，把同一密钥写入服务器本地文件，使用 `-PushSecretFile C:\protected\push-key.txt`。**这是数据推送密钥，与 AppSecret 不同，不要复制 AppSecret。** 不要修改线上数据推送密钥来配合本测试工具。

域名解析和证书就绪后，在任意可联网机器执行：

```powershell
(Invoke-WebRequest -Method Head -Uri 'https://test.example.com/' -UseBasicParsing).StatusCode
```

必须返回 **200**。本包尚未证明这个测试域名已配置或可用。

## 自查工具填写

- roomID：`1000000000000000000`（公开文档的虚拟示例；按自己的自查工具填写，并与接收配置保持一致；不是 SDK 真实房间鉴权结果）。
- 角色：保留截图的 `GamePlayer`；角色 openID 为虚拟值即可。不需要真实主播 token。
- 协议：HTTPS。
- 测试数据推送域名：在页面已经有 `https://` 前缀的输入框中只填 `test.example.com`，不要填 `/live-api`、健康检查地址或重复协议。
- 数据密钥：保持页面现有 `default`，与本服务一致；若页面为其他值，使用上面的本地文件配置方式。
- 评论：可分别保存并推送 `加入`、`1`、`2`、`y`、`z`、`c`、`s`、`武器1`。
- 点赞：先测试数量 1，再测试 10。
- 礼物：添加仙女棒、能力药丸、魔法镜、甜甜圈、能量电池，数量先设 1；让平台自动填写价格/礼物 ID。
- 粉丝团：本服务可保存其推送，当前游戏未开通/实现对应玩法效果。
- 选队、赛事、进出房等其他 tab：这份服务只支持公开文档中已核验的四类 HTTP 数据，未核验的协议不伪造响应。

先选角色再点击推送。在服务器查看收件状态：

```powershell
& C:\Services\DouyinSelfTest\Inspect-Receiver.ps1
& C:\Services\DouyinSelfTest\Inspect-Receiver.ps1 -Recent
```

结果出现 `mock_only: true` 和消息计数增长，说明 HTTP 测试收件链路通过。`-Recent` 在服务器本地显示最近 20 条模拟消息的内容/数量，不提供公共查询接口，不输出推送密钥。默认最多 10000 条，每次请求最多 1 MiB，每条保存数据最多 16 KiB；满后返回 507，需在停服务后归档并指定新的独立数据库，不能删除真实后端数据。签名时间容差 5 分钟，需要服务器时间准确。

## 上传游戏包后的真实联调

先配置自己的 AppId、礼物映射和后端地址，再构建平台联调包；公开源码不附作者的成品包或服务配置。直播伴侣选择解压目录根 `GeometricWarfare.exe`。测试部署保留 `BACKEND_ONLINE_VERSION=false` / `BACKEND_ENABLE_UPLOADS=false`，用平台授权测试主播和测试观众验证评论、点赞、礼物、选队及整局结算。不要手工传启动 token，也不要选内层 UE exe。

当前实现正常执行官方审核测试礼物的当次效果，并隔离正式永久权益及个人/榜单统计。旧 `AllowPlatformTestGifts` 开关已移除，详见 [平台提供者说明](../../Plugins/DouyinLiveProvider/README.md)。这不表示此独立 HTTP 收件服务会转发礼物到 UE；它的模拟数据也不能当成真实付费记录。

当前没有将自查 HTTP 消息转发至游戏的模拟 provider，也没有证明自查工具支持当前 SDK 单推模式。需要模拟数据直接驱动游戏时，必须另做隔离测试适配并验证，不能仅填写域名就宣称完成。

官方来源：

- [openAPI 自测工具操作手册](https://developer.open-douyin.com/docs/resource/zh-CN/interaction/develop/test-tools/openAPI-tool)：测试域名隔离、HEAD 200、虚拟房间/角色和数据密钥。
- [数据开放说明](https://developer.open-douyin.com/docs/resource/zh-CN/interaction/develop/server/live-room-scope/data-open/data-open-desc)：HTTP headers、MD5/base64 签名、数组载荷和 2XX 收件响应。
- [SDK 指令直推能力](https://developer.open-douyin.com/docs/resource/zh-CN/interaction/develop/unity-sdk/live-unity-sdk-support/direct-push-ability)：SinglePush 与直播伴侣真实房间调试步骤。
