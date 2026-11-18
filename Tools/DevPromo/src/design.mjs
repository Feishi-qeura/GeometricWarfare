// Original Sketchbook & Craft-Paper Design System for Geometric Warfare Dev Promo.
// Colors match Source/GeometricWarfare/Standalone/GWPalette.h.

export const C = {
  Paper: '#F6F0E1',
  PaperWarm: '#FFFDF7',
  PaperShade: '#EADFC8',
  PaperDeep: '#D9CAAC',
  Desk: '#E5D9C3',
  DeskWood: '#C9B393',
  Ink: '#1F2633',
  InkSoft: '#4A5262',
  Pencil: '#8C909A',
  Grid: '#9DB5D3',
  Coral: '#E8553F',
  Cobalt: '#2F6BD8',
  Marigold: '#F2A93B',
  Teal: '#26A796',
  Mint: '#9AD8CB',
  Plum: '#4B3F72',
  Magenta: '#B23A7A',
  Night: '#2A2140',
  Violet: '#8E5CD9',
  Frost: '#5EC8E8',
  Fire: '#FF6A2B',
  Volt: '#6E6BFF',
  Spark: '#FFD84A',
  Leaf: '#4DBA5C',
  Kraft: '#D6BC90',
  Danger: '#E23A33',
  Ruler: '#C5CCD6',
  RulerEdge: '#8E98A8',
  Sticky: '#FFF2B0',
};

export const SEATS = ['#E8553F', '#2F6BD8', '#D9A10E', '#3A9E45', '#E0559B', '#17A3BF', '#8E5B2E', '#94B81A'];

const rgbCache = new Map();
function parseHex(hex) {
  let v = rgbCache.get(hex);
  if (!v) {
    const h = hex.replace('#', '');
    v = [parseInt(h.slice(0, 2), 16), parseInt(h.slice(2, 4), 16), parseInt(h.slice(4, 6), 16)];
    rgbCache.set(hex, v);
  }
  return v;
}

export function rgba(hex, alpha = 1) {
  const [r, g, b] = parseHex(hex);
  return `rgba(${r},${g},${b},${Math.max(0, Math.min(1, alpha))})`;
}

export const clamp01 = x => Math.max(0, Math.min(1, x));

export const ease = {
  smooth: t => { t = clamp01(t); return t * t * (3 - 2 * t); },
  outCubic: t => { t = 1 - clamp01(t); return 1 - t * t * t; },
  inCubic: t => { t = clamp01(t); return t * t * t; },
  inOutCubic: t => { t = clamp01(t); return t < 0.5 ? 4 * t * t * t : 1 - Math.pow(-2 * t + 2, 3) / 2; },
  outBack: (t, s = 1.6) => { t = clamp01(t) - 1; return 1 + (s + 1) * t * t * t + s * t * t; },
  inBack: (t, s = 1.4) => { t = clamp01(t); return (s + 1) * t * t * t - s * t * t; },
  elasticOut: t => {
    t = clamp01(t);
    if (t === 0 || t === 1) return t;
    return Math.pow(2, -9 * t) * Math.sin((t * 10 - 0.75) * (Math.PI * 2 / 3)) + 1;
  },
};

export function rand01(seed) {
  let x = (seed >>> 0) || 1;
  x ^= x << 13;
  x ^= x >>> 17;
  x ^= x << 5;
  return (x >>> 0) / 4294967296;
}

export function signedNoise(seed, i) {
  return rand01(Math.imul(seed | 0, 747796405) + Math.imul(i | 0, 2891336453) + 997) * 2 - 1;
}

export function fontSpec(px, weight = 700, family = 'GWCJK') {
  return `${weight} ${px.toFixed(1)}px GWLatin, ${family}, sans-serif`;
}

function createPaperGrain() {
  const off = new OffscreenCanvas(256, 256);
  const g = off.getContext('2d');
  const img = g.createImageData(256, 256);
  let s = 0x6D2B79F5;
  for (let i = 0; i < 256 * 256; i++) {
    s ^= s << 13; s ^= s >>> 17; s ^= s << 5;
    const n = ((s >>> 0) % 1000) / 1000;
    const v = 255 - Math.floor(n * n * 24);
    img.data[i * 4] = v;
    img.data[i * 4 + 1] = v - 2;
    img.data[i * 4 + 2] = v - 5;
    img.data[i * 4 + 3] = 255;
  }
  g.putImageData(img, 0, 0);
  return off;
}

