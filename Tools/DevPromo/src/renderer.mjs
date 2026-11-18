import { C, SEATS, rgba, clamp01, ease, PaperKit } from './design.mjs';
import * as TL from './timeline.mjs';

const W = 1920;
const H = 1080;

const canvas = document.getElementById('stage');
const ctx = canvas.getContext('2d', { willReadFrequently: true, alpha: false });
const kit = new PaperKit(ctx, W, H);

const params = new URLSearchParams(location.search);
const mode = params.get('mode') || 'preview';

let plateIndex = {};
const logoImg = new Image();
const imgCache = new Map();

async function loadAssets() {
  logoImg.src = '/assets/game-logo.png';
  await logoImg.decode();
  plateIndex = await (await fetch('/plates.json')).json();
  for (const w of [500, 700, 900]) {
    await document.fonts.load(`${w} 40px GWCJK`, '几何战争塔防联机武器成就外观属性开发计划购物清单远征派对上线发售');
    await document.fonts.load(`${w} 40px GWLatin`, 'GEOMETRIC WARFARE 0123456789');
  }
  await document.fonts.ready;
}

function findPlateFrame(shot, targetFrame) {
  const list = plateIndex[shot];
  if (!list || !list.length) return -1;
  const f = Math.max(0, Math.floor(targetFrame));
  let lo = 0, hi = list.length - 1, best = list[0];
  while (lo <= hi) {
    const mid = (lo + hi) >> 1;
    if (list[mid] <= f) { best = list[mid]; lo = mid + 1; }
    else hi = mid - 1;
  }
  return best;
}

async function getPlate(shot, targetFrame) {
  const f = findPlateFrame(shot, targetFrame);
  if (f < 0) return null;
  const key = `${shot}:${f}`;
  if (imgCache.has(key)) return imgCache.get(key);
  const res = await fetch(`/plates/${shot}/${String(f).padStart(4, '0')}.png`);
  if (!res.ok) return null;
  const bmp = await createImageBitmap(await res.blob());
  imgCache.set(key, bmp);
  if (imgCache.size > 36) {
    const oldest = imgCache.keys().next().value;
    imgCache.get(oldest)?.close?.();
    imgCache.delete(oldest);
  }
  return bmp;
}

function drawLogo(cx, cy, width, alpha = 1) {
  if (alpha <= 0.01) return;
  const h = (width * logoImg.naturalHeight) / logoImg.naturalWidth;
  ctx.save();
  ctx.globalAlpha *= alpha;
  ctx.shadowColor = 'rgba(31,38,51,0.24)';
  ctx.shadowOffsetX = 7;
  ctx.shadowOffsetY = 11;
  ctx.drawImage(logoImg, cx - width / 2, cy - h / 2, width, h);
  ctx.restore();
}

function drawCroppedPlate(img, dx, dy, dw, dh, crop) {
  if (!img) {
    ctx.fillStyle = C.PaperShade;
    ctx.fillRect(dx, dy, dw, dh);
    return;
  }
  const [sx, sy, sw, sh] = crop || [0, 0, img.width, img.height];
  ctx.drawImage(img, sx, sy, sw, sh, dx, dy, dw, dh);
}

// Framed photo card with washi tape & caption strip
function drawFramedPhoto(img, x, y, w, h, deg, seed, opts = {}) {
  const { crop = null, label = '', tapeColor = C.Teal, age = 1, alpha = 1 } = opts;
  ctx.save();
  ctx.globalAlpha *= alpha;
  ctx.translate(x + w / 2, y + h / 2);
  ctx.rotate((deg * Math.PI) / 180);
  ctx.translate(-(x + w / 2), -(y + h / 2));

  const pad = 14;
  const bot = label ? 50 : 14;
  kit.paperCard(x - pad, y - pad, w + pad * 2, h + pad + bot, {
    fill: C.PaperWarm, stroke: C.Ink, strokeW: 2.6, shadow: 10, seed, radius: 3, grain: false,
  });

  if (img) {
    ctx.save();
    ctx.beginPath();
    ctx.rect(x, y, w, h);
    ctx.clip();
    drawCroppedPlate(img, x, y, w, h, crop);
    ctx.restore();
  }
  ctx.strokeStyle = C.Ink;
  ctx.lineWidth = 2;
  ctx.strokeRect(x, y, w, h);

  kit.washiTape(x + 48, y - 6, 110, 28, -24, tapeColor, 0.88);
  kit.washiTape(x + w - 48, y - 6, 110, 28, 22, tapeColor, 0.88);

  if (label) {
    kit.text(label, x + 6, y + h + 13, 24, C.InkSoft, { weight: 700, alpha: clamp01((age - 0.15) * 3) });
  }
  ctx.restore();
}

