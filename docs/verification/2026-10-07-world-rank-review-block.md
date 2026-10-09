# 世界榜审核阻断诊断

2026-10-07用户提交1.1.15包的截图提示：未接入“上报世界榜单列表数据”和“上报用户世界榜单的累计战绩”API。

## 当前证据

- 官方用户战绩接入说明明确这两项需要分别上传：榜单接口展示排序后的Top150；累计战绩接口更新用户个人区域，每批最多50人。上传榜单不会代替个人累计战绩。
- 当前代码`Services/LivePlatformBackend/live_backend/policy.py`分别生成`upload_user_result`和`upload_rank_list`任务；`upstream.py`向官方HTTPS路径发送，使用服务端应用token，按`err_no=0`校验结果；`worker.py`只有成功后才记任务done。
- 本次实际公网GET `https://example.com/live-api/healthz`返回：`status=ready`，`uploads_enabled=false`，`outbox.pending=17`，`complete_time_unit=pending_verification`，`publication_state=completion_configuration_required`。
- 明确已知阻断是服务端关闭上传。还未读到远端数据库中的队列首项、平台拒绝码或按接口成功计数，不能宣称只改开关就已通过审核。
- 用户确认可在服务器PowerShell执行工具；本地任务无法直接修改远端服务器配置。

## 工具范围

`Scripts/WorldRankReviewKit`含默认只读诊断及显式`EnableUploads`配置修复模式。诊断使用SQLite mode=ro、query_only和一致性读事务；统计仅限当前app及测试/正式环境，输出操作计数、成功done计数和安全队首信息，不输出用户ID、成绩载荷、token、AppSecret或lease_token。

回环health请求禁用代理并拒绝跳转。开启上传只修改BACKEND_ENABLE_UPLOADS，保留其他配置，先创建同目录空备份并应用源ACL，再写原配置字节；源文件原地更新，UTF8 BOM保留。工具不发送平台请求、生成假成绩、改变测试/正式开关、自动重试失败任务或重启进程。

队首失败或队首截榜任务缺单位时拒绝开启；未到截榜任务时将缺单位标为未来截榜警告。当前官方文档与既有Go SDK契约未明确complete_time秒/毫秒，不猜测或绕过旧版本完成顺序。

## 验证

- Python诊断功能6项RED→GREEN；审查发现回环HTTP请求可跟随跳转，实际本地302复现后修复；最终7项通过。`Saved/WorldRank-Diagnostics-Final-Green-20261007.log`。
- Windows PowerShell5.1实际运行EnableUploads与Diagnose，验证只更改上传开关、原配置备份字节完全一致、源ACL复制、BOM及测试环境保留、输出无假密钥。`Saved/WorldRank-Kit-PS5-Smoke-20261007.log`。
- 没有修改UE游戏或服务端生产逻辑，没有开启远端上传，没有调用真实抖音战绩API。服务器执行诊断、配置启用与原方式重启后，需要分别验证两个接口的实际成功回执，再由用户在开发者平台重新检测。

## 官方来源

- https://developer.open-douyin.com/docs/resource/zh-CN/interaction/develop/server/live-room-scope/user-scores-rank/user-record
- https://developer.open-douyin.com/docs/resource/zh-CN/interaction/develop/server/live-room-scope/user-scores-rank/user-world-scores
- https://developer.open-douyin.com/docs/resource/zh-CN/interaction/develop/server/live-room-scope/user-scores-rank/upload-data