export class PaperKit {
  constructor(ctx, w = 1920, h = 1080) {
    this.ctx = ctx;
    this.W = w;
    this.H = h;
    this.grain = createPaperGrain();
    this.grainPattern = ctx.createPattern(this.grain, 'repeat');
    this.boil = 0;
  }

  setClock(t) {
    // 6 fps subtle hand-drawn line boil
    this.boil = Math.floor(t * 6);
  }

  measure(text, px, weight = 700) {
    this.ctx.font = fontSpec(px, weight);
    return this.ctx.measureText(text).width;
  }

  text(str, x, y, px, color = C.Ink, opts = {}) {
    const { weight = 700, align = 'left', baseline = 'top', maxWidth = 0, alpha = 1 } = opts;
    if (alpha <= 0.01) return;
    const c = this.ctx;
    c.save();
    c.globalAlpha *= alpha;
    let size = px;
    c.font = fontSpec(size, weight);
    if (maxWidth > 0) {
      const w = c.measureText(str).width;
      if (w > maxWidth) size = px * (maxWidth / w);
      c.font = fontSpec(size, weight);
    }
    c.textAlign = align;
    c.textBaseline = baseline;
    c.fillStyle = color;
    c.fillText(str, x, y);
    c.restore();
  }

  wrapText(str, x, y, px, maxW, lineH, color = C.Ink, weight = 500, alpha = 1) {
    const c = this.ctx;
    c.save();
    c.globalAlpha *= alpha;
    c.font = fontSpec(px, weight);
    const lines = [];
    let cur = '';
    for (const ch of str) {
      const next = cur + ch;
      if (c.measureText(next).width > maxW && cur && !'，。、；：！？）」』…·'.includes(ch)) {
        lines.push(cur);
        cur = ch;
      } else {
        cur = next;
      }
    }
    if (cur) lines.push(cur);
    c.textAlign = 'left';
    c.textBaseline = 'top';
    c.fillStyle = color;
    lines.forEach((line, idx) => c.fillText(line, x, y + idx * lineH));
    c.restore();
    return lines.length;
  }

  inkLine(x1, y1, x2, y2, color = C.Ink, width = 2.5, seed = 1, alpha = 1) {
    const c = this.ctx;
    const dx = x2 - x1, dy = y2 - y1;
    const len = Math.hypot(dx, dy);
    if (len < 1 || alpha <= 0.01) return;
    const nx = -dy / len, ny = dx / len;
    const S = seed * 37 + this.boil;
    const bow = Math.min(2.8, len * 0.01) * signedNoise(S, 3);
    c.save();
    c.globalAlpha *= alpha;
    c.strokeStyle = color;
    c.lineWidth = width;
    c.lineCap = 'round';
    c.beginPath();
    c.moveTo(x1, y1);
    c.quadraticCurveTo((x1 + x2) / 2 + nx * bow, (y1 + y2) / 2 + ny * bow, x2, y2);
    c.stroke();
    c.restore();
  }

  // Hand-cut paper card with crisp offset shadow and inked outline
  paperCard(x, y, w, h, opts = {}) {
    const {
      fill = C.PaperWarm,
      stroke = C.Ink,
      strokeW = 3,
      shadow = 8,
      seed = 100,
      radius = 4,
      grain = true,
      alpha = 1,
    } = opts;
    if (alpha <= 0.01) return;
    const c = this.ctx;
    c.save();
    c.globalAlpha *= alpha;
    if (shadow > 0) {
      c.fillStyle = rgba(C.Ink, 0.16);
      c.beginPath();
      c.roundRect(x + shadow * 0.7, y + shadow, w, h, radius);
      c.fill();
    }
    c.fillStyle = fill;
    c.beginPath();
    c.roundRect(x, y, w, h, radius);
    c.fill();
    if (grain) {
      c.save();
      c.clip();
      c.globalCompositeOperation = 'multiply';
      c.fillStyle = this.grainPattern;
      c.fillRect(x, y, w, h);
      c.restore();
    }
    if (stroke && strokeW > 0) {
      this.inkLine(x, y, x + w, y, stroke, strokeW, seed + 1);
      this.inkLine(x + w, y, x + w, y + h, stroke, strokeW, seed + 2);
      this.inkLine(x + w, y + h, x, y + h, stroke, strokeW, seed + 3);
      this.inkLine(x, y + h, x, y, stroke, strokeW, seed + 4);
    }
    c.restore();
  }