// ---- ACT 0: INTRO (0 - 4.29s) ---------------------------------------------------------------------------------------
function renderIntro(t) {
  kit.drawDraftingDesk(t, { showRuler: true, doodleAlpha: 0.34 });

  // Three geometric buddies hop onto the desk then scatter when the logo slams down
  const buddies = [
    { kind: 'circle', color: C.Coral, tx: 660, ty: 420, sz: 175, delay: 0.16, fromX: -480, fromY: -240 },
    { kind: 'square', color: C.Teal, tx: 960, ty: 465, sz: 165, delay: 0.34, fromX: 0, fromY: -540 },
    { kind: 'triangle', color: C.Marigold, tx: 1260, ty: 430, sz: 185, delay: 0.52, fromX: 480, fromY: -240 },
  ];
  const logoAt = TL.beat(2); // ~1.07s
  buddies.forEach((b, idx) => {
    const enter = clamp01((t - b.delay) / 0.42);
    const scatter = clamp01((t - (logoAt - 0.06)) / 0.32);
    if (enter <= 0 || scatter >= 1) return;
    const p = ease.outBack(enter, 1.7);
    const x = b.tx + b.fromX * (1 - p) + (b.tx - 960) * 2.8 * ease.inCubic(scatter);
    const y = b.ty + b.fromY * (1 - p) - Math.sin(enter * Math.PI) * 70 + (b.ty - 440 - 80) * 2.5 * ease.inCubic(scatter);
    const rot = (1 - p) * (idx === 0 ? -0.6 : 0.6) + scatter * (idx - 1) * 1.4;
    const squash = 1 + 0.14 * Math.cos((t - b.delay - 0.4) * 18) * Math.exp(-Math.max(0, t - b.delay - 0.4) * 6);
    ctx.save();
    ctx.globalAlpha = 1 - scatter;
    kit.drawBuddy(b.kind, x, y, b.sz * (1 - scatter * 0.35), b.color, rot, squash);
    ctx.restore();
  });

  // Main Logo Slam
  const logoAge = t - logoAt;
  if (logoAge >= 0) {
    const slamP = clamp01(logoAge / 0.24);
    const scale = slamP < 1 ? 2.2 - 1.2 * ease.inCubic(slamP) : 1 + 0.03 * Math.cos((logoAge - 0.24) * 26) * Math.exp(-(logoAge - 0.24) * 7);
    const shake = logoAge > 0.18 && logoAge < 0.45 ? (1 - (logoAge - 0.18) / 0.27) * 7 : 0;

    ctx.save();
    ctx.translate(Math.sin(t * 97) * shake, Math.cos(t * 113) * shake);
    ctx.save();
    ctx.translate(960, 385);
    ctx.scale(scale, scale);
    drawLogo(0, 0, 1260, clamp01(slamP * 3));
    ctx.restore();

    kit.confetti(350, 380, logoAge - 0.16, 22, 920, 101);
    kit.confetti(1570, 380, logoAge - 0.16, 22, 920, 102);

    // Subtitle coral tape banner
    const subAge = t - TL.beat(3.5);
    if (subAge >= 0) {
      const sp = ease.outBack(clamp01(subAge / 0.34), 1.4);
      const bw = 760 * sp;
      kit.washiTape(960, 695, bw, 84, -1.2, C.Coral, 0.92);
      kit.text('版本宣传片 · 实机展示 & 开发清单', 960, 696, 38, C.Ink, {
        weight: 900, align: 'center', baseline: 'middle', alpha: clamp01((subAge - 0.08) * 4),
      });
    }

    // Four clean summary chips along bottom
    const pills = [
      { label: '塔防玩法', tag: '已完成', color: C.Teal, icon: 'maze' },
      { label: '多人联机', tag: '1-8 人', color: C.Cobalt, icon: 'hex' },
      { label: '四大系统', tag: '更新中', color: C.Coral, icon: 'gear' },
      { label: '开发计划', tag: '购物清单', color: C.Marigold, icon: 'wishlist' },
    ];
    pills.forEach((p, i) => {
      const pa = t - (TL.beat(4.5) + i * 0.16);
      if (pa < 0) return;
      const pop = ease.outBack(clamp01(pa / 0.3), 1.6);
      const cx = 360 + i * 400;
      const cy = 865;
      const pw = 340, ph = 82;
      ctx.save();
      ctx.translate(cx, cy);
      ctx.scale(pop, pop);
      kit.paperCard(-pw / 2, -ph / 2, pw, ph, { fill: C.PaperWarm, stroke: C.Ink, strokeW: 2.6, shadow: 6, seed: 200 + i });
      kit.drawIcon(p.icon, -pw / 2 + 42, 0, 38, p.color);
      kit.text(p.label, -pw / 2 + 76, -15, 28, C.Ink, { weight: 900 });
      kit.highlightBar(-pw / 2 + 76, 17, 132, 22, p.color, 1, 0.35);
      kit.text(p.tag, -pw / 2 + 84, 18, 19, C.InkSoft, { weight: 700 });
      ctx.restore();
    });
    ctx.restore();
  }
}

// ---- CHAPTER SLATES (4.29 - 5.36s & 21.43 - 22.50s) ---------------------------------------------------------------
function renderChapterSlate(kind, localAge) {
  kit.drawDraftingDesk(localAge, { showRuler: true, doodleAlpha: 0.24 });

  const cfg = kind === 'td'
    ? {
        num: 'PART 01',
        title: '塔防玩法',
        en: 'TOWER DEFENSE · 已实装',
        desc: '方格纸画墙造迷宫 · 圆盘建造防御塔 · 抵御波次与巨型 Boss',
        stamp: '已完成',
        accent: C.Teal,
        icons: ['maze', 'bolt', 'flame', 'star'],
      }
    : {
        num: 'PART 02',
        title: '多人联机',
        en: 'ONLINE CO-OP · 1–8 人同屏',
        desc: 'Steam / Epic 好友房间 · 抓起队友当诱饵 · 战后趣味颁奖',
        stamp: '已完成',
        accent: C.Cobalt,
        icons: null,
      };

  const enter = ease.outBack(clamp01(localAge / 0.34), 1.3);
  const cw = 980, ch = 430;
  const cx = W / 2, cy = H / 2 - 10;

  ctx.save();
  ctx.translate(cx, cy);
  ctx.scale(enter, enter);
  kit.paperCard(-cw / 2, -ch / 2, cw, ch, { fill: C.PaperWarm, stroke: C.Ink, strokeW: 3.5, shadow: 14, seed: 310 });
  kit.washiTape(0, -ch / 2 + 4, 210, 38, -2.5, cfg.accent, 0.9);

  // Left number pill
  kit.highlightBar(-cw / 2 + 54, -ch / 2 + 44, 150, 34, cfg.accent, 1, 0.35);
  kit.text(cfg.num, -cw / 2 + 66, -ch / 2 + 50, 22, C.Ink, { weight: 900 });

  // Big title
  const tw = kit.measure(cfg.title, 108, 900);
  kit.highlightBar(-tw / 2 - 14, -55, tw + 28, 42, cfg.accent, clamp01(localAge / 0.35), 0.42);
  kit.text(cfg.title, 0, -90, 108, C.Ink, { weight: 900, align: 'center' });
  kit.text(cfg.en, 0, 36, 28, C.InkSoft, { weight: 700, align: 'center' });
  kit.text(cfg.desc, 0, 88, 25, C.Ink, { weight: 700, align: 'center' });

  // Bottom decorative strip (either 4 TD icons or 8 Co-op seat circles!)
  if (cfg.icons) {
    cfg.icons.forEach((ic, idx) => {
      kit.drawIcon(ic, -120 + idx * 80, 155, 38, idx % 2 ? C.Coral : cfg.accent);
    });
  } else {
    SEATS.forEach((col, idx) => {
      const bob = Math.sin(localAge * 9 + idx * 0.7) * 5;
      kit.drawBuddy(idx % 3 === 0 ? 'circle' : idx % 3 === 1 ? 'square' : 'triangle', -210 + idx * 60, 155 + bob, 32, col, 0, 1);
    });
  }

  // Rubber stamp
  kit.rubberStamp(cfg.stamp, cw / 2 - 135, ch / 2 - 68, 40, cfg.accent, localAge - 0.16, -9, false);
  ctx.restore();
}

