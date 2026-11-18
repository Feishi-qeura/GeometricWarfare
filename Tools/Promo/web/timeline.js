// The promo's timeline, shared by the picture (promo.js) and the soundtrack (music.mjs). 120 BPM: a beat is half a
// second (15 frames), a bar two seconds; sections start on bar lines and cuts land on beats.
export const FPS = 30, BPM = 120, BEAT = 60 / BPM, BAR = 4 * BEAT;
export const END = 82;

// Section starts (seconds). Each section's page wipe closes over the end of the one before it.
export const S = {
  intro: 0,
  td: 6,
  coop: 22,
  next: 36,
  list: 54,
  outro: 76,
};
export const WipeClose = .55, WipeOpen = .75;
// A section card pops in this long before its wipe has closed; its stamp lands .62 s after the pop.
export const CardLead = .14;

// The page wipes between sections: tiles close over the old scene ending at `at`, a title card rides on the cover,
// and the tiles open again at `open` (the TD section hands over to the game's own wipe in td_open instead).
export const Wipes = [
  { at: 5.45, open: null, card: 'td' },
  { at: 22.0, open: 23.0, card: 'coop' },
  { at: 36.0, open: 37.0, card: 'next' },
  { at: 76.0, open: 76.75, card: 'outro' },
];

export const Cards = {
  td: { big: '塔防', small: 'TOWER DEFENSE', stamp: '已完成', accent: 'Teal', glyph: 19 },
  coop: { big: '联机', small: 'ONLINE CO-OP · 1–8 人', stamp: '已完成', accent: 'Cobalt', glyph: 4 },
  next: { big: '即将更新', small: 'COMING NEXT', stamp: '施工中', accent: 'Marigold', glyph: 10 },
  outro: { big: '', small: '', stamp: '', accent: 'Coral', glyph: 3 },
};

// Footage: a plate (Saved/Promo/Plates/<shot>) from plate frame `from`, on screen from `at` for `len` seconds at
// `speed` frames per output frame. `zoom` eases a slow push-in across the clip.
export const Clips = [
  { shot: 'td_open', at: 6.45, len: 2.55, from: 18, speed: 1, zoom: [1, 1.02] },
  { shot: 'td_build', at: 9.0, len: 4.0, from: 0, speed: 1.6, zoom: [1.02, 1.05] },
  { shot: 'td_wave', at: 13.0, len: 3.5, from: 20, speed: 1, zoom: [1, 1.03] },
  { shot: 'td_rocket', at: 16.5, len: 2.0, from: 6, speed: 1, zoom: [1.04, 1.12], focus: [1100, 470] },
  { shot: 'td_boss', at: 18.5, len: 2.5, from: 30, speed: 1, zoom: [1, 1.03] },
  { shot: 'td_victory', at: 21.0, len: 1.0, from: 6, speed: 1, zoom: [1, 1] },
  { shot: 'coop_hub', at: 23.0, len: 3.5, from: 0, speed: 1, zoom: [1.02, 1.05] },
  { shot: 'coop_throw', at: 26.5, len: 4.5, from: 24, speed: 1, zoom: [1, 1.02] },
  { shot: 'coop_defend', at: 31.0, len: 3.0, from: 40, speed: 1, zoom: [1, 1.03] },
  { shot: 'coop_results', at: 34.0, len: 2.0, from: 0, speed: 1, zoom: [1, 1] },
];

// The outro's background: a stretch of the title page's brawl (GW.Promo.Capture.Brawl, one minute from its seed).
export const OutroBrawl = { shot: 'brawl', from: 660 };

// Lower-corner captions over the footage.
export const Captions = [
  { at: 7.4, len: 1.5, title: '在方格纸上布防', sub: '每一页都是一个小盒子，核心就在里面', glyph: 19, color: 'Teal' },
  { at: 9.25, len: 3.5, title: '画墙改路 · 圆盘建塔', sub: '敌人自动寻路——你画的墙，就是它们的迷宫', glyph: 20, color: 'Kraft' },
  { at: 13.25, len: 3.0, title: '守住这一波！', sub: '弩炮塔 · 迫击炮 · 电塔 · 铁丝网，再加上你的枪', glyph: 11, color: 'Coral' },
  { at: 16.6, len: 1.8, title: '火箭筒抛射', sub: '蓄力越久飞得越远（队友也会被炸到）', glyph: 12, color: 'Fire' },
  { at: 18.6, len: 2.3, title: 'Boss 激光切开迷宫', sub: '墙会被打碎——下一波前记得补上', glyph: 22, color: 'Plum' },
  { at: 23.4, len: 2.9, title: '大厅就是房间', sub: 'Steam 好友一键加入，最多 8 人', glyph: 4, color: 'Cobalt' },
  { at: 26.7, len: 4.1, title: '把队友扔下去当诱饵！', sub: '落地砸倒一片，再把敌人全都引过去', glyph: 15, color: 'Magenta' },
  { at: 31.2, len: 2.6, title: '八人同守一个盒子', sub: '整活强度：关闭 / 友好 / 混乱', glyph: 14, color: 'Teal' },
];

