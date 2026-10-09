世界榜 upstream_response_shape 单次响应诊断

适用本次现象：上传已开启，失败队首为 id=1 的 set_valid_version，错误 upstream_response_shape；累计战绩和榜单任务尚未执行。

在服务器解压到现有桌面 douyin 工具目录，执行：
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\WorldRank-ResponseProbe.ps1 -BackendDirectory "C:\Services\LivePlatformBackend"

请只运行一次，把输出JSON发回聊天。不要发.env、原始接口响应或任何token。

本工具不是只读网络检查：它会真实获取一次应用token；若当前服务器代码接受token响应，则重发同一条已失败队首的“设置测试世界榜版本”请求。使用数据库已保存的app/version/body，不生成新版本、不伪造观众或成绩、不调用两个成绩上传API，也不改任务状态。接口设置的是同一个版本。

只允许测试环境、当前应用、指定id且处于failed状态的set_valid_version队首；不满足条件则不会发请求。默认id=1，若后续排查其他任务需先确认，不要自行改ExpectedJobId反复运行。

输出只含：请求阶段、HTTP状态、有限的Content-Type、JSON是否有效、预先允许的字段名与类型、数字错误码，以及服务器upstream.py文件哈希。access_token、secret、用户ID、消息正文和未知字段名均不会输出。仍沿用安装版本的成功校验，不把异常响应算成功。

requests中只出现application_token：失败发生在获取应用token阶段。
出现application_token和set_valid_version：token已通过安装版本校验，需检查第二项返回结构。
json_valid=false且HTTP200：接口实际返回非JSON，需检查代理/网关或平台侧响应。
installed_backend_validation=accepted：本次探测通过了现有严格校验，但数据库任务仍是failed；后续按原因修复或正规重试，不代表16条成绩任务已恢复。

获取新token可能使之前的token有效期缩短。不要循环运行。本工具不会重启后端或IIS，也不会改变上传开关或测试/正式环境。

官方令牌文档：
https://developer.open-douyin.com/docs/resource/zh-CN/interaction/develop/server/interface-request-credential/get-access-token
官方设置榜单版本文档：
https://developer.open-douyin.com/docs/resource/zh-CN/interaction/develop/server/live-room-scope/user-scores-rank/world-list-version
