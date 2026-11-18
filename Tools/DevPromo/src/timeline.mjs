// Master timeline for Geometric Warfare's new promotional video.
// Locked to the game's native 112 BPM soundtrack grid (1 beat = 60/112 s, 1 bar = 4 beats = 2.142857 s).
export const FPS = 30;
export const BPM = 112;
export const BEAT = 60 / BPM;
export const BAR = 4 * BEAT;
export const END = 38 * BAR; // 81.4286 s (2443 frames)

export const bar = n => n * BAR;
export const beat = n => n * BEAT;

// Act boundaries (in seconds, aligned to bars/beats)
export const ACT = {
  intro: 0,               // Bars 0-2   (0.00 - 4.29s)
  tdCard: bar(2),         // Bar 2      (4.29 - 5.36s) -> 2 beats chapter card
  td: bar(2.5),           // Bar 2.5    (5.36 - 21.43s) -> 7.5 bars TD showcase
  coopCard: bar(10),      // Bar 10     (21.43 - 22.50s) -> 2 beats chapter card
  coop: bar(10.5),        // Bar 10.5   (22.50 - 34.29s) -> 5.5 bars Co-op showcase
  systems: bar(16),       // Bars 16-24 (34.29 - 51.43s) -> 8 bars (exact Results.wav length)
  list: bar(24),          // Bars 24-34 (51.43 - 72.86s) -> 10 bars Shopping List
  outro: bar(34),         // Bars 34-38 (72.86 - 81.43s) -> 4 bars Outro
};

// Gameplay footage clips
export const CLIPS = [
  // Tower Defense (5.36s - 21.43s)
  {
    id: 'td_build',
    shot: 'td_build',
    at: bar(2.5),
    len: bar(2),
    from: 0,
    speed: 1.48,
    zoom: [1.0, 1.03],
    badge: { text: '塔防玩法', status: '已完成', accent: 'Teal', corner: 'tl-offset' },
    callout: {
      pos: 'br',
      tag: '迷宫布防',
      title: '方格纸画墙造迷宫',
      sub: '右键拖出纸箱防线 · 圆盘建造四大防御塔与梯子',
      accent: 'Teal',
      icon: 'maze',
    },
  },
  {
    id: 'td_wave',
    shot: 'td_wave',
    at: bar(4.5),
    len: bar(2),
    from: 24,
    speed: 1.0,
    zoom: [1.0, 1.04],
    badge: { text: '塔防玩法', status: '已完成', accent: 'Teal', corner: 'tl' },
    callout: {
      pos: 'bl',
      tag: '立体守线',
      title: '炮塔 + 枪械并肩御敌',
      sub: '敌人自动寻路绕迷宫 · 弩炮 / 迫击炮 / 电塔交叉火力',
      accent: 'Coral',
      icon: 'bolt',
    },
  },
  {
    id: 'td_rocket',
    shot: 'td_rocket',
    at: bar(6.5),
    len: bar(1),
    from: 8,
    speed: 1.0,
    zoom: [1.02, 1.13],
    focus: [880, 440],
    badge: { text: '塔防玩法', status: '已完成', accent: 'Teal', corner: 'tl' },
    callout: {
      pos: 'bl',
      tag: '高抛爆破',
      title: '蓄力火箭筒越墙轰炸',
      sub: '蓄力越久抛得越远 · 专治成群精英（小心别炸队友）',
      accent: 'Fire',
      icon: 'flame',
    },
  },
  {
    id: 'td_boss',
    shot: 'td_boss',
    at: bar(7.5),
    len: bar(1.5),
    from: 62,
    speed: 1.0,
    zoom: [1.0, 1.05],
    badge: { text: '塔防玩法', status: '已完成', accent: 'Teal', corner: 'tl' },
    callout: {
      pos: 'bl',
      tag: 'Boss 突袭',
      title: '巨型 Boss 激光切开防线',
      sub: '纸墙会被光束与冲撞摧毁 · 随时补防逆转战局',
      accent: 'Plum',
      icon: 'trapezoid',
    },
  },
  {
    id: 'td_victory',
    shot: 'td_victory',
    at: bar(9),
    len: bar(1),
    from: 4,
    speed: 1.0,
    zoom: [1.0, 1.01],
    badge: { text: '塔防玩法', status: '已完成', accent: 'Teal', corner: 'tl' },
    callout: {
      pos: 'bl',
      tag: '三星目标',
      title: '守住核心 · 盖章通关',
      sub: '赢取贴纸奖励，解锁更多笔记章节',
      accent: 'Marigold',
      icon: 'star',
    },
  },

  // Online Co-op (22.50s - 34.29s)
  {
    id: 'coop_hub',
    shot: 'coop_hub',
    at: bar(10.5),
    len: bar(1.75),
    from: 0,
    speed: 1.0,
    zoom: [1.01, 1.05],
    badge: { text: '多人联机', status: '1-8人 · 已完成', accent: 'Cobalt', corner: 'tr' },
    callout: {
      pos: 'br',
      tag: '好友开黑',
      title: '大厅就是房间 · 最多 8 人',
      sub: 'Steam / Epic 好友一键加入 · 靶场练枪整活两不误',
      accent: 'Cobalt',
      icon: 'hex',
    },
  },
  {
    id: 'coop_throw',
    shot: 'coop_throw',
    at: bar(12.25),
    len: bar(1.75),
    from: 30,
    speed: 1.0,
    zoom: [1.0, 1.04],
    badge: { text: '多人联机', status: '1-8人 · 已完成', accent: 'Cobalt', corner: 'tl' },
    callout: {
      pos: 'bl',
      tag: '友谊小船',
      title: '抓起队友扔进怪堆当诱饵！',
      sub: '落地砸翻一片、吸引火力 · 整活强度：关闭 / 友好 / 混乱',
      accent: 'Magenta',
      icon: 'throw',
    },
  },
  {
    id: 'coop_defend',
    shot: 'coop_defend',
    at: bar(14),
    len: bar(1),
    from: 45,
    speed: 1.0,
    zoom: [1.0, 1.03],
    badge: { text: '多人联机', status: '1-8人 · 已完成', accent: 'Cobalt', corner: 'tl' },
    callout: {
      pos: 'bl',
      tag: '协同御敌',
      title: '八人同守一个纸盒',
      sub: '挂梯子上下支援 · 倒地互救 · 防线绝不失守',
      accent: 'Teal',
      icon: 'shield',
    },
  },
  {
    id: 'coop_results',
    shot: 'coop_results',
    at: bar(15),
    len: bar(1),
    from: 0,
    speed: 1.0,
    zoom: [1.0, 1.01],
    badge: { text: '多人联机', status: '1-8人 · 已完成', accent: 'Cobalt', corner: 'tl' },
    callout: {
      pos: 'bl',
      tag: '派对颁奖',
      title: '战后趣味颁奖时刻',
      sub: 'MVP · 最佳诱饵 · 人体炮弹 · 墙艺大师 · 背锅侠',
      accent: 'Marigold',
      icon: 'crown',
    },
  },
];

