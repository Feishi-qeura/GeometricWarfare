世界榜审核阻断 · 服务器诊断与上传开关工具

当前已核实：2026-10-07，https://example.com/live-api/healthz 返回 uploads_enabled=false，pending=17。现有服务端已有 upload_user_result 与 upload_rank_list 实现，但关闭了上传。

1. 先在服务器解压本工具，进入解压目录，在 PowerShell 执行：
   powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\WorldRank-Review.ps1
   按提示输入实际 LivePlatformBackend 目录（包含 .venv 与 Start-Backend.ps1）。
   若 .env 单独存放，改用：
   powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\WorldRank-Review.ps1 -BackendDirectory "你的后端目录" -EnvironmentFile "实际.env路径"
   默认只读，不改数据库、任务、配置或分数，也不发送任何平台API。请把输出的JSON文本发回当前聊天；不要发送.env、原始数据库或密钥。
   默认后端回环端口8765；若服务器配置了其他端口，可传 -Port 实际端口。若后端使用其他Python解释器，可传 -PythonExecutable "解释器完整路径"，默认使用后端.venv。

2. 若诊断只显示 uploads_disabled，credentials_configured=true，且没有 head_job_failed / completion_time_unit_required，可在同一目录显式开启上传：
   powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\WorldRank-Review.ps1 -Mode EnableUploads
   若第一步使用了自定义 BackendDirectory / EnvironmentFile，第二步也传相同参数。
   本操作只把BACKEND_ENABLE_UPLOADS改为true，备份保留原配置的访问权限，其他配置保持原值。不会切换测试/正式环境，不会删除失败任务，不会修改已有成绩。

3. 用服务器原有方式重启 LivePlatformBackend 后端进程/服务，重新运行第一步。
   不需要重启IIS或整台服务器。配置文件改成true而运行中实例还是false时，报告会标注runtime_restart_required；改文件本身不会自动重载进程。

4. 核验平台接收：
   review_apis.upload_user_result.confirmed_jobs 与 upload_rank_list.confirmed_jobs 分别代表当前应用/当前测试或正式环境下，现有worker记录为done的成功任务数。该worker只有在官方响应err_no=0通过校验后才记done。
   这两个计数都应增长。pending任务逐步下降，失败则head_job.error显示platform_数字或安全错误码。记录来自现有真实已归档数据，工具不生成测试观众、假成绩或空榜来刷检测。
   若 cumulative_user_records=0 或没有用户上报任务，需要真实调试观众入场、选择红/蓝阵营并完成一局，由已认证的SDK结算链路接收成绩。
   确认后在开发者平台点击“重新检测”；平台最终检查结果不能由本地工具代替。

5. completion_time_unit未配置：
   当前官方文档与已核验SDK只明确complete_time是Int64“上传完成时间”，没有明确秒/毫秒，工具不会猜。
   若队首已经是complete_upload_user_result，则会阻止自动开启上传，需先获得平台对单位的确认；不能跳过旧版本完成任务或改队列顺序。
   若尚未到截榜任务，普通累计战绩与Top150上传可执行，仍须在周日23:00截榜前核实并配置BACKEND_COMPLETE_TIME_UNIT为seconds或milliseconds。

官方说明：
https://developer.open-douyin.com/docs/resource/zh-CN/interaction/develop/server/live-room-scope/user-scores-rank/user-record
https://developer.open-douyin.com/docs/resource/zh-CN/interaction/develop/server/live-room-scope/user-scores-rank/user-world-scores
两个接口数据区域不同，累计战绩每批最多50人，榜单列表按排序一次上传Top150，上传列表不会自动补齐个人战绩。

这是服务端开关与上传链路问题，当前无需重做1.1.15游戏包。本工具不含AppSecret、不安装依赖、不改IIS、不重启服务，Python使用服务器现有.venv。
