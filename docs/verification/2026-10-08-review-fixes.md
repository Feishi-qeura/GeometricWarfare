# 1.1.16 提审问题修复

用户确认失败版本为 1.1.15，反馈为召集贴纸缺失、礼物无效果和外显规避词。旧版本压缩包保留。

## 修复及依据

- 主界面内置常驻“一键摇人 · 召集好友”卡片，说明“小摇杆 → 召集 → 发起召集”，覆盖桌面、横屏及竖屏，不依赖 `-GWRuleSticker`。已有 SDK 召集关系字段补充明确反馈，普通看播/分享不冒充召集，不新增付费诱导或奖励承诺。
- 1.1.15 provider 在 `IsTestData=true && AllowPlatformTestGifts=false` 时提前丢弃礼物。[官方数据开放说明](https://developer.open-douyin.com/docs/resource/zh-CN/interaction/develop/server/live-room-scope/data-open/data-open-desc)明确此类数据包含版本审核互动测试，要求统计过滤。1.1.16 保留测试标记、执行原礼物效果、渲染后按原消息 ID/type 履约，测试武器仅保留当前会话，禁止写成正式付费权益。
- 测试效果会影响对手和后续回合，因此收到审核测试礼物的整个会话不提交个人战绩、房间榜或世界榜，仍按合法流程关闭对局。重新连接的会话会清空战局及临时权益。已冻结的真实结算快照不会被后来测试礼物改写。
- 五种礼物映射与[官方礼物表](https://developer.open-douyin.com/docs/resource/zh-CN/interaction/jierushuoming/hudongshuju/liwushuju)一致，未猜测或替换加密 ID。
- 按[直播玩法准入规则](https://developer.open-douyin.com/docs/resource/zh-CN/interaction/introduction/live-play/creation)清理外显：玩法设置、能量条、观众、连续击败、强化；内部技术标识不改。

## 规范读取边界

用户提供的一键摇人要求（私有文档链接已省略）通过内置浏览器读到了正文与功能动线：2026-04-01 后更新必须接入并展示贴纸，普通分享无法取得召集关系，禁止付费诱导，普通看播不能代替落座。页面“必接改造/可选改造”嵌入表持续加载失败，不能声称已核对表内不可见细节。

没有本次提审的原始事件日志，故不能断言所有真实付费事件都曾进入该测试分支。该分支是已确认代码缺陷，并与审核截图症状吻合。

## 验证记录

- UE5.8 Editor 编译成功；最终完整自动化 `GeometricWarfare`：37 成功、0 警告、0 失败、0 未运行。报告：`Saved/Review-Final-All-20261008/index.json`。覆盖生产礼物解码器、五种礼物的真实/测试标记、药丸 66+10、电池 1、去重、武器存档隔离、测试统计隔离、召集反馈及大量通知计数。
- 截图复核：PC 1920×1080、设置面板、手机 390×844、移动横屏 1280×720、近方屏 1024×1280、三种武器通知、两种基地通知，以及 854×480 / 960×540 结算。召集卡片可见，设置为“玩法设置 / 能量条”。小横屏结算文字原有遮挡已修正。截图位于 `Saved/Screenshots/Review-*.png`；礼物画面采用本地自动化数据，不能代替平台赠送验证。
- Shipping BuildCookRun 成功，AutomationTool ExitCode=0；版本为 1.1.16、GM 关闭。日志：`Saved/Review-Shipping-Package-20261008.log`。
- 最终交付：`output/GeomeWar_1.1.16.zip`，125,390,664 字节，单一版本目录。205 个 ZIP 文件逐项 SHA256 与发布目录一致；原生程序与本轮 Shipping 构建一致，根启动器与本轮启动器构建一致，包含 SDK 宿主；无调试符号、存档、环境凭证文件或运行日志。元数据：`output/GeomeWar_1.1.16-verification.json`。
- ZIP SHA256：`6B80464284C7B689EB41579B5EC914988F1E31A3B0D87BFAECFC1F8CE35C1BE5`。
- 由正式包根目录启动器进行原生 Spout 验证：240/240 新帧，1920×1080，错误尺寸 0，像素 alpha 全为 255。等待平台状态下召集卡片可见，指定 GM 预览参数也未出现调试面板。接收日志：`Saved/Review-Package-Spout-Receiver.log`；实际画面：`tmp/spout-research/package-1.1.16-final.bmp`。本次只关闭测试启动的进程。

## 交付后现场复核

解压 1.1.16，直播伴侣选择包根目录 `GeometricWarfare.exe`，保留完整目录。用平台自测/审核礼物逐项验证五种礼物，重点药丸 66/10 和电池 1；查看画面效果和平台履约。召集需要实际观众通过小摇杆召集链接进入验证。平台控制台的能力开通、真实推送与审核通过均不能由本地自动化替代。
