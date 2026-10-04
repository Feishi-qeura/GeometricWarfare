# 英雄连发剑气与结算荣誉

## 本轮规则

- 步枪弹夹从 30 提高至 45，适用于主播固定步枪及观众已解锁步枪。伤害、换弹时间与其它参数保持原规则。
- BOSS 初始及最大生命从 16000 提高至 32000；40% 暴怒线为 12800，暴怒减伤 45% 与既有攻击保持。
- 英雄每 5 秒开始一轮六剑，首剑立即发射，余剑每隔 0.2 秒发射，一轮在 1 秒内完成。每轮锁定起始枪口方向，各道从玩家当前位置按原 ±0.2 个几何体宽度偏移发出；速度从 440 提高至 880，距离仍为 1100，单剑飞行时间为 1.25 秒，保留末段淡出与穿透。普通进化剑气仍为 440 速度、2.5 秒。死亡取消待发余剑，复活重新计时，下局清空。
- 结算保留 MVP/FMVP，新增“击杀最多”“承伤最多”两张荣誉卡，展示头像、昵称、阵营与本局数值，错峰淡入并轻微上移。红蓝灰观众均参与这两个新奖项，主播不参评；零有效值不颁奖。同值先比较积分，再按较小玩家 ID 稳定排序。
- 承伤为形状减伤后实际生命损失与护甲吸收伤害之和。护甲吸收按照真实吸收量计算，非护甲消耗的 85% 成本；不含减伤抵消、生命过量伤害或治疗。所有普通武器、接触、剑气与 BOSS 环境伤害共用该累计入口。死亡与复活不清除本局累计，下局重置。

## 验证结果

原生测试已通过：RoundUpgrade 107、Weapons 1778、Boss 584、Expansion 12930、SupplyCombat 36、Awards 39、Match 30211 项断言，零失败。新增测试覆盖 0.2 秒边界、分帧一致性、锁定朝向、不同出生时刻独立寿命、每轮 5 秒、死亡/复活/换局取消，以及承伤减免、护甲不足、过量伤害、灰色参评、空奖与平局排序。Awards 红绿证据和完整对局回归保存在 `Saved/AwardsV4-Red.log`、`Saved/AwardsV4-Green.log`、`Saved/AwardsV4-Match.log`。

主工程 Editor 构建和 DLL 链接成功（19.77 秒），Game 构建成功（30.96 秒），日志为 `Saved/HeroAwardsV4-EditorBuild.log`、`Saved/HeroAwardsV4-GameBuild.log`。

使用主工程最新 DLL 执行 `GeometricWarfare.` UE 自动化：12 成功、0 失败、0 警告、0 未运行。报告为 `Saved/HeroAwardsV4Automation/index.json`，日志为 `Saved/HeroAwardsV4Automation.log`。

已逐张查看主工程离屏截图：

- [1080p 四项结算荣誉](../../Saved/Screenshots/HeroAwardsV4-Results1080.png)
- [720p 紧凑结算](../../Saved/Screenshots/HeroAwardsV4-Results720.png)
- [结算荣誉入场过程](../../Saved/Screenshots/HeroAwardsV4-ResultsEnter.png)
- [英雄同向逐道连发](../../Saved/Screenshots/HeroAwardsV4-HeroBurst.png)
- [32000 生命 BOSS 与补给](../../Saved/Screenshots/HeroAwardsV4-Boss32000.png)

截图使用可重复的本地模拟场景，结算通过实际伤害、击杀及正常结算路径生成，灰色观众 12 杀、蓝色观众最高承伤；并非真实平台实战。画面布局、独立荣誉卡、逐剑间距及新 BOSS 血条均可见；发射时序和每剑寿命由规则测试验证。本轮未验证真实抖音接入、云存档或持续满员直播性能。

## 提案同步

本轮同步填写用户指定的本地提案，沿用模板栏目和表格，更新七分钟对局、最新武器、补给、英雄、BOSS、礼物和结算规则，并放入新实机截图。开发者姓名依据用户提供的飞书文档正文填写“徐鸿超”。开放平台主体、联系方式、appId、正式礼物配置和资质按实际证据保留待补充或待核验；飞书页眉组织名称不自动当作已确认的开放平台主体。

DOCX 已做结构与内容核对。本机未找到 LibreOffice/Word/WPS 渲染器，未完成原生 Word 分页视觉验证；不将图片和 XML 检查当作分页验证。

成品已写回 `C:/Users/zxxuh/Downloads/【提案评估】玩法名称xx.docx`，修改前备份为同目录 `【提案评估】玩法名称xx_更新前备份_20261004.docx`；工作区另存 `output/docx/【提案评估】几何战争_FEISHI_20261004.docx`。复制前校验原文件未被另行修改，备份与原文件哈希一致，写回后成品与工作区副本哈希一致（SHA256 `d067dbd5567f2eaa667efeb2fc701dfe8be432f8b081cd3282855819f40f149d`）。最终含 11 张表、15 条互动指令、5 张内嵌图片；模板原有 19 个关系保留。ZIP/XML、文档重开与最新关键规则检查通过。完整过程记录在 `tmp/docx-20261004/validation.json` 与 `final-render.log`。
