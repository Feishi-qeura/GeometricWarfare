// Geometric Warfare development-update promo: every frame drawn on one canvas with the game's sketchbook toolkit
// (sketch.js) over footage the game itself rendered (GW.Promo.Capture plates). See timeline.js for the cut.
import { Palette as P, rgba, noise, hash01, Ease, V, add, mul, Sketch, confetti, wipe, fontFor } from './sketch.js';
import * as TL from './timeline.js';

const W = 1920, H = 1080;
const canvas = document.getElementById('c');
const ctx = canvas.getContext('2d', { willReadFrequently: true, alpha: false });
const sk = new Sketch(ctx, W, H);
const K = sk.scale; // 1.2 at 1080p, the HUD's own scale
const params = new URLSearchParams(location.search);
const mode = params.get('mode') || 'preview';
const log = m => fetch('/log', { method: 'POST', body: String(m) }).catch(() => { });

// --- assets ----------------------------------------------------------------------------------------------------------
let plateIndex = {};
const logo = new Image();
async function loadAssets() {
  logo.src = '/assets/logo.webp';
  await logo.decode();
  plateIndex = await (await fetch('/plates.json')).json();
  for (const weight of [500, 700, 900]) {
    await document.fonts.load(`${weight} 40px GWCJK`, '几何战争塔防联机即将更新开发计划购物清单');
    await document.fonts.load(`${weight} 40px GWLatin`, 'GEOMETRIC 0123');
  }
  await document.fonts.ready;
}
const cache = new Map();
function nearestFrame(shot, frame) {
  const frames = plateIndex[shot];
  if (!frames || !frames.length) return -1;
  let lo = 0, hi = frames.length - 1, best = frames[0];
  while (lo <= hi) {
    const mid = (lo + hi) >> 1;
    if (frames[mid] <= frame) { best = frames[mid]; lo = mid + 1; } else hi = mid - 1;
  }
  return best;
}
async function plateImage(shot, frame) {
  const f = nearestFrame(shot, Math.max(0, Math.floor(frame)));
  if (f < 0) return null;
  const key = `${shot}:${f}`;
  if (cache.has(key)) return cache.get(key);
  const res = await fetch(`/plates/${shot}/${String(f).padStart(4, '0')}.png`);
  if (!res.ok) return null;
  const bmp = await createImageBitmap(await res.blob());
  cache.set(key, bmp);
  if (cache.size > 30) {
    const first = cache.keys().next().value;
    cache.get(first)?.close?.();
    cache.delete(first);
  }
  return bmp;
}

// --- helpers ---------------------------------------------------------------------------------------------------------
const clamp01 = Ease.clamp01;
const between = (t, a, b) => t >= a && t < b;
const ease = (t, a, b) => Ease.smooth((t - a) / (b - a));
const color = name => P[name] || name;

// The desk every 2D scene lies on: graph paper like the game's floor, its grain, a soft vignette and pencil doodles.
function desk(t, opts = {}) {
  ctx.fillStyle = opts.paper || P.Paper;
  ctx.fillRect(0, 0, W, H);
  const drift = opts.drift ?? 0;
  ctx.save();
  ctx.translate(-((t * drift) % 32), 0);
  for (let x = 0; x <= W + 32; x += 32) {
    const major = Math.round(x / 32) % 5 === 0;
    ctx.fillStyle = rgba(P.Grid, major ? .34 : .17);
    ctx.fillRect(x, 0, major ? 1.6 : 1, H);
  }
  ctx.restore();
  for (let y = 0; y <= H; y += 32) {
    const major = Math.round(y / 32) % 5 === 0;
    ctx.fillStyle = rgba(P.Grid, major ? .34 : .17);
    ctx.fillRect(0, y, W, major ? 1.6 : 1);
  }
  ctx.save();
  ctx.globalCompositeOperation = 'multiply';
  ctx.fillStyle = sk.grainPattern;
  ctx.fillRect(0, 0, W, H);
  ctx.restore();
  if (opts.doodles) doodles(t, opts.doodles);
  const g = ctx.createRadialGradient(W / 2, H / 2, H * .35, W / 2, H / 2, H * 1.05);
  g.addColorStop(0, 'rgba(31,38,51,0)');
  g.addColorStop(1, 'rgba(31,38,51,0.16)');
  ctx.fillStyle = g;
  ctx.fillRect(0, 0, W, H);
}
// Pencil doodles (GWSketch.cpp Doodles) scattered round the page, each sketched in when its time comes.
function doodles(t, start = 0) {
  const lead = P.Pencil;
  for (let i = 0; i < 26; ++i) {
    const at = start + .04 * i;
    const show = clamp01((t - at) / .35);
    if (show <= 0) continue;
    let x = (hash01(i * 97 + 11) * .96 + .02) * W, y = (hash01(i * 131 + 7) * .92 + .04) * H;
    // Keep the middle clear for the logo and cards.
    if (Math.abs(x - W / 2) < 520 && Math.abs(y - H / 2) < 300) x = x < W / 2 ? x - 560 : x + 560;
    const s = (18 + 22 * hash01(i * 7 + 3)) * K, kind = i % 5, p = V(x, y), a = .55;
    if (kind === 0) sk.circle(p, s * .5, lead, 1.5 * K, 900 + i, show, a);
    else if (kind === 1) {
      const A = V(x, y - s * .55), B = V(x + s * .5, y + s * .35), C = V(x - s * .5, y + s * .35);
      sk.penTo(A, B, show * 3, lead, 1.5 * K, 900 + i, 1, a); sk.penTo(B, C, show * 3 - 1, lead, 1.5 * K, 910 + i, 1, a); sk.penTo(C, A, show * 3 - 2, lead, 1.5 * K, 920 + i, 1, a);
    } else if (kind === 2) {
      const h = s * .45, c = [V(x - h, y - h), V(x + h, y - h), V(x + h, y + h), V(x - h, y + h)];
      for (let k = 0; k < 4; ++k) sk.penTo(c[k], c[(k + 1) % 4], show * 4 - k, lead, 1.5 * K, 930 + i * 4 + k, 1, a);
    } else if (kind === 3) {
      sk.penTo(V(x - s * .5, y), V(x + s * .5, y), show * 2, lead, 1.5 * K, 950 + i, 1, a);
      sk.penTo(V(x, y - s * .5), V(x, y + s * .5), show * 2 - 1, lead, 1.5 * K, 960 + i, 1, a);
    } else sk.arc(p, s * .45, 0, Math.PI * 1.5 * show, lead, 1.5 * K, a);
  }
}

