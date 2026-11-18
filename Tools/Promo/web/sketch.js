// The game's sketchbook toolkit (Source/GeometricWarfare/Standalone/GWSketch.cpp and GWMotion.cpp) on a 2D canvas, so
// the promo's cards, tape, pen lines, lettering, motions and page wipe follow the same rules as the HUD: the same
// hash noise, the same pen wobble and overshoot, the same easing curves and the same triangle tiles.

export const Palette = {
  Paper: '#F6F0E1', PaperShade: '#EADFC8', PaperDeep: '#D9CAAC',
  Ink: '#1F2633', InkSoft: '#4A5262', Pencil: '#8C909A', Grid: '#9DB5D3',
  Coral: '#E8553F', Cobalt: '#2F6BD8', Marigold: '#F2A93B', Teal: '#26A796',
  Mint: '#9AD8CB', Plum: '#4B3F72', Magenta: '#B23A7A', Night: '#2A2140',
  Violet: '#8E5CD9', Frost: '#5EC8E8', Fire: '#FF6A2B', Volt: '#6E6BFF',
  Spark: '#FFD84A', Leaf: '#4DBA5C', Kraft: '#D6BC90', Danger: '#E23A33',
  Ruler: '#C5CCD6', RulerEdge: '#8E98A8', Sticky: '#FFF2B0',
};
export const Seats = ['#E8553F', '#2F6BD8', '#D9A10E', '#3A9E45', '#E0559B', '#17A3BF', '#8E5B2E', '#94B81A'];

const parsed = new Map();
function rgb(color) {
  let v = parsed.get(color);
  if (!v) {
    const h = color.replace('#', '');
    v = [parseInt(h.slice(0, 2), 16), parseInt(h.slice(2, 4), 16), parseInt(h.slice(4, 6), 16)];
    parsed.set(color, v);
  }
  return v;
}
export function rgba(color, alpha = 1) {
  const [r, g, b] = rgb(color);
  return `rgba(${r},${g},${b},${Math.max(0, Math.min(1, alpha))})`;
}
export function mix(a, b, t) {
  const A = rgb(a), B = rgb(b);
  const c = A.map((x, i) => Math.round(x + (B[i] - x) * t));
  return '#' + c.map(x => x.toString(16).padStart(2, '0')).join('');
}
export function shade(color, k) {
  const c = rgb(color).map(x => Math.max(0, Math.min(255, Math.round(x * k))));
  return '#' + c.map(x => x.toString(16).padStart(2, '0')).join('');
}

// GWSketch.cpp Noise(): a hash in -1..1.
export function noise(seed, index) {
  let h = (Math.imul(seed | 0, 747796405) + Math.imul(index | 0, 2891336453) + 12345) >>> 0;
  h = Math.imul(((h >>> ((h >>> 28) + 4)) ^ h) >>> 0, 277803737) >>> 0;
  h = ((h >>> 22) ^ h) >>> 0;
  return (h & 0xffffff) / 8388607.5 - 1;
}
// GWMotion.cpp Hash01().
export function hash01(x) {
  x = x >>> 0;
  x ^= x >>> 16; x = Math.imul(x, 0x7feb352d) >>> 0;
  x ^= x >>> 15; x = Math.imul(x, 0x846ca68b) >>> 0;
  x ^= x >>> 16;
  return (x & 0xffffff) / 16777215;
}

export const Ease = {
  clamp01: t => Math.max(0, Math.min(1, t)),
  outCubic(t) { t = 1 - Ease.clamp01(t); return 1 - t * t * t; },
  inCubic(t) { t = Ease.clamp01(t); return t * t * t; },
  inOutCubic(t) { t = Ease.clamp01(t); return t < .5 ? 4 * t * t * t : 1 - Math.pow(-2 * t + 2, 3) * .5; },
  outBack(t, a = 1.7) { t = Ease.clamp01(t) - 1; return 1 + (a + 1) * t * t * t + a * t * t; },
  inBack(t, a = 1.7) { t = Ease.clamp01(t); return (a + 1) * t * t * t - a * t * t; },
  wobble(s, f = 22, d = 7) { return s < 0 ? 0 : Math.cos(s * f) * Math.exp(-s * d); },
  smooth(t) { t = Ease.clamp01(t); return t * t * (3 - 2 * t); },
  outQuint(t) { t = 1 - Ease.clamp01(t); return 1 - t * t * t * t * t; },
};

