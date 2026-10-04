# Douyin Live Bridge

独立 UE 5.8 Runtime 插件，将直播观众事件与具体玩法解耦。

当前提供本地模拟评论、点赞、分享与五种规范化礼物事件，消息校验与有限窗口去重、HTTPS 头像加载和一个可选的自定义 WebSocket 适配传输。当前没有集成官方 LiveOpenSDK 二进制，也没有完成真实直播间联调。ADAPTER CONNECTED 只表示连上自定义适配服务，不代表抖音 SDK 已连接。当前实现与验证范围见 [礼物、永久武器与本地 GM 验证记录](../../docs/verification/2026-10-03-gifts-weapons.md)。

## 本地模拟

通过 GameInstance 获取 `UDouyinLiveSubsystem`：

| 调用 | 游戏线程委托 | 事件 |
| --- | --- | --- |
| `SimulateComment(UserId, Nickname, Content)` | `OnComment` | `FDouyinComment` |
| `SimulateLike(UserId, Nickname, Count = 1)` | `OnLike` | `FDouyinLike` |
| `SimulateShare(UserId, Nickname)` | `OnShare` | `FDouyinShare` |
| `SimulateGift(UserId, Nickname, GiftName, Count = 1)` | `OnGift` | `FDouyinGift` |

本地模拟为每次调用生成独立事件 ID，通过与适配服务相同的投递校验。处于 relay 模式时这四种模拟调用都返回 false。昵称不能作为平台唯一身份，正式消息使用稳定的用户标识。`FDouyinLike::Count` 表示本次新增加的点赞数，`FDouyinGift::Count` 表示本次新增加的礼物件数，两者都是 1–100 的整数；0、负数和超过上限的调用返回 false。分享的每条事件表示一次分享，不携带累计次数。

`DeliverComment`、`DeliverLike`、`DeliverShare`、`DeliverGift` 是游戏线程入口，调用方提供稳定且非空的 `MessageId`、`UserId`，可选 `Nickname`；礼物另需受支持的 `GiftName` 和有效增量 `Count`。插件广播 DTO，由玩法层决定恢复生命、复活、基地强化或授予武器等效果；插件本身不改变战斗状态或写入武器存档。接入官方 SDK 时，先确认授权 SDK 包是否具备可用于 UE 的原生接口、平台依赖及许可范围，再在插件内实现适配。不要以读取网页版直播数据代替官方接入。

## 可选适配服务

以下是本插件自定义协议，**不是官方 SDK 的网络协议**。仅在已有可信适配服务时使用。普通本地演示不需要启动服务，也不进行网络连接。

连接接受 wss:// 地址，或本机 ws://127.0.0.1:端口、ws://localhost:端口。当前不自动重连，断线后由调用方重新调用 ConnectRelay。远程鉴权由适配服务及后续原生 SDK 接入阶段落实，不应将开发用入口直接作为公共生产服务。

```json
{
  "room_id": "test-room",
  "type": "live_comment",
  "payload": [
    {
      "msg_id": "unique-event-1",
      "sec_openid": "stable-viewer-1",
      "nickname": "测试观众",
      "avatar_url": "https://example.com/avatar.png",
      "content": "加入"
    }
  ]
}
```

点赞与分享使用独立的同类型批次：

```json
{
  "room_id": "test-room",
  "type": "live_like",
  "payload": [
    { "msg_id": "unique-like-1", "sec_openid": "stable-viewer-1", "nickname": "测试观众", "count": 3 }
  ]
}
```

```json
{
  "room_id": "test-room",
  "type": "live_share",
  "payload": [
    { "msg_id": "unique-share-1", "sec_openid": "stable-viewer-1", "nickname": "测试观众" }
  ]
}
```

礼物使用 `live_gift` 同类型批次：

```json
{
  "room_id": "test-room",
  "type": "live_gift",
  "payload": [
    { "msg_id": "unique-gift-1", "sec_openid": "stable-viewer-1", "nickname": "测试观众", "gift_name": "仙女棒", "count": 1 },
    { "msg_id": "unique-gift-2", "sec_openid": "stable-viewer-1", "nickname": "测试观众", "gift_name": "能力药丸", "count": 2 },
    { "msg_id": "unique-gift-3", "sec_openid": "stable-viewer-1", "nickname": "测试观众", "gift_name": "魔法镜", "count": 1 },
    { "msg_id": "unique-gift-4", "sec_openid": "stable-viewer-1", "nickname": "测试观众", "gift_name": "甜甜圈", "count": 1 },
    { "msg_id": "unique-gift-5", "sec_openid": "stable-viewer-1", "nickname": "测试观众", "gift_name": "能量电池", "count": 1 }
  ]
}
```