// The game's logo image, its sticker rim casting a hard paper shadow.
function drawLogo(cx, cy, width, alpha = 1) {
  const h = width * logo.naturalHeight / logo.naturalWidth;
  ctx.save();
  ctx.globalAlpha = alpha * sk.fade;
  ctx.shadowColor = 'rgba(31,38,51,0.22)';
  ctx.shadowOffsetX = 7;
  ctx.shadowOffsetY = 10;
  ctx.drawImage(logo, cx - width / 2, cy - h / 2, width, h);
  ctx.restore();
}

// A rubber stamp: a rough box with heavy letters, slammed down (StampIn).
function stamp(text, cx, cy, size, ink, age, delay = 0, turn = -7, hatch = false) {
  if (age < delay) return;
  sk.stampIn(V(cx, cy), age, delay);
  ctx.save();
  ctx.translate(cx, cy);
  ctx.rotate(turn * Math.PI / 180);
  ctx.translate(-cx, -cy);
  const w = sk.measure(text, size, 'Heavy') + size * 1.0, h = size * 1.55, x = cx - w / 2, y = cy - h / 2;
  if (hatch) {
    // Caution tape round a work-in-progress stamp.
    sk.box(x - 8, y - 8, w + 16, h + 16, P.Marigold, .9);
    ctx.save();
    ctx.beginPath(); ctx.rect(x - 8, y - 8, w + 16, h + 16); ctx.clip();
    ctx.strokeStyle = rgba(P.Ink, .85 * sk.fade); ctx.lineWidth = 9;
    ctx.beginPath();
    for (let k = -h * 2; k < w + h * 2; k += 26) { ctx.moveTo(x + k, y + h + 10); ctx.lineTo(x + k + h + 20, y - 10); }
    ctx.stroke();
    ctx.restore();
    sk.box(x + 6, y + 6, w - 12, h - 12, P.Paper, 1);
  }
  const edge = hatch ? P.Ink : ink;
  sk.pen(V(x, y), V(x + w, y), edge, 4.2 * K, 77, .8, .92);
  sk.pen(V(x + w, y), V(x + w, y + h), edge, 4.2 * K, 78, .8, .92);
  sk.pen(V(x + w, y + h), V(x, y + h), edge, 4.2 * K, 79, .8, .92);
  sk.pen(V(x, y + h), V(x, y), edge, 4.2 * K, 80, .8, .92);
  ctx.globalAlpha = .9;
  sk.label(text, cx, cy - size * .62, size, hatch ? P.Ink : ink, true, 0, 'Heavy');
  ctx.globalAlpha = 1;
  ctx.restore();
  sk.pop();
}

// --- intro (0 - 5.45) -----------------------------------------------------------------------------------------------
const introShapes = [
  { glyph: 0, color: 'Coral', at: V(690, 455), size: 230, delay: .22, from: V(-760, -260), spin: -40 },
  { glyph: 1, color: 'Teal', at: V(965, 505), size: 200, delay: .42, from: V(0, -820), spin: 25 },
  { glyph: 3, color: 'Marigold', at: V(1235, 470), size: 240, delay: .62, from: V(760, -260), spin: 60 },
];
function intro(t) {
  desk(t, { doodles: .05 });
  // Three of the game's shapes bounce onto the page and are knocked away as the logo stamps down.
  for (const s of introShapes) {
    const p = (t - s.delay) / .5;
    if (p <= 0) continue;
    const away = clamp01((t - 1.32) / .3);
    if (away >= 1) continue;
    const land = Ease.outBack(p, 1.8), squash = 1 + .18 * Ease.wobble(t - s.delay - .5, 18, 6);
    let pos = add(s.at, mul(s.from, 1 - land));
    const out = mul(V(s.at.x - 960, s.at.y - 470 - 120), 3.2 * Ease.inCubic(away));
    pos = add(pos, out);
    ctx.save();
    ctx.translate(pos.x, pos.y);
    ctx.rotate((s.spin * (1 - Ease.outCubic(p)) + away * 160) * Math.PI / 180);
    ctx.scale(squash * (1 - away * .4), (2 - squash) * (1 - away * .4));
    ctx.globalAlpha = 1 - away;
    sk.glyph(s.glyph, V(0, 0), s.size, color(s.color), 600 + s.glyph);
    sk.hatchPoly(s.glyph === 0 ? sk.ring(V(0, 0), 24, s.size * .45, 0) : s.glyph === 1 ? sk.ring(V(0, 0), 4, s.size * .575, 45) : sk.ring(V(0, 0), 3, s.size * .5, -90), P.Ink, 14, 2, 610 + s.glyph, .22);
    ctx.restore();
  }
  // The logo: slammed onto the page (2.4x to 1 with a rock) with the camera jolted and paper flying.
  const logoAge = t - 1.3;
  if (logoAge >= 0) {
    const shake = logoAge > .2 && logoAge < .55 ? (1 - (logoAge - .2) / .35) * 9 : 0;
    ctx.save();
    ctx.translate(noise(31, Math.floor(t * 30)) * shake, noise(37, Math.floor(t * 30)) * shake);
    const lift = Ease.inOutCubic((t - 4.4) / .5) * 26;
    sk.stampIn(V(960, 430), logoAge, 0);
    drawLogo(960, 430 - lift, 1380);
    sk.pop();
    confetti(sk, V(330, 430), logoAge - .2, 26, 1000, 9, 11, Math.PI * 1.15, 1.4);
    confetti(sk, V(1590, 430), logoAge - .2, 26, 1000, 9, 12, -Math.PI * .15, 1.4);
    confetti(sk, V(960, 250), logoAge - .22, 18, 800, 8, 13, -Math.PI / 2, 1.2);
    // The subtitle: a strip of tape slapped on, the words dropped onto it.
    const sub = t - 2.0;
    if (sub >= 0) {
      const y = 790 - lift;
      sk.popIn(V(960, y), sub, 0);
      sk.tape(V(960, y), 760 * Ease.outBack(sub / .3), 92, -1.5, P.Coral, .95);
      sk.lettering('开发进度报告', 960, y - 40, 62, P.Ink, 2401, sub - .08);
      sk.pop();
      const date = t - 2.5;
      if (date >= 0) {
        sk.popIn(V(960, y + 84), date, 0);
        sk.label('DEV UPDATE · 2026.11', 960, y + 66, 30, P.InkSoft, true, 0, 'Bold');
        sk.pop();
      }
      // What is inside: three chips that hop in one after another.
      const chips = [['塔防', '已完成', P.Teal], ['联机', '已完成', P.Cobalt], ['后续计划', '清单', P.Marigold]];
      chips.forEach(([a, b, c], i) => {
        const age = t - 3.0 - i * .25;
        if (age < 0) return;
        const aw = sk.measure(a, 32, 'Heavy'), bw = sk.measure(b, 24, 'Bold'), cw = aw + bw + 104;
        const cx = 960 + (i - 1) * 360, cy = 970 - lift, x0 = cx - cw / 2;
        sk.popIn(V(cx, cy), age, 0);
        sk.card(x0, cy - 34, cw, 68, P.Paper, P.Ink, 2500 + i, 1.4, 2.2);
        sk.glyph(i < 2 ? 16 : 7, V(x0 + 34, cy), 34, c, 2510 + i);
        sk.label(a, x0 + 60, cy - 20, 32, P.Ink, false, 0, 'Heavy');
        sk.highlight(x0 + 70 + aw, cy - 15, bw + 16, 30, c, 2520 + i, .35);
        sk.label(b, x0 + 78 + aw, cy - 14, 24, P.InkSoft, false, 0, 'Bold');
        sk.pop();
      });
    }
    ctx.restore();
  }
}