// ---- GAMEPLAY FOOTAGE & OVERLAYS (Acts 1 & 2) -----------------------------------------------------------------------
function renderClip(clip, t, img) {
  const localT = t - clip.at;
  const k = clamp01(localT / clip.len);
  const z = clip.zoom[0] + (clip.zoom[1] - clip.zoom[0]) * ease.inOutCubic(k);
  const dw = W * z;
  const dh = H * z;
  const focus = clip.focus || [W / 2, H / 2];
  const dx = Math.min(0, Math.max(W - dw, W / 2 - focus[0] * z));
  const dy = Math.min(0, Math.max(H - dh, H / 2 - focus[1] * z));

  drawCroppedPlate(img, dx, dy, dw, dh, null);

  // Subtle paper vignette around frame edges
  const grad = ctx.createRadialGradient(W / 2, H / 2, H * 0.42, W / 2, H / 2, W * 0.62);
  grad.addColorStop(0, 'rgba(31,38,51,0)');
  grad.addColorStop(1, 'rgba(31,38,51,0.16)');
  ctx.fillStyle = grad;
  ctx.fillRect(0, 0, W, H);

  // Top badge placed strictly in safe HUD zones
  if (clip.badge) {
    const b = clip.badge;
    const accent = C[b.accent] || C.Teal;
    const bw = kit.measure(b.text, 26, 900) + kit.measure(b.status, 20, 700) + 102;
    const bh = 54;
    const bx = b.corner === 'tr' ? W - bw - 32 : b.corner === 'tl-offset' ? 104 : 32;
    const by = 22;
    const slide = ease.outBack(clamp01(localT / 0.3), 1.3);
    const ox = (1 - slide) * (b.corner === 'tr' ? 120 : -120);

    ctx.save();
    ctx.translate(ox, 0);
    kit.paperCard(bx, by, bw, bh, { fill: C.PaperWarm, stroke: C.Ink, strokeW: 2.5, shadow: 6, seed: 410 });
    kit.washiTape(bx + bw - 26, by + 4, 58, 18, 28, accent, 0.9);
    kit.drawIcon('star', bx + 30, by + bh / 2, 26, accent);
    kit.text(b.text, bx + 54, by + 13, 26, C.Ink, { weight: 900 });
    const tw = kit.measure(b.text, 26, 900);
    kit.highlightBar(bx + 66 + tw, by + 14, kit.measure(b.status, 20, 700) + 16, 26, accent, 1, 0.34);
    kit.text(b.status, bx + 74 + tw, by + 16, 20, C.InkSoft, { weight: 700 });
    ctx.restore();
  }

  // Lower-corner kinetic callout card
  if (clip.callout) {
    const co = clip.callout;
    const accent = C[co.accent] || C.Coral;
    const tw = kit.measure(co.title, 36, 900);
    const sw = kit.measure(co.sub, 21, 700);
    const cw = Math.max(tw, sw) + 146;
    const ch = 122;
    const cx = co.pos === 'br' ? W - cw - 38 : 38;
    const cy = H - ch - 36;

    const enterP = clamp01(localT / 0.34);
    const leaveP = clamp01((localT - (clip.len - 0.24)) / 0.24);
    const slideX = (1 - ease.outBack(enterP, 1.3)) * (co.pos === 'br' ? 180 : -180) + ease.inCubic(leaveP) * (co.pos === 'br' ? 180 : -180);
    const alpha = clamp01(enterP * 3) * (1 - leaveP);

    ctx.save();
    ctx.translate(slideX, 0);
    ctx.globalAlpha *= alpha;
    kit.paperCard(cx, cy, cw, ch, { fill: C.PaperWarm, stroke: C.Ink, strokeW: 2.8, shadow: 9, seed: Math.round(clip.at * 100) });
    kit.washiTape(cx + 50, cy + 4, 88, 22, -18, accent, 0.9);
    kit.drawIcon(co.icon, cx + 56, cy + ch / 2 + 2, 52, accent);

    // Highlight under title
    kit.highlightBar(cx + 106, cy + 40, tw + 16, 22, accent, clamp01((localT - 0.14) / 0.32), 0.36);
    kit.text(co.title, cx + 110, cy + 20, 36, C.Ink, { weight: 900 });
    kit.text(co.sub, cx + 112, cy + 74, 21, C.InkSoft, { weight: 700, alpha: clamp01((localT - 0.2) * 3.5) });
    ctx.restore();
  }
}