`gift_name` 只接受上面五个完全匹配的名称，不接受未知名称、带空格的未规范化名称或评论指令。未来可信平台适配器负责将经授权来源的真实礼物 ID 映射为这些名称；当前 DTO 没有声称兼容官方礼物 ID、价格或原始网络字段。`msg_id` 是适配器提供的稳定事件 ID，不是礼物商品 ID；不得从观众评论推导已支付礼物。

点赞和礼物的 `count` 都必须是 JSON 数字、整数且在 1–100 之间；字符串 `"3"`、小数、0、负数和超出范围的值会拒绝整批，不截断、不取整。适配服务应先将上游数据规范化为**本事件新增数量**，不得直接填入累计点赞数或礼物连击累计件数；重试必须保留原事件 ID。这些字段仅是本地协议约定，不推测或声称匹配官方事件格式。

使用带 `Gifts` 参数的 `FDouyinEventDecoder::DecodeEvents` 重载，返回评论、点赞、分享、礼物四个数组；旧 `Decode` 继续只接受评论，旧三数组重载会拒绝包含礼物记录的批次，避免静默丢失礼物。每个信封最多 64K 字符、100 条同类型事件；身份最多 256 字符、事件 ID 128、昵称 64、评论 128、头像 URL 4096。用户与事件 ID 会去除首尾空格。整批消息先验证后投递，任一记录无效会清空所有输出，合法前缀也不投递；错误房间不投递。

评论、点赞、分享、礼物四类消息共用最近 4096 个事件的去重窗口，键由房间和事件 ID 组成；更换用户、礼物名称、数量或事件类型不能绕过同一房间同一事件 ID 的去重。无效事件不会占用 ID，不同房间可有各自同名事件 ID。窗口不是持久化履约账本，超出窗口的旧事件或进程重启后的事件不保证再次拦截。

头像只接受 HTTPS，压缩数据上限 2 MiB，尺寸上限 1024×1024；失败使用玩法的占位头像。

头像解码后先在 CPU 居中裁剪并缩为 64×64，之后才创建 GPU 纹理；不会先导入整张原图。URL 缓存保留 128 张用于复用，观众仍可分别持有缩略图。会话切换取消待处理下载，每个用户的请求票据阻止旧下载覆盖更新的缓存结果。

本版已提供规范化礼物的本地玩法接线，尚无官方礼物履约、榜单上报、SDK 身份验证或正式对局同步。后续抖音云服务与云端权益同步尚未实现，不能用模拟测试宣称已完成平台接入。

## 玩法层存档边界

当前 GameMode 将分享和武器礼物取得的解锁按稳定用户 ID 写入 `Saved/SaveGames/ArenaWeaponProgress_v1.sav`，本地与平台身份分开命名空间。保存失败时权益保留在内存并每 5 秒重试，不要求再次发送或消费礼物；只有成功保存后才保证跨重启保留。该文件保存武器权益，不保存插件的 4096 条事件去重窗口。

一次性步枪兑换码由本地 GM 生成，关注关系目前由主播人工确认；兑换码消费和步枪权益在同次存档中提交，写盘失败会回滚该次兑换。读取失败或版本不兼容时不覆盖原文件。测试可使用 `-GWProgressSlot=测试槽名` 隔离存档；后续云端实现需另行定义身份、权益合并和持久化履约，当前不提供云同步。

## 验证

`Tests/Test-InteractionRules.ps1` 使用本地 MSVC 独立验证点赞增量边界、非整数和非有限数值。UE 自动化测试 `GeometricWarfare.Bridge.InteractionDecode`、`InteractionDeduplicate` 验证评论 / 点赞 / 分享入口；`GiftDecode`、`GiftDelivery` 验证五种礼物、非法计数、批次原子性、跨类型及房间去重、错误房间隔离和 relay 模式禁用模拟。礼物测试通过测试访问设置 relay 状态，不建立网络连接。

玩法测试 `GeometricWarfare.Arena.SocialCombat`、`GiftCombat` 验证真实委托进入 GameMode 后的效果、武器权益与兑换流程。实际构建、通过数量和截图以 [2026-10-03 本批次验证记录](../../docs/verification/2026-10-03-gifts-weapons.md)为准；旧录像和较早测试计数仅代表对应历史版本。测试这些本地接口不等于验证官方 SDK、真实直播间或云端履约。