// --- section cards on the page wipe ----------------------------------------------------------------------------------
function sectionCard(id, age, show) {
  const c = TL.Cards[id];
  if (!c || !c.big || show <= 0) return;
  const bigSize = 112 * K, smallSize = 30 * K;
  const bigW = sk.letteringWidth(c.big, bigSize), bigH = bigSize * 1.15;
  const w = Math.max(bigW + 220 * K, 560 * K), h = bigH + 200 * K, x = (W - w) / 2, y = (H - h) * .46;
  sk.popIn(V(W / 2, H / 2), age, 0);
  const keep = sk.fade;
  sk.fade *= show;
  sk.card(x, y, w, h, P.Paper, P.Ink, 1501, 2.4, 2.6 * K);
  sk.tape(V(x + w / 2, y + 4 * K), 150 * K, 30 * K, -3, color(c.accent), .9);
  sk.highlight(x + (w - bigW) / 2 - 12 * K, y + 34 * K + bigH * .6, bigW + 24 * K, bigH * .24, color(c.accent), 1502, .45);
  sk.lettering(c.big, x + w / 2, y + 30 * K, bigSize, P.Ink, 1503, age);
  sk.label(c.small, x + w / 2, y + 44 * K + bigH, smallSize, P.InkSoft, true, w - 60 * K, 'Bold');
  // The loader from the game's title card: a shape gaining sides as it turns, between two of the section's glyphs.
  const turn = sk.clock * 2.2, sides = 3 + Math.floor((sk.clock * 2.5) % 4), spot = V(x + w / 2, y + h - 40 * K);
  sk.polygon(sk.ring(spot, sides, 15 * K, turn * 180 / Math.PI), color(c.accent), P.Ink, 1.8 * K, 1504);
  for (const side of [-1, 1]) sk.glyph(c.glyph, add(spot, V(side * 46 * K, 0)), 16 * K, P.PaperDeep, 1505);
  sk.fade = keep;
  sk.pop();
  if (c.stamp) {
    const hatch = c.stamp === '施工中';
    sk.fade *= show;
    stamp(c.stamp, x + w - 70 * K, y + h - 26 * K, 46, hatch ? P.Ink : P.Danger, age, .42, -8, hatch);
    sk.fade = keep;
  }
}
const CardLead = TL.CardLead;
function wipes(t) {
  for (const w of TL.Wipes) {
    const closeAt = w.at - TL.WipeClose;
    const closedUntil = w.open ?? TL.Clips[0].at;
    let stage = null, age = 0;
    if (between(t, closeAt, w.at)) { stage = 'closing'; age = t - closeAt; }
    else if (between(t, w.at, closedUntil)) stage = 'closed';
    else if (w.open != null && between(t, w.open, w.open + TL.WipeOpen)) { stage = 'opening'; age = t - w.open; }
    if (!stage) continue;
    const cover = wipe(sk, stage, age);
    const show = stage === 'closed' ? 1 : stage === 'opening' ? 1 - clamp01(age / .22) : clamp01((cover - .75) / .25);
    // The hand-over to the game's own wipe (td_open): this card shrinks away just before the plate's card is up.
    const handover = w.open == null ? 1 - clamp01((t - (closedUntil - .16)) / .14) : 1;
    // Like the game's card, it pops in as soon as the tiles have nearly covered the screen.
    sectionCard(w.card, t - (w.at - CardLead), show * handover);
  }
}

// --- footage --------------------------------------------------------------------------------------------------------
function clipAt(t) {
  for (const c of TL.Clips) if (between(t, c.at, c.at + c.len)) return c;
  return null;
}
function clipFrame(c, t) {
  return c.from + (t - c.at) * TL.FPS * c.speed;
}
function drawPlate(img, x, y, w, h, crop) {
  if (!img) { sk.box(x, y, w, h, P.PaperShade); return; }
  const [sx, sy, sw, sh] = crop || [0, 0, img.width, img.height];
  ctx.drawImage(img, sx, sy, sw, sh, x, y, w, h);
}
function footage(c, t, img) {
  const k = clamp01((t - c.at) / c.len), z = c.zoom[0] + (c.zoom[1] - c.zoom[0]) * Ease.inOutCubic(k);
  const w = W * z, h = H * z;
  // The push-in centres on the clip's focus (plate pixels), kept inside the plate.
  const f = c.focus || [W / 2, H / 2];
  const x = Math.min(0, Math.max(W - w, W / 2 - f[0] * z)), y = Math.min(0, Math.max(H - h, H / 2 - f[1] * z));
  drawPlate(img, x, y, w, h);
}
// A caption pinned in the lower-right corner: a paper card with tape, a glyph, a headline and a line under it.
function caption(c, t) {
  const age = t - c.at, left = c.at + c.len - t;
  if (age < 0 || left < -.32) return;
  const titleSize = 46, subSize = 25;
  const tw = sk.letteringWidth(c.title, titleSize), sw = sk.measure(c.sub, subSize, 'Bold');
  const w = Math.max(tw, sw) + 168, h = 136, x = W - w - 56, y = H - h - 52;
  const pivot = V(x + w / 2, y + h / 2), accent = color(c.color);
  if (left < 0) sk.liftOut(pivot, -left, 0, 50); else sk.slideIn(pivot, age, V(220, 30), 0);
  sk.card(x, y, w, h, P.Paper, P.Ink, 3100 + Math.round(c.at * 10), 1.8, 2.4);
  sk.tape(V(x + 34, y + 10), 92, 26, -32, accent, .95);
  sk.glyph(c.glyph, V(x + 64, y + h / 2 + 4), 62, accent, 3200 + Math.round(c.at));
  const sweep = Ease.outCubic((age - .18) / .4);
  sk.highlight(x + 116, y + 26 + titleSize * .62, (tw + 16) * sweep, titleSize * .34, accent, 3300 + Math.round(c.at), .38);
  sk.lettering(c.title, x + 120, y + 22, titleSize, P.Ink, 3400 + Math.round(c.at), age - .05, { left: true, step: .03 });
  sk.label(c.sub, x + 122, y + 84, subSize, P.InkSoft, false, 0, 'Bold', clamp01((age - .3) / .25));
  sk.pop();
}
function badge(b, t) {
  if (!between(t, b.from, b.to + .3)) return;
  const age = t - b.from, left = b.to - t, accent = color(b.accent);
  const tw = sk.measure(b.text, 30, 'Heavy'), stw = sk.measure(b.state, 21, 'Bold');
  const w = tw + stw + 118, x = 34, y = 26, h = 60;
  if (left < 0) sk.liftOut(V(x + w / 2, y + h / 2), -left, 0, 40); else sk.slideIn(V(x + w / 2, y + h / 2), age, V(-200, 0), 0);
  sk.card(x, y, w, h, P.Paper, P.Ink, 3600 + Math.round(b.from), 1.4, 2.2);
  sk.tape(V(x + w - 16, y + 6), 54, 18, 34, accent, .9);
  sk.glyph(16, V(x + 32, y + h / 2), 36, accent, 3610);
  sk.label(b.text, x + 58, y + 11, 30, P.Ink, false, 0, 'Heavy');
  sk.highlight(x + 70 + tw, y + 16, stw + 18, 30, accent, 3620, .32);
  sk.label(b.state, x + 79 + tw, y + 18, 21, P.InkSoft, false, 0, 'Bold');
  sk.pop();
}