const V = (x, y) => ({ x, y });
const add = (a, b) => V(a.x + b.x, a.y + b.y);
const sub = (a, b) => V(a.x - b.x, a.y - b.y);
const mul = (a, k) => V(a.x * k, a.y * k);
const lerp = (a, b, t) => V(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
export { V, add, sub, mul, lerp };

// Fonts: Roboto (the HUD's Latin faces) and Noto Sans SC for CJK, by weight.
export const Faces = { Body: 500, Bold: 700, Heavy: 900 };
export function fontFor(px, face = 'Bold') {
  return `${Faces[face] || face} ${px.toFixed(1)}px GWLatin, GWCJK, sans-serif`;
}

function makeGrain() {
  const c = new OffscreenCanvas(256, 256), g = c.getContext('2d');
  const img = g.createImageData(256, 256);
  let s = 0x2545F491;
  for (let i = 0; i < 256 * 256; ++i) {
    s ^= s << 13; s ^= s >>> 17; s ^= s << 5;
    const n = ((s >>> 0) % 1000) / 1000;
    const v = 255 - Math.floor(n * n * 26);
    img.data[i * 4] = v; img.data[i * 4 + 1] = v - 2; img.data[i * 4 + 2] = v - 6; img.data[i * 4 + 3] = 255;
  }
  g.putImageData(img, 0, 0);
  return c;
}

export class Sketch {
  constructor(ctx, width, height) {
    this.ctx = ctx;
    this.W = width;
    this.H = height;
    this.scale = Math.max(.5, Math.min(width / 1600, height / 900));
    this.boil = 0;
    this.fade = 1;
    this.clock = 0;
    this.grain = makeGrain();
    this.grainPattern = ctx.createPattern(this.grain, 'repeat');
    this.stack = [];
  }
  setClock(t) {
    this.clock = t;
    this.boil = Math.floor(t * 6);
  }

  // --- primitives -------------------------------------------------------------------------------------------------
  line(a, b, color, thickness, alpha = 1) {
    const A = alpha * this.fade;
    if (A <= .002) return;
    const c = this.ctx;
    c.strokeStyle = rgba(color, A);
    c.lineWidth = Math.max(.5, thickness);
    c.lineCap = 'round';
    c.beginPath();
    c.moveTo(a.x, a.y);
    c.lineTo(b.x, b.y);
    c.stroke();
  }
  // A pencil line: overshoots its ends a little, bows by a hashed amount, and gets a faint second pass.
  pen(a, b, color, thickness, seed, wobble = 1, alpha = 1) {
    const d = sub(b, a), length = Math.hypot(d.x, d.y);
    if (length < .5) return;
    const dir = mul(d, 1 / length), perp = V(-dir.y, dir.x);
    const S = seed * 31 + this.boil, k = this.scale;
    const over = Math.min(length * .05, 3.5 * k) * wobble;
    const bow = Math.max(.3, Math.min(2.6 * k, length * .012)) * wobble * noise(S, 3);
    const start = sub(a, mul(dir, over * (.35 + .65 * Math.abs(noise(S, 1)))));
    const end = add(b, mul(dir, over * (.35 + .65 * Math.abs(noise(S, 2)))));
    const segments = Math.max(1, Math.min(8, Math.ceil(length / (26 * k))));
    const A = alpha * this.fade;
    if (A <= .002) return;
    const c = this.ctx;
    c.strokeStyle = rgba(color, A);
    c.lineWidth = Math.max(.5, thickness);
    c.lineCap = 'round';
    c.lineJoin = 'round';
    c.beginPath();
    c.moveTo(start.x, start.y);
    for (let i = 1; i <= segments; ++i) {
      const t = i / segments;
      const p = add(lerp(start, end, t), mul(perp, Math.sin(Math.PI * t) * bow));
      c.lineTo(p.x, p.y);
    }
    c.stroke();
    if (thickness >= 1.2 && wobble > 0) {
      const off = mul(perp, noise(S, 4) * 1.1 * k);
      this.line(add(add(a, off), mul(dir, over * .4)), sub(add(b, off), mul(dir, over * .2)), color, Math.max(.8, thickness * .55), .32 * alpha);
    }
  }
  // A partial pen stroke from a to b, drawn up to fraction t (the pen-sweep reveal).
  penTo(a, b, t, color, thickness, seed, wobble = 1, alpha = 1) {
    if (t <= 0) return;
    this.pen(a, lerp(a, b, Math.min(1, t)), color, thickness, seed, wobble * Math.min(1, t * 2), alpha);
  }
  fill(points, color, alpha = 1) {
    const A = alpha * this.fade;
    if (points.length < 3 || A <= .002) return;
    const c = this.ctx;
    c.fillStyle = rgba(color, A);
    c.beginPath();
    c.moveTo(points[0].x, points[0].y);
    for (let i = 1; i < points.length; ++i) c.lineTo(points[i].x, points[i].y);
    c.closePath();
    c.fill();
  }
  box(x, y, w, h, color, alpha = 1) {
    const A = alpha * this.fade;
    if (A <= .002) return;
    this.ctx.fillStyle = rgba(color, A);
    this.ctx.fillRect(x, y, w, h);
  }
  grained(points) {
    if (this.fade < .98) return;
    const c = this.ctx;
    c.save();
    c.globalCompositeOperation = 'multiply';
    c.fillStyle = this.grainPattern;
    c.beginPath();
    c.moveTo(points[0].x, points[0].y);
    for (let i = 1; i < points.length; ++i) c.lineTo(points[i].x, points[i].y);
    c.closePath();
    c.fill();
    c.restore();
  }
  card(x, y, w, h, paper, stroke, seed, lift = 0, strokeWidth = 0, alpha = 1) {
    const J = 1.4 * this.scale;
    const corner = (cx, cy, k) => V(cx + noise(seed, k) * J, cy + noise(seed, k + 9) * J);
    const A = corner(x, y, 1), B = corner(x + w, y, 2), C = corner(x + w, y + h, 3), D = corner(x, y + h, 4);
    if (lift > 0) {
      const drop = V(3 * lift * this.scale, 5 * lift * this.scale);
      this.fill([add(A, drop), add(B, drop), add(C, drop), add(D, drop)], Palette.Ink, .12 * alpha);
      const half = mul(drop, .45);
      this.fill([add(A, half), add(B, half), add(C, half), add(D, half)], Palette.Ink, .08 * alpha);
    }
    this.fill([A, B, C, D], paper, alpha);
    if (alpha > .9) this.grained([A, B, C, D]);
    const thick = strokeWidth > 0 ? strokeWidth : 2 * this.scale;
    if (stroke) {
      this.pen(A, B, stroke, thick, seed + 1, 1, alpha);
      this.pen(B, C, stroke, thick, seed + 2, 1, alpha);
      this.pen(C, D, stroke, thick, seed + 3, 1, alpha);
      this.pen(D, A, stroke, thick, seed + 4, 1, alpha);
    }
  }
  tape(center, w, h, degrees, color, alpha = 1) {
    const r = degrees * Math.PI / 180, X = V(Math.cos(r), Math.sin(r)), Y = V(-X.y, X.x), k = this.scale;
    const pts = [], teeth = 4;
    for (let i = 0; i <= teeth; ++i) pts.push(add(add(center, mul(X, w * .5 + (i % 2 ? -2.5 : 0) * k)), mul(Y, -h * .5 + h * i / teeth)));
    for (let i = 0; i <= teeth; ++i) pts.push(add(sub(center, mul(X, w * .5 + (i % 2 ? -2.5 : 0) * k)), mul(Y, h * .5 - h * i / teeth)));
    this.fill(pts, color, .62 * alpha);
    this.line(sub(sub(center, mul(X, w * .42)), mul(Y, h * .5)), sub(add(center, mul(X, w * .42)), mul(Y, h * .5)), '#FFFFFF', 1, .25 * alpha);
  }
  highlight(x, y, w, h, color, seed, alpha = 1) {
    const pts = [], steps = 6, skew = h * .35;
    for (let i = 0; i <= steps; ++i) pts.push(V(x + skew + (w - skew) * i / steps, y + noise(seed, i) * h * .12));
    for (let i = steps; i >= 0; --i) pts.push(V(x + (w - skew) * i / steps, y + h + noise(seed, i + 20) * h * .12));
    this.fill(pts, color, alpha);
  }
  hatch(x, y, w, h, color, spacing, thickness, seed, alpha = 1) {
    if (w <= 0 || h <= 0) return;
    let index = 0;
    for (let k = -h; k < w; k += spacing, ++index) {
      const x0 = Math.max(x + k, x), x1 = Math.min(x + k + h, x + w);
      if (x0 >= x1) continue;
      const j = noise(seed, index) * .8 * this.scale;
      this.line(V(x0, y + h - (x0 - x - k) + j), V(x1, y + h - (x1 - x - k) - j), color, thickness, alpha);
    }
  }
  // Hatching clipped to a polygon (the logo's and the cards' diagonal shading).
  hatchPoly(points, color, spacing, thickness, seed, alpha = 1) {
    const c = this.ctx;
    let minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
    for (const p of points) { minX = Math.min(minX, p.x); minY = Math.min(minY, p.y); maxX = Math.max(maxX, p.x); maxY = Math.max(maxY, p.y); }
    c.save();
    c.beginPath();
    c.moveTo(points[0].x, points[0].y);
    for (let i = 1; i < points.length; ++i) c.lineTo(points[i].x, points[i].y);
    c.closePath();
    c.clip();
    this.hatch(minX, minY, maxX - minX, maxY - minY, color, spacing, thickness, seed, alpha);
    c.restore();
  }
  circle(center, radius, color, thickness, seed, turns = 1, alpha = 1) {
    const S = seed * 17 + this.boil, k = this.scale;
    const count = Math.max(14, Math.min(72, Math.ceil(radius / (3 * k)) + 10));
    const start = noise(S, 1) * Math.PI, p1 = noise(S, 2) * Math.PI, p2 = noise(S, 3) * Math.PI;
    const wob = Math.min(.05, 2.2 * k / Math.max(radius, 1));
    const A = alpha * this.fade;
    if (A <= .002) return;
    const c = this.ctx;
    c.strokeStyle = rgba(color, A);
    c.lineWidth = thickness;
    c.lineCap = 'round';
    c.lineJoin = 'round';
    c.beginPath();
    for (let i = 0; i <= count; ++i) {
      const t = i / count, a = start + t * turns * 2 * Math.PI;
      const r = radius * (1 + wob * (.6 * Math.sin(2 * a + p1) + .4 * Math.sin(3 * a + p2))) * (1 + .03 * (t - .5));
      const x = center.x + Math.cos(a) * r, y = center.y + Math.sin(a) * r;
      if (i) c.lineTo(x, y); else c.moveTo(x, y);
    }
    c.stroke();
  }
  disc(center, radius, color, sides = 24, alpha = 1) {
    const pts = [];
    for (let i = 0; i < sides; ++i) pts.push(V(center.x + Math.cos(i * 2 * Math.PI / sides) * radius, center.y + Math.sin(i * 2 * Math.PI / sides) * radius));
    this.fill(pts, color, alpha);
  }
  arc(center, radius, from, to, color, thickness, alpha = 1) {
    const count = Math.max(4, Math.min(64, Math.ceil(Math.abs(to - from) * radius / (4 * this.scale))));
    let prev = V(center.x + Math.cos(from) * radius, center.y + Math.sin(from) * radius);
    for (let i = 1; i <= count; ++i) {
      const a = from + (to - from) * i / count;
      const p = V(center.x + Math.cos(a) * radius, center.y + Math.sin(a) * radius);
      this.line(prev, p, color, thickness, alpha);
      prev = p;
    }
  }
  polygon(points, fillColor, stroke, thickness, seed, alpha = 1) {
    if (fillColor) this.fill(points, fillColor, alpha);
    if (thickness > 0)
      for (let i = 0; i < points.length; ++i) this.pen(points[i], points[(i + 1) % points.length], stroke, thickness, seed + i, .7, alpha);
  }
  ring(c, sides, radius, spin) {
    const pts = [];
    for (let i = 0; i < sides; ++i) {
      const a = (spin + i * 360 / sides) * Math.PI / 180;
      pts.push(V(c.x + Math.cos(a) * radius, c.y + Math.sin(a) * radius));
    }
    return pts;
  }
  star(c, points, outer, inner, spin) {
    const pts = [];
    for (let i = 0; i < points * 2; ++i) {
      const a = (spin + i * 180 / points) * Math.PI / 180;
      pts.push(V(c.x + Math.cos(a) * (i % 2 ? inner : outer), c.y + Math.sin(a) * (i % 2 ? inner : outer)));
    }
    return pts;
  }
  // GWSketch.cpp Glyph(): 0 circle, 1 square, 2 rectangle, 3 triangle, 4 hexagon, 5 star, 6 diamond, 7 plus, 8 flag,
  // 9 lock, 10 gear, 11 bolt, 12 flame, 13 snowflake, 14 shield, 15 piercing arrow, 16 check, 17 cross, 18 crown,
  // 19 maze, 20 cube, 21 pentagon, 22 trapezoid, 23 ink drop.
  glyph(kind, C, size, color, seed, alpha = 1) {
    const ink = Palette.Ink, R = size * .5, W = Math.max(1.4, size * .06);
    const P = (x, y) => V(C.x + x, C.y + y);
    switch (kind) {
      case 0: this.disc(C, R * .9, color, 24, alpha); this.circle(C, R * .9, ink, W, seed, 1, alpha); break;
      case 1: this.polygon(this.ring(C, 4, R * 1.15, 45), color, ink, W, seed, alpha); break;
      case 2: this.polygon([P(-R, -R * .55), P(R, -R * .55), P(R, R * .55), P(-R, R * .55)], color, ink, W, seed, alpha); break;
      case 3: this.polygon(this.ring(C, 3, R, -90), color, ink, W, seed, alpha); break;
      case 4: this.polygon(this.ring(C, 6, R, -90), color, ink, W, seed, alpha); break;
      case 5: {
        const p = this.star(C, 5, R, R * .45, -90);
        this.fill(p, color, alpha);
        for (let i = 0; i < p.length; ++i) this.pen(p[i], p[(i + 1) % p.length], ink, W, seed + i, .5, alpha);
        break;
      }
      case 6: this.polygon([P(0, -R), P(R * .7, 0), P(0, R), P(-R * .7, 0)], color, ink, W, seed, alpha); break;
      case 7: {
        const T = R * .32;
        this.box(C.x - R, C.y - T, 2 * R, 2 * T, color, alpha);
        this.box(C.x - T, C.y - R, 2 * T, 2 * R, color, alpha);
        this.pen(P(-R, 0), P(R, 0), ink, W, seed, .4, alpha);
        this.pen(P(0, -R), P(0, R), ink, W, seed + 1, .4, alpha);
        break;
      }
      case 8:
        this.pen(P(-R * .5, R), P(-R * .5, -R), ink, W * 1.2, seed, .4, alpha);
        this.polygon([P(-R * .45, -R), P(R, -R * .55), P(-R * .45, -R * .1)], color, ink, W, seed + 2, alpha);
        break;
      case 9:
        this.polygon([P(-R * .7, -R * .05), P(R * .7, -R * .05), P(R * .7, R), P(-R * .7, R)], color, ink, W, seed, alpha);
        this.arc(P(0, -R * .05), R * .45, Math.PI, 2 * Math.PI, ink, W * 1.4, alpha);
        this.disc(P(0, R * .45), R * .14, ink, 10, alpha);
        break;
      case 10:
        for (let i = 0; i < 8; ++i) {
          const a = i * Math.PI / 4;
          this.pen(P(Math.cos(a) * R * .6, Math.sin(a) * R * .6), P(Math.cos(a) * R, Math.sin(a) * R), ink, W * 2.2, seed + i, .2, alpha);
        }
        this.disc(C, R * .68, color, 24, alpha);
        this.circle(C, R * .68, ink, W, seed, 1, alpha);
        this.disc(C, R * .25, Palette.Paper, 24, alpha);
        this.circle(C, R * .25, ink, W * .8, seed + 3, 1, alpha);
        break;
      case 11:
        this.polygon([P(R * .15, -R), P(-R * .55, R * .1), P(-R * .02, R * .1), P(-R * .2, R), P(R * .55, -R * .15), P(R * .02, -R * .15)], color, ink, W, seed, alpha);
        break;
      case 12: {
        const p = [];
        for (let i = 0; i < 14; ++i) {
          const t = i / 13, a = Math.PI * .5 + t * 2 * Math.PI;
          const rad = R * (.55 + .45 * Math.pow(Math.abs(Math.sin(a * .5 - Math.PI * .25)), 3));
          p.push(add(P(Math.cos(a) * .75 * rad, -Math.sin(a) * rad), V(0, R * .2)));
        }
        p.push(P(R * .1, -R));
        this.fill(p, color, alpha);
        for (let i = 0; i < p.length; ++i) this.pen(p[i], p[(i + 1) % p.length], ink, W, seed + i, .3, alpha);
        break;
      }
      case 13:
        for (let i = 0; i < 3; ++i) {
          const D = V(Math.cos(i * Math.PI / 3 + Math.PI / 2), Math.sin(i * Math.PI / 3 + Math.PI / 2));
          this.pen(sub(C, mul(D, R)), add(C, mul(D, R)), color, W * 2.2, seed + i, .3, alpha);
          this.pen(sub(C, mul(D, R)), add(C, mul(D, R)), ink, W * .8, seed + i + 9, .3, alpha);
          for (const s of [-1, 1]) {
            const tip = add(C, mul(D, R * .6 * s)), N = V(-D.y, D.x);
            this.pen(tip, add(tip, mul(add(mul(D, s * .25), mul(N, .3)), R)), ink, W, seed + 20, .2, alpha);
            this.pen(tip, add(tip, mul(sub(mul(D, s * .25), mul(N, .3)), R)), ink, W, seed + 21, .2, alpha);
          }
        }
        break;
      case 14:
        this.polygon(this.ring(C, 6, R, -90), color, ink, W, seed, alpha);
        this.pen(P(-R * .4, 0), P(R * .4, 0), Palette.Paper, W * 2, seed + 7, .2, alpha);
        this.pen(P(0, -R * .4), P(0, R * .4), Palette.Paper, W * 2, seed + 8, .2, alpha);
        break;
      case 15:
        this.polygon(this.ring(C, 3, R * .55, 0), color, ink, W, seed, alpha);
        this.pen(P(-R, R * .35), P(R * .2, -R * .1), ink, W * 1.3, seed + 4, .3, alpha);
        this.polygon([P(R, -R * .35), P(R * .55, -R * .45), P(R * .7, -R * .05)], ink, ink, 0, seed, alpha);
        break;
      case 16:
        this.pen(P(-R * .7, 0), P(-R * .15, R * .55), color, W * 2.4, seed, .5, alpha);
        this.pen(P(-R * .15, R * .55), P(R * .8, -R * .7), color, W * 2.4, seed + 1, .5, alpha);
        break;
      case 17:
        this.pen(P(-R * .6, -R * .6), P(R * .6, R * .6), color, W * 2.2, seed, .5, alpha);
        this.pen(P(R * .6, -R * .6), P(-R * .6, R * .6), color, W * 2.2, seed + 1, .5, alpha);
        break;
      case 18:
        this.polygon([P(-R, R * .5), P(-R, -R * .4), P(-R * .45, R * .05), P(0, -R * .7), P(R * .45, R * .05), P(R, -R * .4), P(R, R * .5)], color, ink, W, seed, alpha);
        break;
      case 19:
        this.card(C.x - R, C.y - R, 2 * R, 2 * R, color, ink, seed, 0, W, alpha);
        this.pen(P(-R * .5, -R), P(-R * .5, R * .4), ink, W, seed + 3, .3, alpha);
        this.pen(P(-R * .5, R * .4), P(R * .2, R * .4), ink, W, seed + 4, .3, alpha);
        this.pen(P(R * .5, R), P(R * .5, -R * .3), ink, W, seed + 5, .3, alpha);
        this.pen(P(R * .5, -R * .3), P(-R * .1, -R * .3), ink, W, seed + 6, .3, alpha);
        break;
      case 20: {
        const top = V(0, -R), right = V(R * .87, -R * .5), left = V(-R * .87, -R * .5);
        this.polygon([C, add(C, left), add(C, top), add(C, right)], shade(color, 1.15), ink, W, seed, alpha);
        this.polygon([C, add(C, right), P(R * .87, R * .5), P(0, R)], shade(color, .75), ink, W, seed + 5, alpha);
        this.polygon([C, P(0, R), P(-R * .87, R * .5), add(C, left)], color, ink, W, seed + 9, alpha);
        break;
      }
      case 21: this.polygon(this.ring(C, 5, R, -90), color, ink, W, seed, alpha); break;
      case 22: this.polygon([P(-R, R * .7), P(-R * .55, -R * .7), P(R * .55, -R * .7), P(R, R * .7)], color, ink, W, seed, alpha); break;
      case 23: {
        const p = [P(0, -R)];
        for (let i = 0; i <= 10; ++i) {
          const a = Math.PI * (-.15 + 1.3 * i / 10);
          p.push(add(P(Math.cos(a) * R * .62, Math.sin(a) * R * .62), V(0, R * .3)));
        }
        this.polygon(p, color, ink, W, seed, alpha);
        break;
      }
    }
  }

  // --- text -------------------------------------------------------------------------------------------------------
  measure(text, px, face = 'Bold') {
    const c = this.ctx;
    c.font = fontFor(px, face);
    return c.measureText(text).width;
  }
  label(text, x, y, px, color, center = false, maxWidth = 0, face = 'Bold', alpha = 1) {
    const A = alpha * this.fade;
    if (A <= .002) return;
    const c = this.ctx;
    if (maxWidth > 0) {
      const w = this.measure(text, px, face);
      if (w > maxWidth) px *= maxWidth / w;
    }
    c.font = fontFor(px, face);
    c.textBaseline = 'top';
    c.textAlign = center ? 'center' : 'left';
    c.fillStyle = rgba(color, A);
    c.fillText(text, x, y);
  }
  // Hand lettering (GWSketch.cpp Lettering): each glyph a little off the line with a soft graphite shadow; given an
  // age the letters drop onto the line one after another and bounce once.
  lettering(text, x, y, px, color, seed, age = -1, opts = {}) {
    const A0 = (opts.alpha ?? 1) * this.fade;
    if (A0 <= .002) return;
    const chars = [...text], c = this.ctx, k = this.scale, face = opts.face || 'Heavy';
    const widths = chars.map(ch => this.measure(ch, px, face));
    const gap = px * (opts.spacing ?? .02);
    const total = widths.reduce((s, w) => s + w + gap, 0);
    let cursor = opts.left ? x : x - total * .5;
    const step = opts.step ?? .045;
    for (let i = 0; i < chars.length; ++i) {
      let drop = 0, alpha = 1;
      if (age >= 0) {
        const p = (age - i * step) / .34;
        drop = -(1 - Ease.outBack(p, 2.4)) * px * .55;
        alpha = Ease.clamp01(p * 3);
      }
      const jitter = noise(seed + Math.floor(this.boil / 2), i) * px * .035 + drop, grow = 1 + noise(seed, i + 40) * .04;
      const sz = px * grow, turn = (opts.tilt ?? 0) * noise(seed + 7, i) * Math.PI / 180;
      if (alpha > .01 && chars[i] !== ' ') {
        c.save();
        c.translate(cursor + widths[i] * .5, y + jitter + sz * .5);
        c.rotate(turn);
        c.font = fontFor(sz, face);
        c.textBaseline = 'middle';
        c.textAlign = 'center';
        if (opts.outline) {
          // The logo's sticker letters: a cream fill inside a heavy ink outline, with a paper rim round it.
          c.lineJoin = 'round';
          if (opts.rim) {
            c.strokeStyle = rgba(opts.rim, A0 * alpha);
            c.lineWidth = opts.outline + opts.rimWidth;
            c.strokeText(chars[i], 3 * k, 4 * k);
            c.strokeText(chars[i], 0, 0);
          }
          c.strokeStyle = rgba(Palette.Ink, A0 * alpha);
          c.lineWidth = opts.outline;
          c.strokeText(chars[i], 3 * k * (opts.shadow ? 1 : 0), 4 * k * (opts.shadow ? 1 : 0));
          c.strokeText(chars[i], 0, 0);
          c.fillStyle = rgba(color, A0 * alpha);
          c.fillText(chars[i], 0, 0);
          if (opts.hatch) {
            c.save();
            c.globalCompositeOperation = 'source-atop';
            const hs = Math.max(5, sz * .09);
            c.strokeStyle = rgba(opts.hatch, .35 * A0 * alpha);
            c.lineWidth = Math.max(1, sz * .018);
            c.beginPath();
            for (let h = -sz; h < sz; h += hs) { c.moveTo(h - sz * .5, sz * .5); c.lineTo(h + sz * .5, -sz * .5); }
            c.stroke();
            c.restore();
          }
        } else {
          c.fillStyle = rgba(Palette.Ink, .16 * alpha * A0);
          c.fillText(chars[i], 3 * k, 4 * k - drop * .4);
          c.fillStyle = rgba(color, alpha * A0);
          c.fillText(chars[i], 0, 0);
          c.fillText(chars[i], .7 * k, .4 * k);
        }
        c.restore();
      }
      cursor += widths[i] + gap;
    }
    return total;
  }
  letteringWidth(text, px, face = 'Heavy', spacing = .02) {
    return [...text].reduce((s, ch) => s + this.measure(ch, px, face) + px * spacing, 0);
  }

  // --- motion (GWMotion.cpp) -------------------------------------------------------------------------------------
  push(pivot, offset, turn = 0, grow = 1, alpha = 1) {
    const c = this.ctx;
    c.save();
    c.translate(pivot.x + offset.x, pivot.y + offset.y);
    c.rotate(turn * Math.PI / 180);
    c.scale(grow, grow);
    c.translate(-pivot.x, -pivot.y);
    this.stack.push(this.fade);
    this.fade *= Ease.clamp01(alpha);
  }
  pop() {
    this.ctx.restore();
    this.fade = this.stack.pop() ?? 1;
  }
  dropIn(pivot, t, delay = 0, rise = 70) {
    const p = (t - delay) / .42, settle = Ease.outBack(p, 1.5);
    this.push(pivot, V(0, -(1 - settle) * rise * this.scale), (1 - Ease.outCubic(p)) * -3.5, .965 + .035 * settle, Ease.clamp01(p * 4));
  }
  slideIn(pivot, t, from, delay = 0) {
    const p = (t - delay) / .4;
    this.push(pivot, mul(from, this.scale * (1 - Ease.outBack(p, 1.2))), 0, 1, Ease.clamp01(p * 3));
  }
  popIn(pivot, t, delay = 0) {
    const p = (t - delay) / .34;
    this.push(pivot, V(0, 0), (1 - Ease.outCubic(p)) * -6, Math.max(.01, Ease.outBack(p, 2)), Ease.clamp01(p * 5));
  }
  stampIn(pivot, t, delay = 0) {
    const p = (t - delay) / .2;
    const grow = p < 1 ? 2.4 - 1.4 * Ease.inCubic(p) : 1 + .04 * Ease.wobble(t - delay - .2, 30, 9);
    const turn = p < 1 ? -14 * (1 - p) : 2.5 * Ease.wobble(t - delay - .2, 24, 6);
    this.push(pivot, V(0, 0), turn, grow, Ease.clamp01(p * 2.5));
  }
  // Leaves: the reverse of an entrance (lifts, shrinks and fades).
  liftOut(pivot, t, delay = 0, rise = 60) {
    const p = Ease.clamp01((t - delay) / .3);
    this.push(pivot, V(0, -Ease.inBack(p, 1.4) * rise * this.scale), p * 4, 1 - .06 * p, 1 - p);
  }
}

// Paper confetti (GWMotion.cpp Confetti/DrawBits), deterministic: each burst is simulated from its start.
const ConfettiColors = [Palette.Coral, Palette.Teal, Palette.Marigold, Palette.Cobalt, Palette.Mint, Palette.Violet];
export function confetti(sk, at, age, count, speed, size, seed, direction = -Math.PI / 2, spread = Math.PI * 2) {
  if (age < 0 || age > 1.3) return;
  const dt = 1 / 60, steps = Math.floor(age / dt);
  for (let i = 0; i < count; ++i) {
    const r = k => hash01(seed * 7919 + i * 131 + k);
    const a = direction + (r(1) - .5) * spread, v = speed * (.35 + .65 * r(2)) * sk.scale;
    let p = V(at.x, at.y), vel = V(Math.cos(a) * v, Math.sin(a) * v), angle = r(3) * Math.PI * 2;
    const spin = (r(4) - .5) * 20, sz = size * (.6 + .55 * r(5)) * sk.scale, life = .6 + .55 * r(6), sides = 3 + Math.floor(r(7) * 4);
    if (age >= life) continue;
    for (let s = 0; s < steps; ++s) {
      vel = V(vel.x, vel.y + 1100 * sk.scale * dt);
      vel = mul(vel, Math.max(0, 1 - 1.6 * dt));
      p = add(p, mul(vel, dt));
      angle += spin * dt;
    }
    const flip = Math.cos(angle * 1.7), alpha = 1 - Ease.smooth((age / life - .6) / .4);
    const pts = [];
    for (let k = 0; k < sides; ++k) {
      const b = angle + k * 2 * Math.PI / sides;
      pts.push(V(p.x + Math.cos(b) * sz, p.y + Math.sin(b) * sz * (.35 + .65 * Math.abs(flip))));
    }
    const color = ConfettiColors[Math.floor(r(8) * ConfettiColors.length) % ConfettiColors.length];
    sk.fill(pts, color, alpha);
    for (let k = 0; k < pts.length; ++k) sk.line(pts[k], pts[(k + 1) % pts.length], Palette.Ink, Math.max(1, sk.scale), .8 * alpha);
  }
}

// The page wipe (GWMotion.cpp AGWHUD::Transition): triangle tiles unfold over the screen from the left (closing),
// hold, then fold away the same way (opening). Returns the share of the screen covered.
export const WipeClose = .55, WipeOpen = .75;
export function wipe(sk, stage, age) {
  if (stage === 'idle') return 0;
  const { W, H } = sk, k = sk.scale;
  const opening = stage === 'opening';
  const tileTime = opening ? .3 : .25, sweep = (opening ? WipeOpen : WipeClose) - tileTime;
  const side = 150 * k, tall = side * .8660254;
  const rows = Math.ceil(H / tall) + 1, cols = Math.ceil(W / (side * .5)) + 2;
  const accents = [Palette.Coral, Palette.Teal, Palette.Marigold, Palette.Cobalt, Palette.Mint];
  let cover = 0, tiles = 0;
  for (let row = 0; row < rows; ++row)
    for (let col = -1; col <= cols; ++col) {
      const up = ((col + row) & 1) === 0;
      const x = col * side * .5, y0 = row * tall, y1 = y0 + tall;
      const A = up ? V(x - side * .5, y1) : V(x - side * .5, y0);
      const B = up ? V(x + side * .5, y1) : V(x + side * .5, y0);
      const tip = up ? V(x, y0) : V(x, y1);
      const mid = mul(add(add(A, B), tip), 1 / 3);
      const id = ((row + 7) * 997 + col + 13) >>> 0;
      const delay = (Ease.clamp01((mid.x + mid.y * .55) / (W + H * .55)) * .9 + hash01(id) * .1) * sweep;
      let grow = 1, turn = 0;
      if (stage === 'closing') {
        const q = Ease.clamp01((age - delay) / tileTime);
        grow = Ease.outBack(q, .9);
        turn = (1 - Ease.outCubic(q)) * 50;
      } else if (opening) {
        const q = Ease.clamp01((age - delay) / tileTime);
        grow = 1 - Ease.inBack(q, .8);
        turn = -Ease.inCubic(q) * 40;
      }
      ++tiles;
      cover += Ease.clamp01(grow);
      if (grow <= .01) continue;
      const rad = turn * Math.PI / 180, cs = Math.cos(rad) * grow * 1.035, sn = Math.sin(rad) * grow * 1.035;
      const place = v => { const d = sub(v, mid); return V(mid.x + d.x * cs - d.y * sn, mid.y + d.x * sn + d.y * cs); };
      const pts = [place(A), place(B), place(tip)];
      const pick = hash01(Math.imul(id, 31) + 5);
      const tint = pick < .07 ? accents[Math.floor(hash01(Math.imul(id, 7)) * 5) % 5] : pick < .2 ? Palette.PaperDeep : up ? Palette.Paper : Palette.PaperShade;
      sk.fill(pts, tint);
      // The HUD asks for 16% ink here, but UE's canvas lines come out solid: the captured game wipe shows full ink
      // edges, and the hand-over to it (td_open) has to match what is on screen, not what the code asked for.
      if (grow > .4)
        for (let q = 0; q < 3; ++q) sk.line(pts[q], pts[(q + 1) % 3], Palette.Ink, Math.max(1.2, 1.3 * k), Ease.clamp01(grow));
    }
  return tiles ? cover / tiles : 1;
}
