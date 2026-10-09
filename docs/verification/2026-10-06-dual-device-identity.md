# 同账号手机与 PC 角色重复：诊断记录

用户确认：手机与 PC 登录同一个抖音账号，两个“绯世”均上积分榜，并由两端指令分别控制。
这属于同账号被拆成两个参赛身份的异常，不是单纯重名或主播助战显示。

## 已核验的代码链路

- SDK 宿主评论事件取 `ICommentMessage.Sender.OpenId`，写入事件的 `user.open_id`。
- UE 提供者直接把该字段传给 `FLiveComment.UserId`，没有拼接设备标识。
- LiveInteraction 去除 ID 两端空白，游戏以此为 `Viewers` 的键。
- 已存在的 ID 重复评论“加入”只定位已有角色；进房消息本身不创建角色。
- 既有 ViewerLifecycle、AdvancedLive 回归通过，覆盖重复加入、重复选队和进房不自动创建。
- 2026-10-05 20:59–21:04 的本机实播诊断仅保存类型/计数，缺少身份信息，不能据此判定两端实际 OpenId。

## 本次修改

UE 提供者收到评论、选队、进房、关注、礼物事件时，额外保存 `identity_event`。
字段包含会话内的用户、昵称、消息指纹，以及动作分类和 ID 空白归一化标志。
原始 ID、昵称、评论文本、头像 URL 和指纹盐不写入诊断记录。
每次认证房间切换重新生成盐；指纹只用于同一会话内的相关性检查，不用于玩家合并。
点赞不产生此类记录，沿用诊断文件 1 MiB 上限。
没有修改积分、阵营、武器权益或身份合并逻辑，尚未修复双端角色重复。

编译时额外遇到原音频模块全局 `Names` 与游戏局部 `Names` 的合并编译 C4459 冲突，
仅将音频资源名数组改名为 `AudioAssetNames`，没有改变音频行为。

## 验证

- Editor Development 编译通过。
- `DouyinHostContract`、`ViewerLifecycle`、`AdvancedLive` 三项 UE 回归通过。
- 新诊断验证原始信息不泄漏、同名不同 ID 指纹不同、空白 ID 一致、换会话指纹变化、未知类型与点赞不记录。
- 日志：`Saved/IdentityDiagnostics-Green-20261005.log`。
- Shipping 编译/打包通过；包内 Shipping EXE 与本次构建的哈希一致，确认包含 `identity_event` 诊断代码。
- 根启动器实际启动通过：无凭证时子游戏保持运行，并记录 `credential_missing`；此次测试进程已关闭。
- ZIP 207 项、125340897 bytes，不含 PDB、envs.log、JSONL、SQLite 或 .env。
- 包路径：`output/GeomeWar_1.1.8-identity-diagnostics-20261006.zip`。
- SHA256：`00015593E9480DDEF0BBE9C44D7A8156EF65B8CB4D850BD5AD6D667F87801AC2`。
- 对照脚本用两条同名不同 ID 的虚构指纹记录验证，正确输出两个身份；该夹具不能证明实播来源。

## 诊断复现步骤

1. 使用身份诊断包通过直播伴侣启动；手机和 PC 登录同一个账号进入同一直播间。
2. 同一局内先手机发“加入”，间隔约 5 秒后 PC 发“加入”，再两端依次发数字指令。记下顺序即可。
3. 结束后读取 `%LOCALAPPDATA%/GeometricWarfare/Saved/LivePlatform/provider-diagnostics-*.jsonl`。
   `Scripts/Read-LiveIdentityDiagnostics.ps1` 可输出最新日志的指纹对照；不要发送 SDK `envs.log` 或启动凭证。

若同一显示名在这两次输入中对应不同 `user_fingerprint`，说明进入 UE 时用户键已经分裂；
需据平台支持的同账号映射解决，不能用昵称/头像推断身份。
若两次用户指纹一致仍有两个参赛角色，应继续核验当前包、游戏创建路径和角色 ID。
上述步骤为原诊断建议；下面记录用户实际执行的 PC 先、手机后的顺序。

## 2026-10-06 实播核验结果

用户报告安装目录：`C:/Users/zxxuh/AppData/Roaming/webcast_mate/miniGame/UnityInstall/GeomeWar_1.1.9`。
实际 Shipping EXE 与本次诊断包 EXE 的 SHA256 完全相同：
`CACB0739C05D51D639841A20399D9260AB550D2D7D4F7B1D1DDC3BB3006DE94A`。

读取日志：
`C:/Users/zxxuh/AppData/Local/GeometricWarfare/Saved/LivePlatform/provider-diagnostics-5943FBD04DE7700107496D8A2D9E0435.jsonl`。
日志只有一次 `room_ready`，下面的指纹来自同一认证会话。

| 香港时间 UTC+8 | 用户描述的来源 | SDK 事件 | 动作分类 | 用户指纹前缀 |
|---|---|---|---|---|
| 2026-10-06 01:44:01.928 | PC | live_comment | join | 8107991D |
| 2026-10-06 01:44:08.264 | PC | live_comment | number | 8107991D |
| 2026-10-06 01:44:28.832 | 手机 | live_enter | event | 98348910 |
| 2026-10-06 01:44:30.331 | 手机 | live_team | event | 98348910 |
| 2026-10-06 01:45:32.040 | 手机 | live_comment | weapon | 98348910 |

两组昵称指纹相同：`DED413CE0EA5A9891896FECF6CE36BEF55B33768`。
PC 用户指纹为 `8107991DA84EF50522870937AC35B800D66E10DB`；
手机用户指纹为 `983489103CD36B08F414E54E44E2D4856CBCCCFF`。
两端原标识长度均为 36，`id_whitespace_normalized` 均为 false。
手机评论事件也使用手机进房/选队事件的指纹，因而不是仅两类消息来源字段造成的错配。

用户明确报告 PC 选红、手机选蓝；日志没有保存数字正文或 group_id，不能声称该颜色由指纹日志独立解码。
这五条消息指纹各不相同；不是重复投递同一条消息。

### 结论与边界

- **已确认**：同一账号的两端测试在 SDK 到 UE 的事件边界提供了两个不同的用户键。
  当前代码按该键创建角色，因此产生两个分别可控制、可计分的参赛身份。
- **已排除本次常见解释**：旧包未含诊断、单纯进房自动建角、重复消息 ID、ID 两端空白，以及只有显示重名而没有两个参赛身份。
- **尚未确认**：平台为什么提供两个不同标识，是否是客户端来源 AID 的差异、标识表示差异或其他平台问题。
- **尚未修复**：没有按昵称或头像合并玩家。需要平台认可的统一标识/映射，或另行设计可验证的跨端绑定流程。

官方 [FAQ 8.3](https://developer.open-douyin.com/docs/resource/zh-CN/interaction/develop/server/live-room-scope/access-faq)
解释 OpenId 与 uid、appid、aid 对应，并举了伴侣登录与抖音用户信息接口返回不同 OpenId 的例子。
该例子不是 PC 观众 vs 手机观众的直接说明，不能将本次 AID 原因当成已获官方确认。
本地取得的 LiveOpenSDK 2.7.12 `IUserInfo` 公开字段为 OpenId、AvatarUrl、Nickname；
本次文档核查没有找到已确认适用于直播指令用户的跨端映射接口，不能扩大为平台绝对不存在该能力。