// --- the systems still being built (37 - 53.6) -----------------------------------------------------------------------
const SheetCrop = [262, 126, 1404, 790];
function photo(img, x, y, w, h, turn, seed, label, crop, age, tapeColor = P.Mint) {
  ctx.save();
  ctx.translate(x + w / 2, y + h / 2);
  ctx.rotate(turn * Math.PI / 180);
  ctx.translate(-(x + w / 2), -(y + h / 2));
  const m = 16, bottom = label ? 58 : 16;
  sk.card(x - m, y - m, w + 2 * m, h + m + bottom, '#FFFDF7', P.Ink, seed, 2.2, 2.2);
  if (sk.fade > .02) {
    ctx.save();
    ctx.globalAlpha = sk.fade;
    drawPlate(img, x, y, w, h, crop);
    ctx.restore();
  }
  sk.pen(V(x, y), V(x + w, y), P.Ink, 1.6, seed + 5, .4);
  sk.pen(V(x + w, y), V(x + w, y + h), P.Ink, 1.6, seed + 6, .4);
  sk.pen(V(x + w, y + h), V(x, y + h), P.Ink, 1.6, seed + 7, .4);
  sk.pen(V(x, y + h), V(x, y), P.Ink, 1.6, seed + 8, .4);
  sk.tape(V(x + 30, y - 6), 120, 34, -30, tapeColor, .9);
  sk.tape(V(x + w - 30, y - 6), 120, 34, 28, tapeColor, .9);
  if (label) sk.label(label, x + 8, y + h + 14, 28, P.InkSoft, false, 0, 'Bold', clamp01((age - .5) / .3));
  ctx.restore();
}
// How far through its exit a spread is (0 = on the desk, 1 = gone off the left edge).
function spreadLeaving(s, t) {
  return clamp01((t - (s.at + TL.SystemLen - TL.SystemExit)) / (TL.SystemExit + TL.SystemOverlap));
}
function systemSpread(s, t, imgs) {
  const age = t - s.at, accent = color(s.accent);
  const leaving = spreadLeaving(s, t);
  if (leaving >= 1) return;
  // The spread is shoved off the desk to the left (a little wind-up first) while the next one drops in on top; it
  // travels far enough that its notebook card clears the frame before it is dropped. Quadratic, so even its last
  // frames move ~220 px instead of whipping.
  const shove = 1.4 * leaving * leaving - .4 * leaving;
  ctx.save();
  ctx.translate(-shove * 2080, Ease.inCubic(leaving) * 60);
  ctx.rotate(-Ease.inCubic(leaving) * .05);
  // A slow push-in while it is read, so the hold after the stamp never stands still.
  const push = 1 + .03 * Ease.smooth(age / TL.SystemLen);
  ctx.translate(W / 2, H / 2);
  ctx.scale(push, push);
  ctx.translate(-W / 2, -H / 2);
  // The photo of the game.
  sk.dropIn(V(660, 470), age, 0, 160);
  photo(imgs.main, 96, 150, 1110, 625, -1.6, 4100 + s.at, `游戏内 · ${s.title.replace('系统', '')}`, s.main.sheet ? SheetCrop : (s.main.crop || null), age);
  sk.pop();
  if (s.inset) {
    sk.dropIn(V(1030, 230), age, .55, 120);
    photo(imgs.inset, 800, 96, 460, 259, 3.4, 4200 + s.at, null, s.inset.sheet ? SheetCrop : (s.inset.crop || null), age, P.Sticky);
    sk.pop();
  }
  // The notebook card: lined paper with a margin, the system's name lettered on it, three pencil lines and a stamp.
  const x = 1290, y = 150, w = 560, h = 760;
  sk.dropIn(V(x + w / 2, y + h / 2), age, .12, 140);
  sk.card(x, y, w, h, '#FBF6E8', P.Ink, 4300 + s.at, 2.4, 2.4);
  for (let ly = y + 200; ly < y + h - 30; ly += 56) sk.line(V(x + 18, ly), V(x + w - 18, ly), P.Grid, 1.3, .55);
  sk.line(V(x + 64, y + 14), V(x + 64, y + h - 14), P.Coral, 1.6, .5);
  sk.tape(V(x + w / 2, y + 2), 160, 34, -2, accent, .9);
  sk.glyph(s.glyph, V(x + w - 70, y + 92), 74, accent, 4400 + s.at);
  const titleSize = 70, tw = sk.letteringWidth(s.title, titleSize);
  sk.highlight(x + 74, y + 58 + titleSize * .62, (tw + 20) * Ease.outCubic((age - .3) / .4), titleSize * .3, accent, 4500 + s.at, .42);
  sk.lettering(s.title, x + 82, y + 52, titleSize, P.Ink, 4600 + s.at, age - .2, { left: true });
  sk.label(s.en, x + 86, y + 136, 24, P.InkSoft, false, 0, 'Bold', clamp01((age - .45) / .3));
  s.lines.forEach((line, i) => {
    const a = age - .55 - i * .22;
    if (a < 0) return;
    const ly = y + 212 + i * 112;
    sk.glyph(3, V(x + 92, ly + 20), 20, accent, 4700 + i);
    // Written left to right like a pen.
    ctx.save();
    ctx.beginPath();
    ctx.rect(x + 100, ly - 10, 30 + (w - 130) * Ease.outCubic(a / .45), 120);
    ctx.clip();
    wrap(line, x + 116, ly, 30, w - 150, P.Ink, 'Bold');
    ctx.restore();
  });
  stamp('施工中', x + w - 150, y + h - 92, 44, P.Ink, age, 1.1, -9, true);
  // A loader that keeps turning: the update is under way.
  const spot = V(x + 104, y + h - 86), sides = 3 + Math.floor((sk.clock * 2.5) % 4);
  sk.polygon(sk.ring(spot, sides, 20, sk.clock * 126), accent, P.Ink, 2, 4800);
  sk.label('更新中…', x + 136, y + h - 106, 30, P.InkSoft, false, 0, 'Bold', clamp01((age - 1.3) / .3));
  sk.pop();
  // Which of the four this is.
  TL.Systems.forEach((_, i) => {
    const cx = W / 2 - 75 + i * 50, cy = 1018, on = TL.Systems[i] === s;
    sk.glyph(4, V(cx, cy), on ? 34 : 24, on ? accent : P.PaperDeep, 4900 + i);
  });
  ctx.restore();
}
function wrap(text, x, y, size, width, ink, face) {
  // Breaks between CJK characters (never before closing punctuation), like the HUD's Paragraph.
  const lines = [];
  let line = '';
  for (const ch of text) {
    const next = line + ch;
    if (sk.measure(next, size, face) > width && line && !'，。、；：！？）」』…·'.includes(ch)) { lines.push(line); line = ch; }
    else line = next;
  }
  if (line) lines.push(line);
  lines.forEach((l, i) => sk.label(l, x, y + i * size * 1.38, size, ink, false, 0, face));
  return lines.length;
}
// Everything on the systems' desk but the desk itself: the heading and whichever spreads are on it (the one leaving
// under the one arriving). The shopping list draws this too while the last spread is still sliding away.
function systemsLayer(t, imgs) {
  // A heading pinned above the spreads; it is peeled off as the last spread goes.
  const head = t - TL.Systems[0].at, peel = t - (TL.ListStart - .26);
  if (peel < .3) {
    if (peel > 0) sk.liftOut(V(240, 70), peel, 0, 50); else sk.slideIn(V(240, 70), head, V(-260, 0), .2);
    sk.tape(V(232, 66), 360, 62, -2, P.Marigold, .9);
    sk.label('即将更新 · 4 个系统', 232, 44, 34, P.Ink, true, 0, 'Heavy');
    sk.pop();
  }
  TL.Systems.forEach((s, i) => {
    if (t >= s.at && spreadLeaving(s, t) < 1) systemSpread(s, t, imgs[i] || {});
  });
}
function systems(t, imgs) {
  desk(t, { paper: '#F3ECDB' });
  systemsLayer(t, imgs);
}