// ---- ACT 3: FOUR SYSTEMS STILL BEING UPDATED (34.29 - 51.43s) ------------------------------------------------------
function renderSystems(t, sysImgs) {
  kit.drawDraftingDesk(t, { showRuler: false, doodleAlpha: 0.22 });

  // Top 4-Tab Blueprint Bar (y = 42..106)
  const activeIdx = TL.SYSTEMS.findIndex(s => t >= s.at && t < s.at + s.len);
  const idx = activeIdx >= 0 ? activeIdx : TL.SYSTEMS.length - 1;

  // Section label top-left
  kit.washiTape(175, 74, 260, 48, -1.5, C.Marigold, 0.9);
  kit.text('四大系统 · 迭代更新中', 175, 75, 24, C.Ink, { weight: 900, align: 'center', baseline: 'middle' });

  // 4 Index Tabs across top center
  TL.SYSTEMS.forEach((s, i) => {
    const on = i === idx;
    const accent = C[s.accent];
    const tx = 355 + i * 310;
    const ty = on ? 42 : 52;
    const tw = 285, th = on ? 62 : 50;
    kit.paperCard(tx, ty, tw, th, {
      fill: on ? C.PaperWarm : C.PaperShade,
      stroke: C.Ink,
      strokeW: on ? 3 : 2,
      shadow: on ? 6 : 2,
      seed: 500 + i,
      grain: false,
    });
    if (on) {
      ctx.fillStyle = accent;
      ctx.fillRect(tx + 3, ty + 3, tw - 6, 8);
    }
    kit.drawIcon(s.icon, tx + 32, ty + th / 2 + (on ? 3 : 0), 26, on ? accent : C.Pencil);
    kit.text(`${s.num} ${s.title}`, tx + 58, ty + th / 2 + (on ? 4 : 1), on ? 24 : 21, on ? C.Ink : C.InkSoft, {
      weight: 900, baseline: 'middle',
    });
  });

  // Top-right spinning gear indicator
  const gearX = 1745, gearY = 74;
  kit.paperCard(1600, 44, 270, 58, { fill: C.Sticky, stroke: C.Ink, strokeW: 2.4, shadow: 4, seed: 520 });
  ctx.save();
  ctx.translate(1635, gearY);
  ctx.rotate(t * 2.2);
  kit.drawIcon('gear', 0, 0, 30, C.Coral);
  ctx.restore();
  kit.text('开发优化中…', 1662, gearY + 1, 23, C.Ink, { weight: 900, baseline: 'middle' });

  // Active system slide-in / slide-out spread
  const sys = TL.SYSTEMS[idx];
  const age = t - sys.at;
  const remain = sys.at + sys.len - t;
  const enterX = (1 - ease.outBack(clamp01(age / 0.36), 1.1)) * 180;
  const exitX = remain < 0.28 ? -ease.inCubic(clamp01((0.28 - remain) / 0.28)) * 260 : 0;
  const alpha = clamp01(age / 0.2) * (remain < 0.25 ? clamp01(remain / 0.25) : 1);
  const imgs = sysImgs[idx] || {};
  const accent = C[sys.accent];

  ctx.save();
  ctx.translate(enterX + exitX, 0);
  ctx.globalAlpha *= alpha;

  // Left/Center Photo Stage
  drawFramedPhoto(imgs.main, 74, 154, 1100, 620, -1.2, 600 + idx, {
    crop: sys.main.crop,
    label: sys.main.label,
    tapeColor: accent,
    age,
  });

  // Optional Secondary UI Inset at TOP-RIGHT of photo stage (keeps lower row of guns/hats completely unobstructed!)
  if (sys.inset && imgs.inset) {
    const inAge = age - 0.35;
    if (inAge > 0) {
      const pop = ease.outBack(clamp01(inAge / 0.32), 1.4);
      ctx.save();
      ctx.translate(990, 290);
      ctx.scale(pop, pop);
      ctx.translate(-990, -290);
      drawFramedPhoto(imgs.inset, 750, 146, 480, 288, 2.6, 650 + idx, {
        crop: sys.inset.crop,
        label: sys.inset.label,
        tapeColor: C.Sticky,
        age: inAge,
      });
      ctx.restore();
    }
  }

  // Bottom progress pips under photo
  for (let i = 0; i < 4; i++) {
    const on = i === idx;
    kit.drawBuddy('square', 550 + i * 48, 882, on ? 28 : 18, on ? accent : C.PaperDeep, on ? Math.sin(t * 4) * 0.1 : 0, 1);
  }
  kit.text(`系统 ${idx + 1} / 4 · ${sys.title}`, 620, 912, 22, C.InkSoft, { weight: 700, align: 'center' });

  // Right Notebook Specification Card
  const sx = 1230, sy = 148, sw = 625, sh = 850;
  kit.paperCard(sx, sy, sw, sh, { fill: '#FBF7EA', stroke: C.Ink, strokeW: 3, shadow: 12, seed: 700 + idx });

  // Lined notebook rules + coral margin
  ctx.save();
  for (let ly = sy + 205; ly < sy + sh - 35; ly += 58) {
    kit.inkLine(sx + 18, ly, sx + sw - 18, ly, rgba(C.Grid, 0.55), 1.4, ly);
  }
  kit.inkLine(sx + 56, sy + 16, sx + 56, sy + sh - 16, rgba(C.Coral, 0.5), 2, 711);
  ctx.restore();

  kit.washiTape(sx + sw / 2, sy + 4, 170, 34, -1.5, accent, 0.9);
  kit.drawIcon(sys.icon, sx + sw - 74, sy + 92, 64, accent);

  const titleW = kit.measure(sys.title, 66, 900);
  kit.highlightBar(sx + 76, sy + 92, titleW + 24, 28, accent, clamp01((age - 0.15) / 0.35), 0.42);
  kit.text(sys.title, sx + 82, sy + 52, 66, C.Ink, { weight: 900 });
  kit.text(sys.sub, sx + 86, sy + 132, 22, C.InkSoft, { weight: 700 });

  // Section A: Current State
  kit.paperCard(sx + 78, sy + 190, sw - 110, 138, {
    fill: C.Paper, stroke: C.Ink, strokeW: 2, shadow: 3, seed: 720 + idx, grain: false,
  });
  kit.highlightBar(sx + 92, sy + 204, 118, 26, accent, 1, 0.3);
  kit.text('当前已实装', sx + 100, sy + 207, 20, C.Ink, { weight: 900 });
  kit.wrapText(sys.current, sx + 96, sy + 242, 23, sw - 148, 34, C.InkSoft, 700, clamp01((age - 0.25) / 0.3));

  // Section B: Update Focus
  kit.highlightBar(sx + 80, sy + 362, 168, 28, C.Marigold, 1, 0.45);
  kit.text('即将更新 · 重点', sx + 88, sy + 365, 22, C.Ink, { weight: 900 });

  sys.updates.forEach((line, k) => {
    const la = age - (0.38 + k * 0.22);
    if (la < 0) return;
    const ly = sy + 418 + k * 102;
    const reveal = ease.outCubic(clamp01(la / 0.38));
    kit.drawIcon('star', sx + 96, ly + 14, 22, accent);
    ctx.save();
    ctx.beginPath();
    ctx.rect(sx + 114, ly - 8, (sw - 140) * reveal, 95);
    ctx.clip();
    kit.wrapText(line, sx + 120, ly, 26, sw - 156, 36, C.Ink, 700);
    ctx.restore();
  });

  // Bottom caution "施工中" stamp
  kit.rubberStamp('待更新 · 施工中', sx + sw - 165, sy + sh - 78, 30, C.Ink, age - 0.85, -8, true);
  kit.text('持续打磨中…', sx + 82, sy + sh - 90, 24, C.InkSoft, { weight: 700, alpha: clamp01((age - 0.5) * 2) });

  ctx.restore();
}

