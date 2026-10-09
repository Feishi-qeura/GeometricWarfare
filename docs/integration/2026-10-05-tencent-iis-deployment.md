# 腾讯云 Windows / IIS 联调部署

目标是用户已有的上海地域 Windows 服务器（2 核 4GB），现有域名 `https://example.com/`。采用 IIS HTTPS → 本机 `127.0.0.1:8765` Python 后端，SQLite 保存对局与待上传任务。代码已准备；尚未登录服务器，也没有修改现有站点。

## 先做只读核查

将部署包解压到站点目录之外，例如 `C:\Services\GeometricWarfareDeployment`。进入其中的 `Services\LivePlatformBackend`，运行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Preflight-IIS.ps1
```

脚本只核查 IIS、URL Rewrite 和 ARR 模块。将这个不含密钥的输出用于确认部署前置条件；现有站点绑定、证书、其他业务和剩余资源仍需检查。不要用示例文件覆盖当前 `web.config`。

## 准备后端进程

确认服务器可用 Python 3.12 后，在服务目录中运行：

```powershell
py -3.12 -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements.lock.txt
Copy-Item -LiteralPath .env.example -Destination .env
```

只在服务器本地编辑 `.env`。填入该玩法的 AppSecret，保留 `BACKEND_ONLINE_VERSION=false` 用于调试。`.env`、数据库和备份都不得放在 IIS 网站目录中；不要把密钥发到聊天、加入 Git 或拷进游戏包。初始化示例不会启用真实榜单上传。

先核对数据库目录权限，再启动：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Start-Backend.ps1
```

该进程固定监听回环地址，只启动一个上传 worker，并用本机文件锁防止同数据库重复启动。另一个 PowerShell 窗口可检查 `http://127.0.0.1:8765/healthz`。服务包装器或任务计划的开机启动、退出重启和运行账户，需要结合服务器现有运维方式配置；当前脚本不会安装服务或重启服务器。

## 接到现有 HTTPS 站点

核对 URL Rewrite / ARR 已安装并允许反向代理后，将 `iis-live-api-merge.fragment.xml` 中的专用规则合并进现有站点配置。只将 HTTPS `/live-api/...` 转发到后端，设置可信 `X-Forwarded-Proto=https`；该路径请求上限为 4 MiB。启用 ARR 和允许修改服务器变量涉及 IIS 配置，必须先检查现有规则和备份。

预期健康检查地址为 `https://example.com/live-api/healthz`。此地址目前只是部署目标，尚未证明已存在。确认实际 TLS、路由和服务身份后，再将游戏配置 `[DouyinLiveProvider] BackendUrl` 设为 `https://example.com/live-api`；客户端自动追加版本化接口路径。

IIS 官方依据：[URL Rewrite 与 ARR 反向代理](https://learn.microsoft.com/en-us/iis/extensions/url-rewrite-module/reverse-proxy-with-url-rewrite-v2-and-application-request-routing)、[请求体限额以字节计](https://learn.microsoft.com/en-us/iis/configuration/system.webServer/security/requestFiltering/requestLimits/)。

## 真正开通总榜联调

- 在控制台启用并安全配置服务端 AppSecret；确认直播信息和世界榜接口权限。
- 使用平台授权测试主播和调试成员，由直播伴侣启动游戏；不要手填真实启动令牌。
- 验证服务端使用官方房间接口核对 room_id / anchor，再发放自有会话；会话只存摘要。
- 确认调试环境后开启 `BACKEND_ENABLE_UPLOADS=true`，观察实际平台错误码与榜单显示。
- `BACKEND_COMPLETE_TIME_UNIT` 先留空。公开总榜接口只说明 Int64，秒/毫秒仍待平台核验。未配置时可保存新周成绩，但旧周完成与后续版本发布保持等待。

每周日香港时间 23:00 重置。跨周补交按用户确认的**首次成功接收入库周次**累计，重复提交仍返回原局所属周次，不重复加分。服务器独立累计所有有效红蓝成绩、胜场和连胜，灰队不计总榜；一次本地接收成功不代表平台已经接受全部榜单上传。

## 调试环境与正式环境切换

当前客户端结算 outbox 按 platform/app/room 隔离，不会自动辨识服务器测试/正式开关。切换 `BACKEND_ONLINE_VERSION` 或后端地址前，必须停止旧游戏和宿主，备份并隔离测试存档及 `Saved/LivePlatform/RoundOutbox`；禁止把测试待提交记录带进正式环境。UE 可用独立的 `-UserDir=<绝对目录>` 区分测试与正式用户目录（已核对 UE 5.8 `FPaths::CustomUserDirArgument` / `ProjectUserDir` 源码）。正式启动方式应确认这个目录隔离已实际生效。

## 备份与故障检查

示例数据库路径需与服务器 `.env` 保持一致；这些命令只输出状态，不输出令牌或原始对局载荷：

```powershell
.\.venv\Scripts\python.exe -m live_backend.operator --database C:\ProgramData\GeometricWarfare\live-backend.sqlite3 status
.\.venv\Scripts\python.exe -m live_backend.operator --database C:\ProgramData\GeometricWarfare\live-backend.sqlite3 backup C:\Backups\GeometricWarfare\live-backend-initial.sqlite3
```

备份目标必须是新文件。工具使用 SQLite 在线备份 API，包含 WAL 中的数据。先在隔离环境验证恢复，再制定保留周期。不要直接复制正在运行的单个 `.sqlite3` 文件当作完整备份。

当前单实例以完整累计快照更新榜单，5000 名累计用户需 100 批用户数据和一次 Top150；上传积压和服务器容量要在实际机器上测量。本地基准不能代替上海服务器的负载验收。