// --- the development plan: a shopping list (53.6 - 76) ---------------------------------------------------------------
const Pad = { x: 610, y: 34, w: 1210, h: 1010, turn: -1.1 };
const Rows = { top: 214, step: 94 };
function pencil(tip, angle, length, t) {
  // The pencil that writes the list: hexagonal marigold body, kraft cone, graphite point and a coral eraser.
  ctx.save();
  ctx.translate(tip.x, tip.y);
  ctx.rotate(angle);
  const r = 15;
  sk.fill([V(0, 0), V(42, -r), V(42, r)], P.Kraft);
  sk.fill([V(0, 0), V(13, -4.6), V(13, 4.6)], P.Ink);
  sk.fill([V(42, -r), V(length, -r), V(length, r), V(42, r)], P.Marigold);
  sk.line(V(42, -r * .33), V(length, -r * .33), '#C98A1E', 2);
  sk.line(V(42, r * .33), V(length, r * .33), '#FFD27A', 2);
  sk.fill([V(length, -r), V(length + 22, -r), V(length + 22, r), V(length, r)], P.PaperDeep);
  sk.fill([V(length + 22, -r), V(length + 52, -r * .9), V(length + 52, r * .9), V(length + 22, r)], P.Coral);
  sk.polygon([V(0, 0), V(42, -r), V(length + 52, -r * .9), V(length + 52, r * .9), V(42, r)], null, P.Ink, 2.2, 5100);
  ctx.restore();
}
// The heading is written by the pencil (a left-to-right reveal with the point on its edge), like the items.
const TitleText = '开发计划', TitleSize = 74;
function titleReveal(t) {
  return Ease.smooth((t - TL.ListTitle.write) / (TL.ListTitle.done - TL.ListTitle.write));
}
function titleWriting(t) {
  const r = titleReveal(t), tw = sk.letteringWidth(TitleText, TitleSize);
  return V(Pad.x + 160 + tw * r, Pad.y + 92 + TitleSize * .8);
}
function shoppingList(t, beneath) {
  // Once the list is written the camera eases in on it while the music asks its question, up to the outro's wipe.
  const push = 1 + .045 * Ease.inOutCubic((t - 72.9) / 2.6);
  ctx.save();
  ctx.translate(W / 2, H / 2);
  ctx.scale(push, push);
  ctx.translate(-W / 2, -H / 2);
  desk(t, { paper: '#F3ECDB' });
  // The last system spread still sliding off the desk under the notepad as it drops in.
  if (beneath) beneath();
  const age = t - TL.ListStart;
  // The notepad lands on the desk.
  ctx.save();
  sk.dropIn(V(Pad.x + Pad.w / 2, Pad.y + Pad.h / 2), age, 0, 220);
  ctx.translate(Pad.x + Pad.w / 2, Pad.y + Pad.h / 2);
  ctx.rotate(Pad.turn * Math.PI / 180);
  ctx.translate(-(Pad.x + Pad.w / 2), -(Pad.y + Pad.h / 2));
  const { x, y, w, h } = Pad;
  sk.card(x, y, w, h, '#FFFCF2', P.Ink, 5200, 3.0, 2.6);
  // Torn-off sheets under the top one, a binding strip with punched holes.
  sk.box(x + 6, y, w - 12, 64, P.PaperDeep, .9);
  sk.pen(V(x, y + 64), V(x + w, y + 64), P.Ink, 2.2, 5201, .5);
  for (let k = 0; k < 14; ++k) {
    const hx = x + 70 + k * (w - 140) / 13;
    sk.disc(V(hx, y + 32), 10, P.Night, 16, .85);
  }
  for (let i = 0; i < 8; ++i) {
    const ly = y + Rows.top + i * Rows.step + Rows.step - 4;
    sk.line(V(x + 20, ly), V(x + w - 20, ly), P.Grid, 1.4, .6);
  }
  sk.line(V(x + 124, y + 70), V(x + 124, y + h - 12), P.Coral, 1.8, .55);
  // The heading: written by the pencil, then swept with the highlighter and labelled.
  const title = TitleText, ts = TitleSize, tw = sk.letteringWidth(title, ts), done = TL.ListTitle.done;
  sk.highlight(x + 150, y + 96 + ts * .64, (tw + 30) * Ease.outCubic((t - done - .05) / .4), ts * .32, P.Marigold, 5210, .5);
  const reveal = titleReveal(t);
  if (reveal > 0) {
    ctx.save();
    ctx.beginPath();
    ctx.rect(x + 150, y + 70, 14 + (tw + 10) * reveal, 140);
    ctx.clip();
    sk.lettering(title, x + 160, y + 92, ts, P.Ink, 5211, -1, { left: true });
    ctx.restore();
  }
  sk.label('购物清单 · SHOPPING LIST', x + 168 + tw + 30, y + 128, 28, P.InkSoft, false, 0, 'Bold', clamp01((t - done - .15) / .3));
  // What is already in the bag: a receipt stub taped over the pad's corner, half off the page.
  {
    const ra = t - done - .3, rx = x + w - 170, ry = y - 6;
    if (ra > 0) {
      sk.popIn(V(rx + 115, ry + 60), ra, 0);
      ctx.save();
      ctx.translate(rx + 115, ry + 60); ctx.rotate(.07); ctx.translate(-(rx + 115), -(ry + 60));
      sk.card(rx, ry, 230, 122, P.Sticky, P.Ink, 5220, 1.6, 2);
      sk.tape(V(rx + 115, ry + 2), 96, 26, -4, P.Coral, .8);
      sk.label('已入手', rx + 18, ry + 14, 24, P.InkSoft, false, 0, 'Bold');
      sk.glyph(16, V(rx + 32, ry + 70), 30, P.Teal, 5221); sk.label('塔防', rx + 52, ry + 53, 30, P.Ink, false, 0, 'Heavy');
      sk.glyph(16, V(rx + 132, ry + 70), 30, P.Teal, 5222); sk.label('联机', rx + 152, ry + 53, 30, P.Ink, false, 0, 'Heavy');
      ctx.restore();
      sk.pop();
    }
  }
  // The items, written one after another.
  let penAt = null;
  TL.ListItems.forEach((it, i) => {
    const at = TL.listItemAt(i), a = t - at;
    if (a < 0) return;
    const ry = y + Rows.top + i * Rows.step;
    // The checkbox: four pencil strokes.
    const bx = x + 50, by = ry + 12, bs = 40;
    const corners = [V(bx, by), V(bx + bs, by), V(bx + bs, by + bs), V(bx, by + bs)];
    for (let k = 0; k < 4; ++k) sk.penTo(corners[k], corners[(k + 1) % 4], a / .07 - k, P.Ink, 2.6, 5300 + i * 4 + k, .9);
    // The number and the title, written left to right; the pencil rides the edge.
    sk.label(`${i + 1}.`, x + 142, ry + 10, 34, P.Ink, false, 0, 'Heavy', clamp01((a - .15) / .12));
    const tSize = 38, titleW = sk.measure(it.title, tSize, 'Heavy');
    const reveal = Ease.outCubic((a - .2) / .55);
    if (reveal > 0) {
      ctx.save();
      ctx.beginPath();
      ctx.rect(x + 196, ry - 10, (titleW + 10) * reveal, 70);
      ctx.clip();
      sk.label(it.title, x + 198, ry + 8, tSize, P.Ink, false, 0, 'Heavy');
      ctx.restore();
      if (reveal < 1) penAt = V(x + 198 + titleW * reveal, ry + 40);
    }
    // The note in pencil under it.
    const n = clamp01((a - .62) / .35);
    if (n > 0) {
      ctx.save();
      ctx.beginPath();
      ctx.rect(x + 196, ry + 50, 900 * n, 44);
      ctx.clip();
      sk.label(it.note, x + 200, ry + 54, 25, P.InkSoft, false, 0, 'Body');
      ctx.restore();
    }
    // The quantity, scribbled at the end of the line.
    const q = a - .8;
    if (q > 0) {
      const qx = x + w - 120, qy = ry + 30;
      sk.popIn(V(qx, qy), q, 0);
      ctx.save();
      ctx.translate(qx, qy); ctx.rotate(-.06); ctx.translate(-qx, -qy);
      sk.label(it.qty, qx, qy - 22, 36, P.Coral, true, 0, 'Heavy');
      ctx.restore();
      sk.pop();
      sk.glyph(it.glyph, V(x + w - 230, ry + 32), 34, [P.Coral, P.Teal, P.Marigold, P.Cobalt, P.Magenta, P.Leaf, P.Violet, P.Mint][i], 5400 + i);
    }
  });
  // A loop round "what players want": the list stays open for them.
  const loop = clamp01((t - CircleAt) / .6);
  if (loop > 0) {
    const pts = [];
    for (let k = 0; k <= 64 * loop; ++k) pts.push(circlePoint(k / 64));
    ctx.strokeStyle = rgba(P.Coral, .9);
    ctx.lineWidth = 4;
    ctx.lineCap = ctx.lineJoin = 'round';
    ctx.beginPath();
    pts.forEach((p, k) => (k ? ctx.lineTo(p.x, p.y) : ctx.moveTo(p.x, p.y)));
    ctx.stroke();
  }
  ctx.restore();
  sk.pop();
  // The pencil: a right hand's, the point on the writing and the body off to the lower right. It walks from the end of
  // one line to the start of the next, loops item 3 and leaves.
  const pen = listPen(t, penAt);
  if (pen) {
    const at = padToScreen(pen.at);
    const bob = pen.writing ? Math.sin(t * 38) * 3 : 0;
    pencil(V(at.x + bob, at.y + bob * .5 - pen.lift), .82 + .06 * Math.sin(t * 7), 250, t);
  }
  // The sticky note: tell us what to add.
  const noteAge = t - 72.4;
  if (noteAge > 0) {
    const nx = 96, ny = 640;
    sk.dropIn(V(nx + 220, ny + 140), noteAge, 0, 120);
    ctx.save();
    ctx.translate(nx + 220, ny + 140); ctx.rotate(-.05); ctx.translate(-(nx + 220), -(ny + 140));
    sk.card(nx, ny, 440, 270, P.Sticky, P.Ink, 5600, 2.2, 2.2);
    sk.tape(V(nx + 220, ny + 4), 150, 32, 3, P.Teal, .85);
    sk.lettering('你想要什么？', nx + 220, ny + 46, 50, P.Ink, 5601, noteAge - .15);
    sk.label('留言告诉我们', nx + 220, ny + 128, 32, P.InkSoft, true, 0, 'Bold', clamp01((noteAge - .5) / .3));
    sk.label('它会被写进这张清单 ↗', nx + 220, ny + 176, 30, P.Coral, true, 0, 'Heavy', clamp01((noteAge - .75) / .3));
    ctx.restore();
    sk.pop();
  }
  // Left of the pad before the note arrives: the three shapes keep an eye on the list.
  const watch = clamp01((t - TL.ListStart - .4) / .5) * (1 - clamp01((t - 72.2) / .3));
  if (watch > 0) {
    const shapes = [[0, P.Coral, V(250, 330), 150], [1, P.Teal, V(420, 470), 130], [3, P.Marigold, V(230, 560), 150]];
    shapes.forEach(([g, c, p, s], i) => {
      const hop = Math.abs(Math.sin(t * 4 + i * 1.3)) * 14;
      sk.glyph(g, V(p.x, p.y - hop), s * Ease.outBack(clamp01((t - TL.ListStart - .4 - i * .12) / .4)), c, 5700 + i, watch);
    });
  }
  ctx.restore();
}
const CircleAt = 72.55;
function circlePoint(u) {
  const ry = Pad.y + Rows.top + 2 * Rows.step + 36, cx = Pad.x + 440, a = -2.5 + u * Math.PI * 2.12;
  const wob = 1 + .025 * Math.sin(u * 17);
  return V(cx + Math.cos(a) * 300 * wob, ry + Math.sin(a) * 66 * wob);
}
function rowStart(i) {
  return V(Pad.x + 198, Pad.y + Rows.top + i * Rows.step + 40);
}
function rowEnd(i) {
  const it = TL.ListItems[i];
  return V(Pad.x + 198 + sk.measure(it.title, 38, 'Heavy'), Pad.y + Rows.top + i * Rows.step + 40);
}
function listPen(t, writingAt) {
  if (writingAt) return { at: writingAt, writing: true, lift: 0 };
  const n = TL.ListItems.length, first = TL.listItemAt(0) + .2, T = TL.ListTitle;
  const hop = (a, b, u) => ({ at: V(a.x + (b.x - a.x) * Ease.inOutCubic(u), a.y + (b.y - a.y) * Ease.inOutCubic(u)), writing: false, lift: Math.sin(Math.PI * clamp01(u)) * 26 });
  if (t < first) {
    // In from the lower right as the notepad lands, the heading written, a breath over it, then down to the first line.
    if (t < T.penIn) return null;
    if (t < T.write) return hop(V(Pad.x + Pad.w + 200, Pad.y + Pad.h + 260), titleWriting(T.write), Ease.outCubic((t - T.penIn) / (T.write - T.penIn)));
    if (t < T.done) return { at: titleWriting(t), writing: true, lift: 0 };
    const leave = first - 1.0;
    if (t < leave) {
      // Lifted off the paper, bobbing like a hand thinking about what to write first.
      const u = clamp01((t - T.done) / .25);
      return { at: add(titleWriting(T.done), V(18 * u, -6 * u + 5 * Math.sin((t - T.done) * 7))), writing: false, lift: 22 * Ease.outCubic(u) };
    }
    const from = add(titleWriting(T.done), V(18, -6 + 5 * Math.sin((leave - T.done) * 7)));
    const u = (t - leave) / 1.0;
    const p = hop(from, rowStart(0), u);
    p.lift = 22 * (1 - Ease.smooth(u)) + p.lift;
    return p;
  }
  for (let i = 0; i < n; ++i) {
    const end = TL.listItemAt(i) + .75, next = i + 1 < n ? TL.listItemAt(i + 1) + .2 : CircleAt;
    if (t >= end && t < next) {
      const target = i + 1 < n ? rowStart(i + 1) : circlePoint(0);
      return hop(rowEnd(i), target, (t - end) / (next - end));
    }
  }
  if (t < CircleAt + .6) return { at: circlePoint(clamp01((t - CircleAt) / .6)), writing: true, lift: 0 };
  const u = clamp01((t - CircleAt - .6) / .8);
  return u < 1 ? hop(circlePoint(1), V(Pad.x + Pad.w + 260, Pad.y + Pad.h + 300), u) : null;
}
function padToScreen(p) {
  const cx = Pad.x + Pad.w / 2, cy = Pad.y + Pad.h / 2, r = Pad.turn * Math.PI / 180;
  const dx = p.x - cx, dy = p.y - cy;
  return V(cx + dx * Math.cos(r) - dy * Math.sin(r), cy + dx * Math.sin(r) + dy * Math.cos(r));
}