// ---- ACT 4: SHOPPING LIST DEVELOPMENT PLAN (51.43 - 72.86s) ---------------------------------------------------------
function renderShoppingList(t) {
  kit.drawDraftingDesk(t, { showRuler: false, doodleAlpha: 0.18 });
  const age = t - TL.LIST.start;
  const padDrop = ease.outBack(clamp01(age / 0.45), 1.15);

  // ---- LEFT COLUMN: IN-THE-BAG RECEIPT, GEOMETRIC SHOPPING BASKET, WISHLIST STICKY NOTE ----
  // 1. "已入购物篮" receipt stub
  const bagAge = age - 0.25;
  if (bagAge > 0) {
    const pop = ease.outBack(clamp01(bagAge / 0.35), 1.4);
    ctx.save();
    ctx.translate(295, 165);
    ctx.rotate(-0.04);
    ctx.scale(pop, pop);
    kit.paperCard(-225, -108, 450, 216, { fill: C.Sticky, stroke: C.Ink, strokeW: 2.6, shadow: 8, seed: 801 });
    kit.washiTape(0, -108, 130, 28, 3, C.Teal, 0.88);
    kit.text('已入购物篮 · IN THE BAG', -195, -84, 22, C.InkSoft, { weight: 900 });
    kit.text('☑  塔防守线玩法（画墙 · 建塔 · Boss）', -195, -36, 23, C.Ink, { weight: 900 });
    kit.text('☑  1–8 人多人联机（大厅 · 投掷 · 颁奖）', -195, 8, 23, C.Ink, { weight: 900 });
    kit.text('◐  武器 / 成就 / 外观 / 属性系统更新中', -195, 54, 22, C.Coral, { weight: 900 });
    ctx.restore();
  }

  // 2. Shopping Basket collecting sticker icons as each of the 8 items is written!
  const basketAge = age - 0.4;
  if (basketAge > 0) {
    const bPop = ease.outBack(clamp01(basketAge / 0.4), 1.3);
    const itemsInBasket = TL.LIST_ITEMS.map((it, idx) => ({
      icon: it.icon,
      color: it.color,
      age: t - (TL.listItemAt(idx) + 0.45),
    }));
    ctx.save();
    ctx.translate(295, 505);
    ctx.scale(bPop, bPop);
    // Two cute buddies standing beside the basket
    kit.drawBuddy('circle', -168, 56 + Math.abs(Math.sin(t * 4)) * -8, 64, C.Coral, -0.08, 1);
    kit.drawBuddy('triangle', 168, 58 + Math.abs(Math.sin(t * 4 + 1.2)) * -8, 68, C.Marigold, 0.08, 1);
    kit.drawShoppingBasket(0, 10, 260, 175, itemsInBasket, t);
    ctx.restore();
  }

  // 3. Bottom Sticky Note CTA ("收集玩家意愿") at spotlightAt (66.96s)
  const ctaAge = t - TL.LIST.spotlightAt;
  if (ctaAge > 0) {
    const cPop = ease.outBack(clamp01(ctaAge / 0.36), 1.45);
    ctx.save();
    ctx.translate(295, 845);
    ctx.rotate(0.035);
    ctx.scale(cPop, cPop);
    kit.paperCard(-225, -135, 450, 270, { fill: C.PaperWarm, stroke: C.Ink, strokeW: 3, shadow: 10, seed: 822 });
    kit.washiTape(0, -135, 145, 30, -4, C.Coral, 0.9);
    kit.highlightBar(-150, -90, 300, 32, C.Marigold, 1, 0.45);
    kit.text('清单还能继续加购！', 0, -75, 32, C.Ink, { weight: 900, align: 'center', baseline: 'middle' });
    kit.text('欢迎留下你的想法与愿望', 0, -14, 25, C.InkSoft, { weight: 700, align: 'center', baseline: 'middle' });
    kit.text('下一条也许就写着你的点子 ↗', 0, 42, 25, C.Coral, { weight: 900, align: 'center', baseline: 'middle' });
    kit.drawBuddy('square', 0, 96, 42, C.Teal, Math.sin(t * 5) * 0.1, 1);
    ctx.restore();
  }

  // ---- RIGHT COLUMN: THE SPIRAL SHOPPING LIST PAD ----
  const pad = { x: 585, y: 44, w: 1275, h: 992, deg: -0.6 };
  ctx.save();
  ctx.translate(pad.x + pad.w / 2, pad.y + pad.h / 2 - (1 - padDrop) * 240);
  ctx.rotate((pad.deg * Math.PI) / 180);
  ctx.translate(-(pad.x + pad.w / 2), -(pad.y + pad.h / 2));

  kit.paperCard(pad.x, pad.y, pad.w, pad.h, { fill: '#FFFCF3', stroke: C.Ink, strokeW: 3.2, shadow: 14, seed: 850 });

  // Spiral binding top bar with punched holes & wire rings
  ctx.fillStyle = C.PaperDeep;
  ctx.fillRect(pad.x + 4, pad.y + 4, pad.w - 8, 62);
  kit.inkLine(pad.x, pad.y + 66, pad.x + pad.w, pad.y + 66, C.Ink, 2.4, 851);
  for (let h = 0; h < 16; h++) {
    const hx = pad.x + 64 + (h * (pad.w - 128)) / 15;
    ctx.fillStyle = C.Night;
    ctx.beginPath(); ctx.arc(hx, pad.y + 34, 9, 0, Math.PI * 2); ctx.fill();
    ctx.strokeStyle = C.RulerEdge;
    ctx.lineWidth = 3;
    ctx.beginPath(); ctx.arc(hx, pad.y + 14, 14, Math.PI * 0.9, Math.PI * 2.1); ctx.stroke();
  }

  // Vertical red notebook margin + ruled horizontal lines
  kit.inkLine(pad.x + 114, pad.y + 72, pad.x + 114, pad.y + pad.h - 14, rgba(C.Coral, 0.55), 2, 852);
  const rowTop = pad.y + 220;
  const rowStep = 94;
  for (let i = 0; i < 8; i++) {
    const ly = rowTop + i * rowStep + rowStep - 6;
    kit.inkLine(pad.x + 22, ly, pad.x + pad.w - 22, ly, rgba(C.Grid, 0.55), 1.4, 860 + i);
  }

  // Pad Header: "开发计划 · 购物清单"
  const headAge = t - TL.LIST.headerAt;
  if (headAge > 0) {
    const hw = kit.measure('开发计划', 60, 900);
    kit.highlightBar(pad.x + 144, pad.y + 124, (hw + 28) * ease.outCubic(clamp01(headAge / 0.45)), 30, C.Marigold, 1, 0.48);
    ctx.save();
    ctx.beginPath();
    ctx.rect(pad.x + 140, pad.y + 78, (hw + 40) * ease.outCubic(clamp01(headAge / 0.45)), 90);
    ctx.clip();
    kit.text('开发计划', pad.x + 152, pad.y + 92, 60, C.Ink, { weight: 900 });
    ctx.restore();

    kit.text('SHOPPING LIST · 后续功能购物清单', pad.x + 168 + hw + 28, pad.y + 114, 26, C.InkSoft, {
      weight: 700, alpha: clamp01((headAge - 0.2) * 3),
    });
    kit.text('STORE #2026-11  ·  持续补货中', pad.x + pad.w - 40, pad.y + 118, 20, C.Pencil, {
      weight: 700, align: 'right', alpha: clamp01((headAge - 0.25) * 3),
    });
  }

  // 8 Shopping List Items
  let activePenPos = null;
  TL.LIST_ITEMS.forEach((item, i) => {
    const itAt = TL.listItemAt(i);
    const ia = t - itAt;
    if (ia < 0) return;

    const ry = rowTop + i * rowStep;
    const accent = C[item.color] || C.Coral;

    // 1. Hand-drawn checkbox
    const bx = pad.x + 48, by = ry + 20, bs = 38;
    const boxP = clamp01(ia / 0.16);
    if (boxP > 0) {
      kit.inkLine(bx, by, bx + bs * Math.min(1, boxP * 4), by, C.Ink, 2.8, 900 + i * 4);
      if (boxP > 0.25) kit.inkLine(bx + bs, by, bx + bs, by + bs * Math.min(1, (boxP - 0.25) * 4), C.Ink, 2.8, 901 + i * 4);
      if (boxP > 0.5) kit.inkLine(bx + bs, by + bs, bx + bs * (1 - Math.min(1, (boxP - 0.5) * 4)), by + bs, C.Ink, 2.8, 902 + i * 4);
      if (boxP > 0.75) kit.inkLine(bx, by + bs, bx, by + bs * (1 - Math.min(1, (boxP - 0.75) * 4)), C.Ink, 2.8, 903 + i * 4);
    }

    // 2. Number badge
    kit.text(`${item.num}.`, pad.x + 136, ry + 14, 30, C.Coral, { weight: 900, alpha: clamp01(ia * 5) });

    // 3. Title revealed left-to-right (max 680px so it NEVER reaches the icon/tag column!)
    const titleX = pad.x + 196;
    const titleSize = 32;
    const titleW = Math.min(680, kit.measure(item.title, titleSize, 900));
    const reveal = ease.outCubic(clamp01((ia - 0.12) / 0.55));
    if (reveal > 0) {
      ctx.save();
      ctx.beginPath();
      ctx.rect(titleX - 4, ry + 2, (titleW + 18) * reveal, 52);
      ctx.clip();
      kit.text(item.title, titleX, ry + 12, titleSize, C.Ink, { weight: 900, maxWidth: 680 });
      ctx.restore();
      if (reveal < 0.99) {
        activePenPos = { x: titleX + titleW * reveal, y: ry + 42, writing: true };
      }
    }

    // 4. Concise description under title
    const noteReveal = clamp01((ia - 0.38) / 0.35);
    if (noteReveal > 0) {
      ctx.save();
      ctx.beginPath();
      ctx.rect(titleX - 4, ry + 48, 720 * noteReveal, 40);
      ctx.clip();
      kit.text(item.note, titleX + 2, ry + 52, 21, C.InkSoft, { weight: 500, maxWidth: 700 });
      ctx.restore();
    }

    // 5. Dotted receipt leader line cleanly between titleW and icon
    const dotAlpha = clamp01((ia - 0.45) / 0.25);
    if (dotAlpha > 0) {
      const dotStart = titleX + titleW + 24;
      const dotEnd = pad.x + 1008;
      if (dotEnd > dotStart + 20) {
        ctx.save();
        ctx.strokeStyle = rgba(C.Pencil, 0.55 * dotAlpha);
        ctx.lineWidth = 2.4;
        ctx.setLineDash([4, 9]);
        ctx.beginPath();
        ctx.moveTo(dotStart, ry + 32);
        ctx.lineTo(dotEnd, ry + 32);
        ctx.stroke();
        ctx.restore();
      }
    }

    // 6. Vector icon & right-hand receipt tag
    const tagAge = ia - 0.46;
    if (tagAge > 0) {
      const pop = ease.outBack(clamp01(tagAge / 0.26), 1.6);
      ctx.save();
      ctx.translate(pad.x + 1052, ry + 34);
      ctx.scale(pop, pop);
      kit.drawIcon(item.icon, 0, 0, 34, accent);
      ctx.restore();

      ctx.save();
      ctx.translate(pad.x + 1168, ry + 34);
      ctx.scale(pop, pop);
      kit.highlightBar(-64, -18, 128, 36, accent, 1, 0.24);
      kit.text(item.tag, 0, 1, 23, C.Ink, { weight: 900, align: 'center', baseline: 'middle' });
      ctx.restore();
    }
  });

  // Spotlight highlight on Item 03 ("收集玩家意愿") after all 8 items are written
  const spotAge = t - TL.LIST.spotlightAt;
  if (spotAge > 0) {
    const ry3 = rowTop + 2 * rowStep;
    const sw = ease.outCubic(clamp01(spotAge / 0.55));
    // Highlight bar + clean coral underline/bracket around row 3 (doesn't strike through neighbor rows!)
    kit.highlightBar(pad.x + 188, ry3 + 8, 250 * sw, 42, C.Marigold, 1, 0.42);
    if (sw > 0.1) {
      ctx.save();
      ctx.strokeStyle = C.Coral;
      ctx.lineWidth = 3.5;
      ctx.beginPath();
      ctx.roundRect(pad.x + 184, ry3 + 4, 248, 48, 8);
      ctx.stroke();
      ctx.restore();
    }
    if (sw < 1) {
      activePenPos = { x: pad.x + 188 + 250 * sw, y: ry3 + 48, writing: true };
    }
  }

  // Mechanical Pencil resting / writing on the pad
  if (activePenPos) {
    const bob = Math.sin(t * 36) * 2.5;
    kit.drawPencil(activePenPos.x + bob, activePenPos.y + bob * 0.5, 0.72);
  } else if (t > TL.LIST.headerAt && t < TL.LIST.spotlightAt + 1.5) {
    // Resting gently near bottom-right corner of pad between lines
    const restX = pad.x + pad.w - 190;
    const restY = pad.y + pad.h - 110 + Math.sin(t * 2.5) * 4;
    kit.drawPencil(restX, restY, 0.68);
  }

  ctx.restore();
}