// The section badge in the top corner while a section's footage plays.
export const Badges = [
  { from: 7.0, to: 21.9, text: '塔防', state: '已完成', accent: 'Teal' },
  { from: 23.3, to: 35.9, text: '联机', state: '已完成', accent: 'Cobalt' },
];

// The systems still being built: a photo of the game (a plate) and a notebook card each.
export const Systems = [
  {
    at: 37.0, title: '武器系统', en: 'WEAPONS', glyph: 15, accent: 'Coral', main: { shot: 'hub_guns', from: 0, crop: [200, 225, 1520, 855] },
    lines: ['手枪 · 步枪 · 霰弹枪 · 狙击枪 · 机枪 · 火箭筒', '六把枪的手感与平衡继续打磨', '更多武器正在路上'],
    inset: { shot: 'hub_shop', from: 20, sheet: true },
  },
  {
    at: 41.0, title: '成就系统', en: 'ACHIEVEMENTS', glyph: 18, accent: 'Marigold', main: { shot: 'hub_achievements', from: 0, sheet: true },
    lines: ['成就墙：全队 8 人谁拿到了、谁还差', '一眼就能看明白', '更多挑战与奖励整理中'],
  },
  {
    at: 45.0, title: '外观系统', en: 'COSMETICS', glyph: 5, accent: 'Magenta', main: { shot: 'hub_hats', from: 0, crop: [80, 140, 1680, 945] },
    lines: ['帽子：纸帽子、礼帽、王冠……', '称号：MVP、最佳诱饵、人体炮弹', '打扮你的几何小人'],
    inset: { shot: 'hub_wardrobe', from: 20, sheet: true },
  },
  {
    at: 49.0, title: '属性系统', en: 'ATTRIBUTES', glyph: 11, accent: 'Cobalt', main: { shot: 'hub_research', from: 0, sheet: true },
    lines: ['研究台：生命 · 伤害 · 墨水 · 投掷力 · 速度', '局外养成，越玩越强', '难度档位保证联机公平'],
  },
];
export const SystemLen = 4.0;
// A spread is shoved off the desk to the left from SystemExit before its time is up, still sliding SystemOverlap into
// the next one (which drops in on top), so no frame of the hand-over stands still.
export const SystemExit = .45, SystemOverlap = .3;

// The development plan, a shopping list: title, a short note, and the quantity pencilled beside it. The notepad drops
// in as the last spread slides away; the pencil comes in as it lands and writes the heading.
export const ListStart = 53.0;
export const ListTitle = { penIn: 53.32, write: 53.78, done: 54.5 };
export const ListItems = [
  { title: '重构"远征"系统', note: '重新设计远征玩法，让每一趟都不一样', qty: '×1', glyph: 18 },
  { title: '增加更多派对模式', note: '更多随机规则，更多整活玩法', qty: '×N', glyph: 15 },
  { title: '收集玩家意愿', note: '你想玩什么？你的建议会写进这张清单', qty: '×每一条', glyph: 8 },
  { title: '更新与优化现有系统', note: '手感、平衡、性能，一项一项打磨', qty: '×全部', glyph: 10 },
  { title: '更多的武器！更多的外观！更多的特性！', note: '多多益善', qty: '×∞', glyph: 5 },
  { title: '上线发售', note: '正式和大家见面', qty: '×1', glyph: 16 },
  { title: '维护更新 BUG', note: '发现一个，修好一个', qty: '×长期', glyph: 14 },
  { title: '开发新的功能', note: '未完待续……', qty: '×？', glyph: 7 },
];
export const ListFirstItem = 56.0, ListItemGap = 2.0;
export const listItemAt = i => ListFirstItem + i * ListItemGap;

// The soundtrack follows this cut: the score in audio/score.mjs, the sound effects in audio/foley.mjs (with the game's
// own events from Plates/<shot>/events.csv), mixed by music.mjs.