// --- outro (76 - 82) -------------------------------------------------------------------------------------------------
// The outro opens on the game's title page: its live brawl under a paper wash, the logo stamped over it.
function outroBase(t, img) {
  if (!img) { desk(t, { doodles: 76.4 }); return; }
  const k = clamp01((t - TL.S.outro) / (TL.END - TL.S.outro)), z = 1.04 + .04 * k;
  drawPlate(img, (W - W * z) / 2, (H - H * z) / 2, W * z, H * z);
  const g = ctx.createRadialGradient(W / 2, H * .45, 120, W / 2, H * .45, W * .62);
  g.addColorStop(0, rgba(P.Paper, .82));
  g.addColorStop(.55, rgba(P.Paper, .55));
  g.addColorStop(1, rgba(P.Paper, .18));
  ctx.fillStyle = g;
  ctx.fillRect(0, 0, W, H);
}
function outroTop(t) {
  const age = t - 76.1;
  if (age < 0) return;
  sk.stampIn(V(960, 420), age, 0);
  drawLogo(960, 420, 1300);
  sk.pop();
  confetti(sk, V(380, 420), age - .15, 30, 1100, 9, 41, Math.PI * 1.15, 1.4);
  confetti(sk, V(1540, 420), age - .15, 30, 1100, 9, 42, -Math.PI * .15, 1.4);
  const sub = t - 76.9;
  if (sub > 0) {
    sk.popIn(V(960, 770), sub, 0);
    sk.tape(V(960, 770), 560 * Ease.outBack(sub / .3), 96, -1.5, P.Teal, .9);
    sk.lettering('敬请期待', 960, 728, 66, P.Ink, 7001, sub - .08);
    sk.pop();
  }
  const fine = t - 77.6;
  if (fine > 0) sk.label('画面为开发中版本，不代表最终品质', 960, 1000, 24, P.InkSoft, true, 0, 'Body', clamp01(fine / .4));
  // The last half second settles to the paper.
  const out = clamp01((t - 81.5) / .5);
  if (out > 0) sk.box(0, 0, W, H, P.Paper, out);
}