  // Semi-transparent serrated masking tape
  washiTape(cx, cy, w, h, deg = -3, color = C.Coral, alpha = 0.82) {
    const c = this.ctx;
    c.save();
    c.translate(cx, cy);
    c.rotate((deg * Math.PI) / 180);
    c.globalAlpha *= alpha;
    c.fillStyle = rgba(color, 0.72);
    c.beginPath();
    const teeth = 4;
    for (let i = 0; i <= teeth; i++) {
      const tx = w / 2 - (i % 2 ? 4 : 0);
      const ty = -h / 2 + (h * i) / teeth;
      if (i === 0) c.moveTo(tx, ty);
      else c.lineTo(tx, ty);
    }
    for (let i = teeth; i >= 0; i--) {
      const tx = -w / 2 + (i % 2 ? 4 : 0);
      const ty = -h / 2 + (h * i) / teeth;
      c.lineTo(tx, ty);
    }
    c.closePath();
    c.fill();
    c.strokeStyle = 'rgba(255,255,255,0.28)';
    c.lineWidth = 1.2;
    c.beginPath();
    c.moveTo(-w * 0.44, -h * 0.45);
    c.lineTo(w * 0.44, -h * 0.45);
    c.stroke();
    c.restore();
  }

  // Highlighter marker brush stroke
  highlightBar(x, y, w, h, color = C.Marigold, progress = 1, alpha = 0.45) {
    if (progress <= 0.01) return;
    const c = this.ctx;
    const ww = w * clamp01(progress);
    c.save();
    c.globalAlpha *= alpha;
    c.fillStyle = color;
    c.beginPath();
    c.moveTo(x + h * 0.25, y);
    c.lineTo(x + ww, y + 1);
    c.lineTo(x + ww - h * 0.22, y + h);
    c.lineTo(x, y + h - 1);
    c.closePath();
    c.fill();
    c.restore();
  }

  // Rubber stamp with diagonal caution stripes option
  rubberStamp(label, cx, cy, size = 34, color = C.Teal, age = 1, deg = -7, striped = false) {
    if (age < 0) return;
    const c = this.ctx;
    const p = clamp01(age / 0.2);
    const scale = p < 1 ? 2.1 - 1.1 * ease.inCubic(p) : 1 + 0.04 * Math.cos((age - 0.2) * 26) * Math.exp(-(age - 0.2) * 8);
    const padX = size * 0.65;
    const tw = this.measure(label, size, 900);
    const bw = tw + padX * 2;
    const bh = size * 1.55;

    c.save();
    c.translate(cx, cy);
    c.rotate((deg * Math.PI) / 180);
    c.scale(scale, scale);
    c.globalAlpha *= clamp01(p * 3);

    if (striped) {
      c.fillStyle = C.Marigold;
      c.fillRect(-bw / 2 - 6, -bh / 2 - 6, bw + 12, bh + 12);
      c.save();
      c.beginPath();
      c.rect(-bw / 2 - 6, -bh / 2 - 6, bw + 12, bh + 12);
      c.clip();
      c.strokeStyle = C.Ink;
      c.lineWidth = 8;
      c.beginPath();
      for (let k = -bh * 2; k < bw + bh * 2; k += 22) {
        c.moveTo(-bw / 2 + k, bh / 2 + 10);
        c.lineTo(-bw / 2 + k + bh + 16, -bh / 2 - 10);
      }
      c.stroke();
      c.restore();
      c.fillStyle = C.PaperWarm;
      c.fillRect(-bw / 2 + 4, -bh / 2 + 4, bw - 8, bh - 8);
    }

    c.strokeStyle = striped ? C.Ink : color;
    c.lineWidth = 4;
    c.strokeRect(-bw / 2, -bh / 2, bw, bh);
    this.text(label, 0, 1, size, striped ? C.Ink : color, { weight: 900, align: 'center', baseline: 'middle' });
    c.restore();
  }