// ---- ACT 5: FINALE / OUTRO (72.86 - 81.43s) -------------------------------------------------------------------------
function renderOutro(t, brawlImg) {
  const localT = t - TL.ACT.outro;
  const k = clamp01(localT / (TL.END - TL.ACT.outro));
  const z = 1.03 + 0.04 * k;
  const dw = W * z, dh = H * z;

  if (brawlImg) {
    drawCroppedPlate(brawlImg, (W - dw) / 2, (H - dh) / 2, dw, dh, null);
    const rad = ctx.createRadialGradient(W / 2, H * 0.45, 140, W / 2, H * 0.45, W * 0.65);
    rad.addColorStop(0, rgba(C.Paper, 0.88));
    rad.addColorStop(0.55, rgba(C.Paper, 0.65));
    rad.addColorStop(1, rgba(C.Paper, 0.25));
    ctx.fillStyle = rad;
    ctx.fillRect(0, 0, W, H);
  } else {
    kit.drawDraftingDesk(t);
  }

  const logoAge = localT - 0.38;
  if (logoAge >= 0) {
    const slamP = clamp01(logoAge / 0.24);
    const scale = slamP < 1 ? 2.1 - 1.1 * ease.inCubic(slamP) : 1 + 0.03 * Math.cos((logoAge - 0.24) * 24) * Math.exp(-(logoAge - 0.24) * 7);
    ctx.save();
    ctx.translate(960, 395);
    ctx.scale(scale, scale);
    drawLogo(0, 0, 1240, clamp01(slamP * 3));
    ctx.restore();

    kit.confetti(370, 395, logoAge - 0.14, 28, 1050, 901);
    kit.confetti(1550, 395, logoAge - 0.14, 28, 1050, 902);
  }

  const ctaAge = localT - 1.15;
  if (ctaAge >= 0) {
    const p = ease.outBack(clamp01(ctaAge / 0.34), 1.4);
    kit.washiTape(960, 725, 680 * p, 92, -1.2, C.Teal, 0.92);
    kit.text('欢迎留下你的想法 · 敬请期待', 960, 726, 44, C.Ink, {
      weight: 900, align: 'center', baseline: 'middle', alpha: clamp01((ctaAge - 0.08) * 3.5),
    });
  }

  const pillsAge = localT - 1.85;
  if (pillsAge >= 0) {
    const alpha = clamp01(pillsAge / 0.35);
    kit.paperCard(480, 835, 960, 62, { fill: C.PaperWarm, stroke: C.Ink, strokeW: 2.4, shadow: 6, seed: 950, alpha });
    kit.text('塔防 × 多人联机已实装  ·  四大系统持续更新  ·  开发购物清单共创中', 960, 867, 24, C.Ink, {
      weight: 900, align: 'center', baseline: 'middle', alpha,
    });
  }

  const noteAge = localT - 2.4;
  if (noteAge >= 0) {
    kit.text('画面为开发中版本演示 · 期待与大家一起把这份清单变成现实', 960, 985, 22, C.InkSoft, {
      weight: 700, align: 'center', alpha: clamp01(noteAge / 0.4),
    });
  }

  // Final settle to cream paper
  const fadeOut = clamp01((t - (TL.END - 0.65)) / 0.65);
  if (fadeOut > 0) {
    ctx.fillStyle = rgba(C.Paper, fadeOut);
    ctx.fillRect(0, 0, W, H);
  }
}