// 4 Systems Still Being Updated (Bars 16-24: exactly 2 bars = 4.2857s each)
export const SYSTEMS = [
  {
    id: 'weapons',
    num: '01',
    at: bar(16),
    len: bar(2),
    title: '武器系统',
    sub: 'WEAPONS & SHOP',
    accent: 'Coral',
    icon: 'weapons',
    main: { shot: 'hub_guns', from: 0, speed: 1.0, crop: [160, 180, 1600, 900], label: '六大枪械实机测试' },
    inset: { shot: 'hub_shop', from: 0, speed: 0.5, crop: [284, 128, 1364, 816], label: '局外商店 · 武器 / 笔 / 道具' },
    current: '手枪 · 步枪 · 霰弹枪 · 狙击枪 · 机枪 · 火箭筒（搭配 3 种画墙笔与 6 种道具）',
    updates: ['继续打磨六把枪的手感、后坐力与弹道反馈', '优化高难度下的伤害与墨水循环平衡', '更多脑洞新武器正在设计制作中'],
  },
  {
    id: 'achievements',
    num: '02',
    at: bar(18),
    len: bar(2),
    title: '成就系统',
    sub: 'ACHIEVEMENT WALL',
    accent: 'Marigold',
    icon: 'crown',
    main: { shot: 'hub_achievements', from: 0, speed: 0.5, crop: [284, 128, 1364, 816], label: '大厅成就墙 · 全队 8 人进度同屏对比' },
    inset: null,
    current: '32 项挑战已实装，房间内每位好友已解锁、还差哪一项一目了然',
    updates: ['新增更多联机整活与硬核挑战成就', '丰富解锁提示动效与专属贴纸 / 称号奖励', '拓展跨章节、远征及派对模式里程碑'],
  },
  {
    id: 'cosmetics',
    num: '03',
    at: bar(20),
    len: bar(2),
    title: '外观系统',
    sub: 'COSMETICS & WARDROBE',
    accent: 'Magenta',
    icon: 'hat',
    main: { shot: 'hub_hats', from: 0, speed: 1.0, crop: [100, 150, 1660, 934], label: '几何小人头饰实机展示' },
    inset: { shot: 'hub_wardrobe', from: 0, speed: 0.5, crop: [284, 128, 1364, 816], label: '衣柜 · 帽子与称号' },
    current: '纸帽子、毛线帽、礼帽、天线、铅笔、王冠等 7 款帽子 + 11 种派对称号',
    updates: ['持续增加更多搞怪头饰与可解锁造型', '加入更多趣味结算称号与个性展示', '计划拓展笔刷样式与专属视觉涂装'],
  },
  {
    id: 'attributes',
    num: '04',
    at: bar(22),
    len: bar(2),
    title: '属性系统',
    sub: 'ATTRIBUTES & RESEARCH',
    accent: 'Cobalt',
    icon: 'bolt',
    main: { shot: 'hub_research', from: 0, speed: 0.5, crop: [284, 128, 1364, 816], label: '研究台 · 永久属性养成 & 难度档限幅' },
    inset: null,
    current: '生命 · 伤害 · 墨水 · 投掷力 · 速度 · 拾取范围 6 大属性，配合 1–5 难度档位限幅',
    updates: ['精调每级属性成长曲线，兼顾爽感与策略深度', '优化高难度档位收益与联机公平匹配体验', '探索更多属性分支与构筑联动'],
  },
];