  // Warm drafting graph-paper desk background with subtle diagonal wood edge and animated geometric doodles
  drawDraftingDesk(t, opts = {}) {
    const { showRuler = true, doodleAlpha = 0.28 } = opts;
    const c = this.ctx;
    // Outer kraft wood margin
    c.fillStyle = C.DeskWood;
    c.fillRect(0, 0, this.W, this.H);

    // Subtle wood stripe grain on outer margin
    c.save();
    c.strokeStyle = rgba(C.Ink, 0.05);
    c.lineWidth = 2;
    for (let x = -this.H; x < this.W + this.H; x += 26) {
      c.beginPath();
      c.moveTo(x, 0);
      c.lineTo(x + this.H * 0.45, this.H);
      c.stroke();
    }
    c.restore();

    // Main cream graph-paper sheet slightly inset
    const m = 24;
    this.paperCard(m, m, this.W - m * 2, this.H - m * 2, {
      fill: C.Paper,
      stroke: C.Ink,
      strokeW: 2.5,
      shadow: 10,
      seed: 42,
      radius: 2,
      grain: true,
    });

    // Graph-paper grid inside sheet
    c.save();
    c.beginPath();
    c.rect(m, m, this.W - m * 2, this.H - m * 2);
    c.clip();
    const step = 40;
    for (let x = m; x <= this.W - m; x += step) {
      const major = Math.round((x - m) / step) % 4 === 0;
      c.fillStyle = rgba(C.Grid, major ? 0.36 : 0.18);
      c.fillRect(x, m, major ? 1.5 : 1, this.H - m * 2);
    }
    for (let y = m; y <= this.H - m; y += step) {
      const major = Math.round((y - m) / step) % 4 === 0;
      c.fillStyle = rgba(C.Grid, major ? 0.36 : 0.18);
      c.fillRect(m, y, this.W - m * 2, major ? 1.5 : 1);
    }

    // Steel drafting ruler ticks along top or bottom margin
    if (showRuler) {
      c.fillStyle = rgba(C.Ruler, 0.48);
      c.fillRect(m, m, this.W - m * 2, 20);
      c.fillStyle = rgba(C.Ink, 0.35);
      for (let x = m + 16; x < this.W - m - 16; x += 16) {
        const tall = ((x - m) / 16) % 5 === 0 ? 12 : 6;
        c.fillRect(x, m, 1.5, tall);
      }
    }

    // Subtle animated pencil geometric doodles in corners
    if (doodleAlpha > 0) {
      const spots = [
        [105, 115, 'tri'], [1805, 125, 'hex'], [110, 965, 'circle'], [1805, 960, 'square'],
        [240, 175, 'plus'], [1690, 185, 'plus'], [210, 880, 'diamond'], [1720, 880, 'tri'],
      ];
      spots.forEach(([sx, sy, kind], idx) => {
        const bob = Math.sin(t * 1.4 + idx * 1.1) * 5;
        const rot = Math.sin(t * 0.7 + idx) * 0.12;
        c.save();
        c.translate(sx, sy + bob);
        c.rotate(rot);
        c.strokeStyle = rgba(C.Pencil, doodleAlpha);
        c.lineWidth = 2;
        c.beginPath();
        if (kind === 'circle') c.arc(0, 0, 16, 0, Math.PI * 2);
        else if (kind === 'square') c.rect(-14, -14, 28, 28);
        else if (kind === 'tri') {
          c.moveTo(0, -17); c.lineTo(16, 13); c.lineTo(-16, 13); c.closePath();
        } else if (kind === 'hex') {
          for (let k = 0; k < 6; k++) {
            const a = (k * Math.PI) / 3;
            const px = Math.cos(a) * 16, py = Math.sin(a) * 16;
            if (k === 0) c.moveTo(px, py); else c.lineTo(px, py);
          }
          c.closePath();
        } else if (kind === 'diamond') {
          c.moveTo(0, -17); c.lineTo(13, 0); c.lineTo(0, 17); c.lineTo(-13, 0); c.closePath();
        } else {
          c.moveTo(-12, 0); c.lineTo(12, 0); c.moveTo(0, -12); c.lineTo(0, 12);
        }
        c.stroke();
        c.restore();
      });
    }
    c.restore();
  }

  // Hand-drawn geometric buddy (circle, square, triangle with cannon barrel / cute outline)
  drawBuddy(kind, cx, cy, size, color, angle = 0, squash = 1) {
    const c = this.ctx;
    c.save();
    c.translate(cx, cy);
    c.rotate(angle);
    c.scale(squash, 2 - squash);
    c.lineWidth = Math.max(2.5, size * 0.09);
    c.strokeStyle = C.Ink;
    c.fillStyle = color;
    const r = size * 0.5;
    c.beginPath();
    if (kind === 'circle') {
      c.arc(0, 0, r, 0, Math.PI * 2);
    } else if (kind === 'square') {
      c.roundRect(-r * 0.9, -r * 0.9, r * 1.8, r * 1.8, 3);
    } else {
      c.moveTo(0, -r * 1.05);
      c.lineTo(r * 0.98, r * 0.8);
      c.lineTo(-r * 0.98, r * 0.8);
      c.closePath();
    }
    c.fill();
    c.stroke();

    // Cannon barrel on circle hero
    if (kind === 'circle') {
      c.fillStyle = C.InkSoft;
      c.fillRect(r * 0.15, -r * 0.18, r * 0.95, r * 0.36);
      c.strokeRect(r * 0.15, -r * 0.18, r * 0.95, r * 0.36);
    }
    c.restore();
  }