// ---- TRANSITION WIPES -----------------------------------------------------------------------------------------------
const WIPES = [
  { at: TL.ACT.tdCard, dur: 0.44, accent: C.Teal },
  { at: TL.ACT.td, dur: 0.40, accent: C.Teal },
  { at: TL.ACT.coopCard, dur: 0.44, accent: C.Cobalt },
  { at: TL.ACT.coop, dur: 0.40, accent: C.Cobalt },
  { at: TL.ACT.systems, dur: 0.48, accent: C.Marigold },
  { at: TL.ACT.list, dur: 0.48, accent: C.Coral },
  { at: TL.ACT.outro, dur: 0.50, accent: C.Teal },
];

function renderWipes(t) {
  for (const w of WIPES) {
    const start = w.at - w.dur * 0.5;
    if (t >= start && t <= start + w.dur) {
      kit.paperSheetWipe((t - start) / w.dur, w.accent);
    }
  }
}

// ---- MAIN FRAME COMPOSER --------------------------------------------------------------------------------------------
function findClipAt(t) {
  for (const c of TL.CLIPS) {
    if (t >= c.at && t < c.at + c.len) return c;
  }
  return null;
}

async function renderFrame(t) {
  kit.setClock(t);
  ctx.setTransform(1, 0, 0, 1, 0, 0);
  ctx.globalAlpha = 1;

  if (t < TL.ACT.tdCard) {
    renderIntro(t);
  } else if (t < TL.ACT.td) {
    renderChapterSlate('td', t - TL.ACT.tdCard);
  } else if (t < TL.ACT.coopCard) {
    const clip = findClipAt(t);
    const img = clip ? await getPlate(clip.shot, clip.from + (t - clip.at) * TL.FPS * clip.speed) : null;
    if (clip) renderClip(clip, t, img);
    else kit.drawDraftingDesk(t);
  } else if (t < TL.ACT.coop) {
    renderChapterSlate('coop', t - TL.ACT.coopCard);
  } else if (t < TL.ACT.systems) {
    const clip = findClipAt(t);
    const img = clip ? await getPlate(clip.shot, clip.from + (t - clip.at) * TL.FPS * clip.speed) : null;
    if (clip) renderClip(clip, t, img);
    else kit.drawDraftingDesk(t);
  } else if (t < TL.ACT.list) {
    const sysImgs = await Promise.all(TL.SYSTEMS.map(async s => {
      if (t < s.at || t >= s.at + s.len) return {};
      const age = t - s.at;
      const mainImg = await getPlate(s.main.shot, s.main.from + age * TL.FPS * s.main.speed);
      const insetImg = s.inset ? await getPlate(s.inset.shot, s.inset.from + age * TL.FPS * s.inset.speed) : null;
      return { main: mainImg, inset: insetImg };
    }));
    renderSystems(t, sysImgs);
  } else if (t < TL.ACT.outro) {
    renderShoppingList(t);
  } else {
    const brawlImg = await getPlate('brawl', 500 + (t - TL.ACT.outro) * TL.FPS);
    renderOutro(t, brawlImg);
  }

  renderWipes(t);
}