// Shopping List Development Plan (Bars 24-34: 51.43s - 72.86s)
export const LIST = {
  start: bar(24),         // 51.43s: spiral notepad & basket drop onto desk
  headerAt: bar(24.4),    // 52.29s: title & receipt stamp appear
  firstItem: bar(25.25),  // 54.11s: item 1 starts
  itemGap: beat(3),       // 1.607s per item -> 8 items finish at bar(25.25) + 7*beat(3) = 65.36s, last item finishes writing ~66.6s
  spotlightAt: bar(31.25),// 66.96s: highlight Item 3 (player wishes) + sticky note CTA
  end: bar(34),           // 72.86s
};

export const LIST_ITEMS = [
  { num: '01', title: '重构“远征”系统', note: '重做路线分支、扎营抉择与首领战，每趟远征都不一样', tag: '玩法重构', icon: 'compass', color: 'Coral' },
  { num: '02', title: '增加更多派对模式', note: '加入更多随机规则、派对轮盘变奏与欢乐整活玩法', tag: '×N 扩充', icon: 'roulette', color: 'Teal' },
  { num: '03', title: '收集玩家意愿', note: '你最想玩什么？把你的脑洞与建议直接写进这张清单', tag: '欢迎留言', icon: 'wishlist', color: 'Marigold' },
  { num: '04', title: '更新与优化现有系统', note: '持续打磨武器、成就、外观、属性与联机性能体验', tag: '全面打磨', icon: 'gear', color: 'Cobalt' },
  { num: '05', title: '更多的武器！更多的外观！更多的特性！', note: '新枪械、新帽子、新称号与更多创意特性加量不加价', tag: '×∞ 加量', icon: 'star', color: 'Magenta' },
  { num: '06', title: '上线发售', note: '打磨就绪，在 Steam 等平台正式与所有玩家见面', tag: '正式发售', icon: 'launch', color: 'Leaf' },
  { num: '07', title: '维护更新 BUG', note: '快速响应社区反馈，发现一个修好一个，稳定开黑', tag: '长期维护', icon: 'shield', color: 'Violet' },
  { num: '08', title: '开发新的功能', note: '不断探索新玩法与新惊喜，精彩内容未完待续……', tag: '敬请期待', icon: 'spark', color: 'Frost' },
];

export const listItemAt = i => LIST.firstItem + i * LIST.itemGap;