  // Vector icons matching sketchbook aesthetic
  drawIcon(name, cx, cy, size, accent = C.Coral) {
    const c = this.ctx;
    const r = size * 0.5;
    c.save();
    c.translate(cx, cy);
    c.strokeStyle = C.Ink;
    c.fillStyle = accent;
    c.lineWidth = Math.max(2, size * 0.085);
    c.lineJoin = 'round';
    c.lineCap = 'round';

    switch (name) {
      case 'maze':
        c.fillRect(-r, -r, size, size);
        c.strokeRect(-r, -r, size, size);
        c.beginPath();
        c.moveTo(-r * 0.4, -r); c.lineTo(-r * 0.4, r * 0.3); c.lineTo(r * 0.25, r * 0.3);
        c.moveTo(r * 0.4, r); c.lineTo(r * 0.4, -r * 0.35);
        c.stroke();
        break;
      case 'bolt':
        c.beginPath();
        c.moveTo(r * 0.15, -r); c.lineTo(-r * 0.55, r * 0.1); c.lineTo(0, r * 0.1);
        c.lineTo(-r * 0.2, r); c.lineTo(r * 0.55, -r * 0.1); c.lineTo(0, -r * 0.1);
        c.closePath();
        c.fill(); c.stroke();
        break;
      case 'flame':
        c.beginPath();
        c.moveTo(0, -r); c.quadraticCurveTo(r * 0.95, -r * 0.1, r * 0.65, r * 0.7);
        c.quadraticCurveTo(0, r * 1.05, -r * 0.65, r * 0.7);
        c.quadraticCurveTo(-r * 0.95, -r * 0.1, 0, -r);
        c.fill(); c.stroke();
        break;
      case 'trapezoid':
        c.beginPath();
        c.moveTo(-r * 0.55, -r * 0.75); c.lineTo(r * 0.55, -r * 0.75);
        c.lineTo(r, r * 0.75); c.lineTo(-r, r * 0.75);
        c.closePath(); c.fill(); c.stroke();
        break;
      case 'star': {
        c.beginPath();
        for (let i = 0; i < 10; i++) {
          const rad = i % 2 ? r * 0.45 : r;
          const a = -Math.PI / 2 + (i * Math.PI) / 5;
          if (i === 0) c.moveTo(Math.cos(a) * rad, Math.sin(a) * rad);
          else c.lineTo(Math.cos(a) * rad, Math.sin(a) * rad);
        }
        c.closePath(); c.fill(); c.stroke();
        break;
      }
      case 'hex':
        c.beginPath();
        for (let i = 0; i < 6; i++) {
          const a = -Math.PI / 2 + (i * Math.PI) / 3;
          if (i === 0) c.moveTo(Math.cos(a) * r, Math.sin(a) * r);
          else c.lineTo(Math.cos(a) * r, Math.sin(a) * r);
        }
        c.closePath(); c.fill(); c.stroke();
        break;
      case 'throw':
        c.beginPath();
        c.arc(-r * 0.35, r * 0.25, r * 0.42, 0, Math.PI * 2);
        c.fill(); c.stroke();
        c.beginPath();
        c.moveTo(0, -r * 0.1); c.quadraticCurveTo(r * 0.5, -r * 0.9, r * 0.9, -r * 0.35);
        c.stroke();
        c.beginPath();
        c.moveTo(r * 0.9, -r * 0.35); c.lineTo(r * 0.55, -r * 0.5);
        c.moveTo(r * 0.9, -r * 0.35); c.lineTo(r * 0.85, -r * 0.75);
        c.stroke();
        break;
      case 'shield':
        c.beginPath();
        c.moveTo(0, -r); c.lineTo(r * 0.85, -r * 0.55); c.lineTo(r * 0.7, r * 0.35);
        c.lineTo(0, r); c.lineTo(-r * 0.7, r * 0.35); c.lineTo(-r * 0.85, -r * 0.55);
        c.closePath(); c.fill(); c.stroke();
        break;
      case 'crown':
        c.beginPath();
        c.moveTo(-r, r * 0.6); c.lineTo(-r, -r * 0.45); c.lineTo(-r * 0.4, r * 0.1);
        c.lineTo(0, -r * 0.7); c.lineTo(r * 0.4, r * 0.1); c.lineTo(r, -r * 0.45); c.lineTo(r, r * 0.6);
        c.closePath(); c.fill(); c.stroke();
        break;
      case 'weapons':
        c.fillStyle = C.InkSoft;
        c.fillRect(-r * 0.9, -r * 0.25, r * 1.65, r * 0.45);
        c.strokeRect(-r * 0.9, -r * 0.25, r * 1.65, r * 0.45);
        c.fillStyle = accent;
        c.fillRect(-r * 0.7, r * 0.2, r * 0.4, r * 0.6);
        c.strokeRect(-r * 0.7, r * 0.2, r * 0.4, r * 0.6);
        break;
      case 'hat':
        c.fillRect(-r * 0.55, -r * 0.85, r * 1.1, r * 1.25);
        c.strokeRect(-r * 0.55, -r * 0.85, r * 1.1, r * 1.25);
        c.fillStyle = C.Coral;
        c.fillRect(-r * 0.55, r * 0.05, r * 1.1, r * 0.35);
        c.beginPath();
        c.moveTo(-r, r * 0.45); c.lineTo(r, r * 0.45);
        c.stroke();
        break;
      case 'compass':
        c.beginPath(); c.arc(0, 0, r * 0.9, 0, Math.PI * 2); c.fillStyle = C.PaperWarm; c.fill(); c.stroke();
        c.beginPath(); c.moveTo(0, -r * 0.6); c.lineTo(r * 0.28, 0); c.lineTo(0, r * 0.6); c.lineTo(-r * 0.28, 0); c.closePath();
        c.fillStyle = accent; c.fill(); c.stroke();
        break;
      case 'roulette':
        c.beginPath(); c.arc(0, 0, r * 0.9, 0, Math.PI * 2); c.fill(); c.stroke();
        c.beginPath();
        c.moveTo(0, -r * 0.9); c.lineTo(0, r * 0.9);
        c.moveTo(-r * 0.9, 0); c.lineTo(r * 0.9, 0);
        c.stroke();
        c.beginPath(); c.arc(0, 0, r * 0.25, 0, Math.PI * 2); c.fillStyle = C.PaperWarm; c.fill(); c.stroke();
        break;
      case 'wishlist':
        c.beginPath();
        c.roundRect(-r * 0.95, -r * 0.75, size * 0.95, size * 0.65, 6);
        c.fill(); c.stroke();
        c.beginPath(); c.moveTo(-r * 0.4, r * 0.55); c.lineTo(-r * 0.55, r * 0.95); c.lineTo(-r * 0.05, r * 0.55);
        c.fill(); c.stroke();
        break;
      case 'gear':
        for (let i = 0; i < 8; i++) {
          const a = (i * Math.PI) / 4;
          c.beginPath();
          c.moveTo(Math.cos(a) * r * 0.6, Math.sin(a) * r * 0.6);
          c.lineTo(Math.cos(a) * r, Math.sin(a) * r);
          c.stroke();
        }
        c.beginPath(); c.arc(0, 0, r * 0.68, 0, Math.PI * 2); c.fill(); c.stroke();
        c.beginPath(); c.arc(0, 0, r * 0.25, 0, Math.PI * 2); c.fillStyle = C.PaperWarm; c.fill(); c.stroke();
        break;
      case 'launch':
        c.beginPath();
        c.moveTo(0, -r); c.quadraticCurveTo(r * 0.65, -r * 0.2, r * 0.45, r * 0.55);
        c.lineTo(-r * 0.45, r * 0.55); c.quadraticCurveTo(-r * 0.65, -r * 0.2, 0, -r);
        c.fill(); c.stroke();
        c.beginPath(); c.arc(0, -r * 0.15, r * 0.22, 0, Math.PI * 2); c.fillStyle = C.PaperWarm; c.fill(); c.stroke();
        break;
      case 'spark':
      default:
        c.beginPath();
        c.moveTo(-r * 0.75, 0); c.lineTo(r * 0.75, 0);
        c.moveTo(0, -r * 0.75); c.lineTo(0, r * 0.75);
        c.stroke();
        c.beginPath(); c.arc(0, 0, r * 0.32, 0, Math.PI * 2); c.fill(); c.stroke();
        break;
    }
    c.restore();
  }