// ---- DRIVER LOOP ----------------------------------------------------------------------------------------------------
async function post(url, body) {
  for (let tries = 0; tries < 5; tries++) {
    try {
      const r = await fetch(url, { method: 'POST', body });
      if (r.ok) return;
    } catch { }
    await new Promise(r => setTimeout(r, 150));
  }
}

async function run() {
  try {
    await post('/log', `starting mode=${mode}`);
    await loadAssets();
    await post('/log', 'assets loaded');
    if (mode === 'video') {
      const fromSec = parseFloat(params.get('from') || '0');
      const toParam = parseFloat(params.get('to') || '-1');
      const fromF = Math.round(fromSec * TL.FPS);
      const toF = Math.round((toParam < 0 ? TL.END : toParam) * TL.FPS);
      for (let f = fromF; f < toF; f++) {
        await renderFrame(f / TL.FPS);
        await post(`/frame?i=${f}`, ctx.getImageData(0, 0, W, H).data);
      }
      await post('/done', `rendered ${toF - fromF} frames`);
    } else if (mode === 'stills' || mode === 'sheet') {
      let times = (params.get('times') || '').split(',').filter(Boolean).map(Number);
      if (mode === 'sheet') {
        const from = parseFloat(params.get('from') || '0');
        const to = parseFloat(params.get('to') || String(TL.END));
        const count = parseInt(params.get('count') || '20', 10);
        times = Array.from({ length: count }, (_, i) => from + ((to - from) * i) / Math.max(1, count - 1));
      }
      for (const [i, tm] of times.entries()) {
        await renderFrame(tm);
        const blob = await new Promise(r => canvas.toBlob(r, 'image/png'));
        const name = mode === 'sheet' ? `sheet-${String(i).padStart(3, '0')}` : `t${tm.toFixed(2)}`;
        await post(`/still?name=${name}`, blob);
      }
      await post('/done', `saved ${times.length} stills`);
    } else {
      document.body.classList.add('preview');
      const seek = document.getElementById('seek');
      const timeLbl = document.getElementById('time');
      seek.max = String(TL.END);
      const drawAt = async sec => {
        await renderFrame(sec);
        timeLbl.textContent = sec.toFixed(2);
      };
      seek.addEventListener('input', () => drawAt(parseFloat(seek.value)));
      await drawAt(0);
    }
  } catch (err) {
    await post('/log', `ERROR: ${err?.stack || err}`);
    await post('/done', `error: ${err?.message || err}`);
  }
}

window.addEventListener('error', e => post('/log', `WINDOW ERROR: ${e.message}`));
window.addEventListener('unhandledrejection', e => post('/log', `REJECTION: ${e.reason?.stack || e.reason}`));
run();