// Sound effect cues (aligned cleanly with picture & 112 BPM music)
export function audioCues() {
  const c = [];
  const add = (t, fx, gain = 1, pitch = 1) => c.push({ t, fx, gain, pitch });

  // Intro
  add(0.15, 'paperSlide', 0.65);
  add(beat(0.5), 'pop', 0.55, 0.92);
  add(beat(1.0), 'pop', 0.55, 1.05);
  add(beat(1.5), 'pop', 0.55, 1.18);
  add(beat(2.0), 'stamp', 0.95);
  add(beat(2.04), 'confetti', 0.6);
  add(beat(4.0), 'tape', 0.65);

  // TD chapter transition & gameplay hits
  add(ACT.tdCard - 0.15, 'pageTurn', 0.85);
  add(ACT.tdCard + 0.12, 'stamp', 0.75);
  add(ACT.td - 0.12, 'paperSlide', 0.55);

  // TD build ring pops
  add(bar(2.5) + 50 / 30 / 1.48, 'pop', 0.5, 1.1);
  add(bar(2.5) + 68 / 30 / 1.48, 'EvolutionPickup', 0.45);
  add(bar(2.5) + 84 / 30 / 1.48, 'pop', 0.5, 1.18);
  add(bar(2.5) + 102 / 30 / 1.48, 'EvolutionPickup', 0.45);
  add(bar(2.5) + 118 / 30 / 1.48, 'pop', 0.5, 1.26);

  // TD wave, rocket, boss, victory
  add(bar(4.5), 'paperSlide', 0.5);
  add(bar(4.5) + 0.2, 'Rifle', 0.35);
  add(bar(4.5) + 0.55, 'Shotgun', 0.35);
  add(bar(6.5) + (16 - 8) / 30, 'Rocket', 0.85);
  add(bar(6.5) + (42 - 8) / 30, 'BossStompImpact', 0.75);
  add(bar(7.5) + 0.1, 'BossLaserWindup', 0.35);
  add(bar(7.5) + (78 - 62) / 30, 'BossLaserBeam', 0.55);
  add(bar(7.5) + (93 - 62) / 30, 'BossLaserBeam', 0.5);
  add(bar(9) + 0.12, 'stamp', 0.85);
  add(bar(9) + 0.16, 'confetti', 0.5);

  // Co-op chapter transition & gameplay hits
  add(ACT.coopCard - 0.15, 'pageTurn', 0.85);
  add(ACT.coopCard + 0.12, 'stamp', 0.8);
  add(ACT.coop - 0.1, 'paperSlide', 0.55);
  add(bar(10.5) + 0.35, 'Pistol', 0.35);
  add(bar(12.25) + (52 - 30) / 30, 'WeaponPickup', 0.55);
  add(bar(12.25) + (82 - 30) / 30, 'BossStompImpact', 0.55);
  add(bar(14), 'MachineGun', 0.32);
  add(bar(14) + 0.25, 'Rifle', 0.32);
  add(bar(15) + 0.12, 'stamp', 0.85);
  add(bar(15) + 0.15, 'confetti', 0.55);

  // 4 Systems
  add(ACT.systems - 0.15, 'pageTurn', 0.9);
  SYSTEMS.forEach((s, idx) => {
    add(s.at + 0.05, 'paperSlide', 0.65);
    add(s.at + 0.3, idx === 0 ? 'WeaponPickup' : 'pop', 0.5, 1 + idx * 0.07);
    add(s.at + 0.9, 'stamp', 0.55);
  });

  // Shopping List
  add(LIST.start - 0.15, 'pageTurn', 0.9);
  add(LIST.start + 0.08, 'paperDrop', 0.8);
  add(LIST.headerAt + 0.1, 'scribble', 0.55);
  LIST_ITEMS.forEach((_, i) => {
    const t = listItemAt(i);
    add(t, 'pencilTick', 0.55);
    add(t + 0.14, 'scribble', 0.5);
    add(t + 0.48, 'Orb', 0.5, 0.92 + i * 0.05);
  });
  add(LIST.spotlightAt, 'tape', 0.6);
  add(LIST.spotlightAt + 0.18, 'scribble', 0.65);
  add(LIST.spotlightAt + 0.5, 'EvolutionPickup', 0.55);

  // Outro
  add(ACT.outro - 0.15, 'pageTurn', 0.9);
  add(ACT.outro + 0.45, 'stamp', 1.0);
  add(ACT.outro + 0.48, 'confetti', 0.8);
  add(ACT.outro + 1.25, 'tape', 0.65);

  return c.sort((a, b) => a.t - b.t);
}