  // Hand-drawn shopping basket that collects geometric stickers as roadmap items are added
  drawShoppingBasket(cx, cy, w, h, collectedIcons, t) {
    const c = this.ctx;
    c.save();
    c.translate(cx, cy);

    // Basket shadow
    c.fillStyle = rgba(C.Ink, 0.14);
    c.beginPath();
    c.ellipse(6, h * 0.48, w * 0.46, 16, 0, 0, Math.PI * 2);
    c.fill();

    // Collected item tokens sitting inside the basket!
    collectedIcons.forEach((item, idx) => {
      const age = item.age;
      if (age < 0) return;
      const drop = ease.outBack(age / 0.35, 1.8);
      const col = idx % 4;
      const row = Math.floor(idx / 4);
      const tx = -w * 0.28 + col * (w * 0.19) + ((row % 2) * 12);
      const ty = -h * 0.22 - row * 34 - (1 - drop) * 90 + Math.sin(t * 3 + idx) * 2;
      c.save();
      c.globalAlpha *= clamp01(age * 4);
      this.drawIcon(item.icon, tx, ty, 38, C[item.color] || C.Coral);
      c.restore();
    });

    // Basket trapezoid body
    c.fillStyle = C.Kraft;
    c.strokeStyle = C.Ink;
    c.lineWidth = 3.5;
    c.beginPath();
    c.moveTo(-w * 0.5, -h * 0.15);
    c.lineTo(w * 0.5, -h * 0.15);
    c.lineTo(w * 0.36, h * 0.42);
    c.lineTo(-w * 0.36, h * 0.42);
    c.closePath();
    c.fill();
    c.stroke();

    // Basket weave lines
    c.strokeStyle = rgba(C.Ink, 0.25);
    c.lineWidth = 2;
    for (let k = -2; k <= 2; k++) {
      c.beginPath();
      c.moveTo(k * w * 0.14, -h * 0.1);
      c.lineTo(k * w * 0.105, h * 0.38);
      c.stroke();
    }
    c.beginPath();
    c.moveTo(-w * 0.44, h * 0.08);
    c.lineTo(w * 0.44, h * 0.08);
    c.moveTo(-w * 0.40, h * 0.26);
    c.lineTo(w * 0.40, h * 0.26);
    c.stroke();

    // Rim bar & handle
    c.fillStyle = C.Coral;
    c.strokeStyle = C.Ink;
    c.lineWidth = 3.5;
    c.beginPath();
    c.roundRect(-w * 0.53, -h * 0.22, w * 1.06, 20, 6);
    c.fill();
    c.stroke();

    // Upright handle arc
    c.strokeStyle = C.Ink;
    c.lineWidth = 5;
    c.beginPath();
    c.arc(0, -h * 0.22, w * 0.3, Math.PI, 0);
    c.stroke();

    // Front sticker badge on basket
    this.paperCard(-w * 0.22, -h * 0.02, w * 0.44, 46, {
      fill: C.Sticky, stroke: C.Ink, strokeW: 2.2, shadow: 3, seed: 888, radius: 4, grain: false,
    });
    this.text(`待办 ${collectedIcons.filter(x => x.age >= 0).length}/8`, 0, 22, 23, C.Ink, {
      weight: 900, align: 'center', baseline: 'middle',
    });

    c.restore();
  }

