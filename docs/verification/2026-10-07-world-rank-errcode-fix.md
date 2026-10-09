# 世界榜成功响应 errcode 兼容修复

## 已确认根因

用户服务器脱敏探测给出：应用token响应HTTP200/JSON、err_no整数0、token字符串和expires_in正整数，通过原校验；set_valid_version响应HTTP200/JSON、errcode整数0、没有err_no，被原代码误判upstream_response_shape。服务器原upstream.py SHA256为`e96ecb3195eea95ed4e0b5405f9441b9f98d2be03b88f518467b801f94c780da`。

错误位于`check_errors(explicit=True)`只认可err_no，而其后通用错误码扫描已经知道errcode等官方SDK别名。修复为必须至少出现一个已知错误码，且所有出现的已知错误码都必须严格为整数0；不接受空对象、仅success消息、字符串/布尔/浮点码、冲突或任何非零错误。新源码SHA256 `9100d508f2ccf3ee513f426f3819c8b10e8292b35794a1b0e2203de6983f99ba`。

## 验证

- 新增2项测试在原实现真实失败；修复后完整后端40项通过，`Saved/WorldRank-Errcode-Full-Backend-20261007.log`。
- Worker+DouyinAPI合成真实返回形状验证：首任务failed时后序不发送；恢复后按set_valid_version→upload_user_result→upload_rank_list顺序done；原始累计积分和归档局数不变。
- 4项恢复工具保护测试通过：只恢复当前应用/测试环境确切的失败队首id1与指定错误，错误scope/id/code拒绝，WAL-aware备份保留原failed状态，原body/attempts/成绩不变，已存在备份不覆盖，再次运行不重复恢复。`Saved/WorldRank-Errcode-Recovery-Tests-20261007.log`。
- WindowsPowerShell5.1实际安装、源指纹/备份、禁止未重启进程恢复、正确Python新进程恢复、私有数据库备份ACL通过，`Saved/WorldRank-Errcode-PS5-Smoke-20261007.log`。测试只使用隔离数据和本机假监听器，没有真实平台请求。
- 独立复查发现PS5嵌入引号语法和运行环境身份检查问题，均修正并重测；复查无其他重要问题。

## 交付与实际边界

`output/WorldRankErrcodeFix_20261007.zip`，9345字节，SHA256 `66BF915F39BD7EFBD807194ABDC29F4B2999DB8831D49D168F2582ED0CA4294B`。

安装模式仅替换与用户报告原指纹完全一致的upstream.py，源备份先应用原ACL再写字节；不修改.env或数据库。用户按原方式重启后，恢复模式核对当前文件指纹、监听进程模块、选定解释器或其venv父进程、进程启动时间晚于补丁写入，创建含WAL的私有一致性数据库备份，再事务重验并仅将任务1重新设为pending。没有直接标done、重计成绩、删除后序任务或更改正式/测试开关。

补丁与恢复尚未在用户服务器执行。需要用户安装、重启、恢复后检查两个实际成绩API的confirmed_jobs及平台重新检测结果；本地测试不能代替真实回执。

## 来源

- 用户提供的服务器脱敏JSON和原文件SHA。
- 官方通用错误码兼容实现：https://github.com/bytedance/douyin-openapi-util-go/blob/main/client/client.go