// --- one frame --------------------------------------------------------------------------------------------------------
function systemFrame(s, t) {
  const age = Math.max(0, t - s.at);
  return { main: { shot: s.main.shot, frame: s.main.from + age * TL.FPS }, inset: s.inset ? { shot: s.inset.shot, frame: s.inset.from + age * TL.FPS } : null };
}
async function renderFrame(t) {
  sk.setClock(t);
  sk.fade = 1;
  const clip = t >= TL.Clips[0].at && t < TL.S.next + 1.0 ? clipAt(t) : null;
  const clipImg = clip ? await plateImage(clip.shot, clipFrame(clip, t)) : null;
  let sysImgs = null;
  if (between(t, TL.Systems[0].at - .01, TL.ListStart + TL.SystemOverlap)) {
    sysImgs = await Promise.all(TL.Systems.map(async s => {
      if (!between(t, s.at - .01, s.at + TL.SystemLen + TL.SystemOverlap)) return {};
      const f = systemFrame(s, t);
      return { main: await plateImage(f.main.shot, f.main.frame), inset: f.inset ? await plateImage(f.inset.shot, f.inset.frame) : null };
    }));
  }
  const outroImg = t >= TL.S.outro ? await plateImage(TL.OutroBrawl.shot, TL.OutroBrawl.from + (t - TL.S.outro) * TL.FPS) : null;
  ctx.setTransform(1, 0, 0, 1, 0, 0);
  ctx.globalAlpha = 1;
  if (t < TL.Clips[0].at) intro(t);
  else if (t < TL.Systems[0].at) {
    if (clip) footage(clip, t, clipImg);
    else desk(t);
  } else if (t < TL.ListStart) systems(t, sysImgs);
  else if (t < TL.S.outro + .75) shoppingList(t, t < TL.ListStart + TL.SystemOverlap ? () => systemsLayer(t, sysImgs) : null);
  else outroBase(t, outroImg);
  for (const c of TL.Captions) caption(c, t);
  for (const b of TL.Badges) badge(b, t);
  wipes(t);
  if (t >= TL.S.outro) outroTop(t);
}