  // Mechanical / wooden drafting pencil for writing the shopping list
  drawPencil(tipX, tipY, angleRad = 0.72) {
    const c = this.ctx;
    const len = 210;
    const r = 13;
    c.save();
    c.translate(tipX, tipY);
    c.rotate(angleRad);

    // Pencil shadow
    c.fillStyle = rgba(C.Ink, 0.16);
    c.beginPath();
    c.moveTo(6, 12);
    c.lineTo(44, -r + 12);
    c.lineTo(len + 46, -r + 12);
    c.lineTo(len + 46, r + 12);
    c.lineTo(44, r + 12);
    c.closePath();
    c.fill();

    // Cone & graphite tip
    c.fillStyle = C.Kraft;
    c.beginPath(); c.moveTo(0, 0); c.lineTo(40, -r); c.lineTo(40, r); c.closePath(); c.fill();
    c.fillStyle = C.Ink;
    c.beginPath(); c.moveTo(0, 0); c.lineTo(13, -4.3); c.lineTo(13, 4.3); c.closePath(); c.fill();

    // Marigold body
    c.fillStyle = C.Marigold;
    c.fillRect(40, -r, len - 60, r * 2);
    c.fillStyle = '#FFD374';
    c.fillRect(40, -r, len - 60, r * 0.65);

    // Ferrule & coral eraser
    c.fillStyle = C.Ruler;
    c.fillRect(len - 20, -r, 20, r * 2);
    c.fillStyle = C.Coral;
    c.fillRect(len, -r * 0.92, 28, r * 1.84);

    // Ink outline
    c.strokeStyle = C.Ink;
    c.lineWidth = 2.6;
    c.beginPath();
    c.moveTo(0, 0);
    c.lineTo(40, -r);
    c.lineTo(len + 28, -r * 0.92);
    c.lineTo(len + 28, r * 0.92);
    c.lineTo(40, r);
    c.closePath();
    c.stroke();

    c.restore();
  }

