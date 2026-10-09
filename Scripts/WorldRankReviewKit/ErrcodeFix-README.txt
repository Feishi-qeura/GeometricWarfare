世界榜 errcode 成功响应兼容补丁 · 2026-10-07

真实根因：应用token返回err_no整数0且结构正确；set_valid_version返回HTTP200/JSON/errcode整数0，没有err_no。旧代码只把err_no视为明确成功，误判upstream_response_shape并锁住后续16条任务。

请把本压缩包全部解压到服务器桌面原来的douyin工具目录，不要只复制PS1。无需替换游戏包或修改.env。

第一步，安装补丁：
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\WorldRank-ApplyFix.ps1 -BackendDirectory "C:\Services\LivePlatformBackend"

脚本核对当前upstream.py与本次服务器报告的SHA完全一致后，仅替换该文件并保留原文件私有备份。若文件已是正确补丁则不重复替换；若指纹不符则拒绝覆盖。

第二步，按服务器原有管理方式重启LivePlatformBackend后端进程/服务。不需要重启IIS或整台服务器。

第三步，仅恢复本次失败任务1：
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\WorldRank-ApplyFix.ps1 -BackendDirectory "C:\Services\LivePlatformBackend" -Mode RetryFailed

脚本核对补丁指纹、回环监听进程属于所选Python且在补丁写入后启动，确认已重启。随后读取当前应用/测试环境中id1的failed set_valid_version及upstream_response_shape，备份数据库（SQLite backup含WAL、备份使用原数据库ACL），只将任务1恢复pending。不会删除队列、重置16条任务、改成绩、改attempts历史或切换正式环境。

如果没有重启就运行第三步，会报告“Backend process predates the installed patch”；请重启后再执行。若后端端口或解释器非默认，可传 -Port 实际端口、-PythonExecutable "解释器完整路径"；.env独立存放时传 -EnvironmentFile "实际路径"。

第四步，等待30秒，使用原来的工具检查：
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\WorldRank-Review.ps1 -BackendDirectory "C:\Services\LivePlatformBackend"

当前原有队列目标：set_valid_version成功，upload_user_result与upload_rank_list的confirmed_jobs分别增长到8（期间新对局可能产生更多），无failed。确认后再到开发者平台点“重新检测”。

补丁仍拒绝空对象、只有success消息、字符串/布尔/浮点错误码，以及任何非零或冲突的错误码；不会将未知返回算成功。APP token、密钥、会话、累计数据和测试/正式开关不变。

本地验证：原缺陷2项RED后修复；完整后端40项通过，含真实Worker+DouyinAPI模拟errcode响应的按序恢复且不重复计分；4项恢复保护测试通过；WindowsPowerShell5.1实际安装/重启检查/恢复/ACL验证通过。未在用户服务器执行补丁，尚需真实成功回执。

旧源码SHA256：e96ecb3195eea95ed4e0b5405f9441b9f98d2be03b88f518467b801f94c780da
新源码SHA256：9100d508f2ccf3ee513f426f3819c8b10e8292b35794a1b0e2203de6983f99ba

未来截榜的complete_time单位仍需核实。这不是当前队列的阻断，补丁不会猜测单位或跳过截榜任务。