// --- the cover (a thumbnail for uploads) -------------------------------------------------------------------------------
async function renderCover() {
  const left = await plateImage('td_wave', 150), right = await plateImage('coop_throw', 110);
  sk.setClock(5);
  sk.fade = 1;
  ctx.setTransform(1, 0, 0, 1, 0, 0);
  desk(5, { doodles: 0 });
  photo(left, 80, 600, 740, 416, -4, 8100, '塔防', null, 9, P.Mint);
  photo(right, 1100, 592, 740, 416, 3.5, 8200, '联机', null, 9, P.Sticky);
  stamp('已完成', 690, 950, 40, P.Danger, 9, 0, -8);
  stamp('已完成', 1720, 950, 40, P.Danger, 9, 0, 6);
  drawLogo(960, 230, 1240);
  sk.tape(V(960, 470), 820, 104, -1.5, P.Coral, .95);
  sk.lettering('开发进度报告', 960, 424, 70, P.Ink, 2401, -1);
  sk.label('DEV UPDATE · 2026.11 · 后续计划清单', 960, 528, 30, P.InkSoft, true, 0, 'Bold');
}

// --- drivers ---------------------------------------------------------------------------------------------------------
async function post(path, body) {
  for (let tries = 0; tries < 5; ++tries) {
    try { const r = await fetch(path, { method: 'POST', body }); if (r.ok) return; } catch { }
    await new Promise(r => setTimeout(r, 200));
  }
}
async function main() {
  await loadAssets();
  if (mode === 'video') {
    const from = Math.round(parseFloat(params.get('from') || '0') * TL.FPS);
    const toParam = parseFloat(params.get('to') || '-1');
    const to = Math.round((toParam < 0 ? TL.END : toParam) * TL.FPS);
    for (let f = from; f < to; ++f) {
      await renderFrame(f / TL.FPS);
      await post(`/frame?i=${f}`, ctx.getImageData(0, 0, W, H).data);
    }
    await post('/done', `rendered ${to - from} frames`);
  } else if (mode === 'cover') {
    await renderCover();
    await post('/still?name=cover', await new Promise(r => canvas.toBlob(r, 'image/png')));
    await post('/done', 'cover');
  } else if (mode === 'stills' || mode === 'sheet') {
    let times = (params.get('times') || '').split(',').filter(Boolean).map(Number);
    if (mode === 'sheet') {
      const from = parseFloat(params.get('from')), to = parseFloat(params.get('to')), count = parseInt(params.get('count'), 10);
      times = Array.from({ length: count }, (_, i) => from + (to - from) * i / Math.max(1, count - 1));
    }
    for (const [i, t] of times.entries()) {
      await renderFrame(t);
      const blob = await new Promise(r => canvas.toBlob(r, 'image/png'));
      const name = mode === 'sheet' ? `sheet-${String(i).padStart(3, '0')}` : `t${t.toFixed(2)}`;
      await post(`/still?name=${name}`, blob);
    }
    await post('/done', `${times.length} stills`);
  } else {
    document.body.classList.add('preview');
    const seek = document.getElementById('seek'), label = document.getElementById('time'), play = document.getElementById('play');
    seek.max = TL.END;
    let playing = false, busy = false;
    const show = async () => { if (busy) return; busy = true; const t = parseFloat(seek.value); await renderFrame(t); label.textContent = t.toFixed(2); busy = false; };
    seek.oninput = show;
    play.onclick = () => { playing = !playing; play.textContent = playing ? 'pause' : 'play'; };
    const step = async () => { if (playing && !busy) { seek.value = (parseFloat(seek.value) + 1 / TL.FPS) % TL.END; await show(); } requestAnimationFrame(step); };
    seek.value = params.get('t') || '0';
    await show();
    step();
  }
}
main().catch(e => { log('ERROR ' + (e && e.stack || e)); post('/done', 'error'); });