  // Confetti burst
  confetti(cx, cy, age, count = 24, spreadSpeed = 900, seed = 10) {
    if (age < 0 || age > 1.4) return;
    const colors = [C.Coral, C.Teal, C.Marigold, C.Cobalt, C.Magenta, C.Mint];
    const c = this.ctx;
    for (let i = 0; i < count; i++) {
      const r1 = rand01(seed * 97 + i * 17 + 1);
      const r2 = rand01(seed * 97 + i * 17 + 2);
      const r3 = rand01(seed * 97 + i * 17 + 3);
      const r4 = rand01(seed * 97 + i * 17 + 4);
      const ang = (r1 - 0.5) * Math.PI * 1.7 - Math.PI / 2;
      const spd = spreadSpeed * (0.4 + 0.6 * r2);
      const drag = 1 - Math.exp(-age * 2.2);
      const px = cx + Math.cos(ang) * spd * drag * 0.45;
      const py = cy + Math.sin(ang) * spd * drag * 0.45 + 480 * age * age;
      const sz = 7 + r3 * 8;
      const alpha = clamp01(1 - (age - 0.7) / 0.6);
      if (alpha <= 0.01) continue;
      c.save();
      c.translate(px, py);
      c.rotate(age * (6 + r4 * 8));
      c.globalAlpha *= alpha;
      c.fillStyle = colors[i % colors.length];
      c.strokeStyle = C.Ink;
      c.lineWidth = 1.4;
      c.beginPath();
      if (i % 3 === 0) {
        c.rect(-sz / 2, -sz / 2, sz, sz * 0.65);
      } else if (i % 3 === 1) {
        c.moveTo(0, -sz * 0.65); c.lineTo(sz * 0.6, sz * 0.5); c.lineTo(-sz * 0.6, sz * 0.5); c.closePath();
      } else {
        c.arc(0, 0, sz * 0.45, 0, Math.PI * 2);
      }
      c.fill();
      c.stroke();
      c.restore();
    }
  }

  // Original Diagonal Craft-Paper Sheet Wipe (distinct from any tile wipe)
  // Progress p in [0..1] sweeps layered colored paper tabs across screen.
  paperSheetWipe(progress, accent = C.Teal) {
    if (progress <= 0.001 || progress >= 0.999) return;
    const c = this.ctx;
    // Two-stage sweep: 0..0.5 enters to full cover, 0.5..1 slides out revealing next scene
    const layers = [
      { color: accent, lag: 0.0, skew: 140 },
      { color: C.Marigold, lag: 0.04, skew: 120 },
      { color: C.Paper, lag: 0.08, skew: 100 },
    ];
    c.save();
    for (const layer of layers) {
      const p = ease.inOutCubic(clamp01((progress - layer.lag) / (1 - layers.length * 0.04)));
      const enter = clamp01(p * 2);
      const exit = clamp01((p - 0.5) * 2);
      const leftX = -this.W - 220 + enter * (this.W + 220) + exit * (this.W + 280);
      const rightX = leftX + this.W + 220;
      c.fillStyle = layer.color;
      c.beginPath();
      c.moveTo(leftX + layer.skew, 0);
      c.lineTo(rightX + layer.skew, 0);
      c.lineTo(rightX - layer.skew, this.H);
      c.lineTo(leftX - layer.skew, this.H);
      c.closePath();
      c.fill();
      c.strokeStyle = C.Ink;
      c.lineWidth = 4;
      c.stroke();
    }
    c.restore();
  }
}
