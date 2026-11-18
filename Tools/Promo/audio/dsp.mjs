// DSP core for the promo soundtrack: seeded randomness, filters, oscillators, envelopes, a stereo bus, Freeverb,
// dynamics (compressor, ducking, lookahead limiter), BS.1770 loudness and a float WAV writer. Everything is plain
// Float32Array processing at 48 kHz, deterministic from its seeds.
export const RATE = 48000;
export const TAU = Math.PI * 2;
export const hz = midi => 440 * Math.pow(2, (midi - 69) / 12);
export const db = d => Math.pow(10, d / 20);
export const clamp = (x, a, b) => (x < a ? a : x > b ? b : x);
export const samples = seconds => Math.max(0, Math.round(seconds * RATE));

// --- randomness ------------------------------------------------------------------------------------------------------
export class Rng {
  constructor(seed = 1) { this.s = (seed >>> 0) || 0x9E3779B9; }
  next() {
    let s = this.s;
    s ^= s << 13; s >>>= 0;
    s ^= s >>> 17;
    s ^= s << 5; s >>>= 0;
    this.s = s;
    return s / 4294967296;
  }
  bi() { return this.next() * 2 - 1; }
  range(a, b) { return a + (b - a) * this.next(); }
  int(n) { return Math.floor(this.next() * n); }
  pick(list) { return list[Math.floor(this.next() * list.length)]; }
  gauss() {
    const u = Math.max(1e-9, this.next()), v = this.next();
    return Math.sqrt(-2 * Math.log(u)) * Math.cos(TAU * v);
  }
}
export function noiseBuffer(n, seed) {
  const r = new Rng(seed), y = new Float32Array(n);
  for (let i = 0; i < n; ++i) y[i] = r.bi();
  return y;
}

// --- filters ---------------------------------------------------------------------------------------------------------
// RBJ cookbook biquad, transposed direct form II.
export class Biquad {
  constructor(type, freq, q = Math.SQRT1_2, gainDb = 0) {
    this.z1 = 0; this.z2 = 0;
    this.set(type, freq, q, gainDb);
  }
  set(type, freq, q = Math.SQRT1_2, gainDb = 0) {
    const w = TAU * clamp(freq, 5, RATE * .49) / RATE, cw = Math.cos(w), sw = Math.sin(w);
    const alpha = sw / (2 * q), A = Math.pow(10, gainDb / 40);
    let b0, b1, b2, a0, a1, a2;
    switch (type) {
      case 'lowpass': b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = b0; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
      case 'highpass': b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = b0; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
      case 'bandpass': b0 = alpha; b1 = 0; b2 = -alpha; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
      case 'notch': b0 = 1; b1 = -2 * cw; b2 = 1; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
      case 'peak': b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A; a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A; break;
      case 'lowshelf': {
        const s = 2 * Math.sqrt(A) * alpha;
        b0 = A * ((A + 1) - (A - 1) * cw + s); b1 = 2 * A * ((A - 1) - (A + 1) * cw); b2 = A * ((A + 1) - (A - 1) * cw - s);
        a0 = (A + 1) + (A - 1) * cw + s; a1 = -2 * ((A - 1) + (A + 1) * cw); a2 = (A + 1) + (A - 1) * cw - s;
        break;
      }
      case 'highshelf': {
        const s = 2 * Math.sqrt(A) * alpha;
        b0 = A * ((A + 1) + (A - 1) * cw + s); b1 = -2 * A * ((A - 1) + (A + 1) * cw); b2 = A * ((A + 1) + (A - 1) * cw - s);
        a0 = (A + 1) - (A - 1) * cw + s; a1 = 2 * ((A - 1) - (A + 1) * cw); a2 = (A + 1) - (A - 1) * cw - s;
        break;
      }
      default: throw new Error(`biquad type ${type}`);
    }
    this.b0 = b0 / a0; this.b1 = b1 / a0; this.b2 = b2 / a0; this.a1 = a1 / a0; this.a2 = a2 / a0;
    return this;
  }
  process(x) {
    const y = this.b0 * x + this.z1;
    this.z1 = this.b1 * x - this.a1 * y + this.z2;
    this.z2 = this.b2 * x - this.a2 * y;
    return y;
  }
  run(buf) {
    for (let i = 0; i < buf.length; ++i) buf[i] = this.process(buf[i]);
    return buf;
  }
}
// Applies a chain of [type, freq, q, gain] biquads to a buffer in place.
export function eq(buf, ...bands) {
  for (const b of bands) new Biquad(...b).run(buf);
  return buf;
}
// Zavalishin's topology-preserving state-variable filter: stable under fast cutoff sweeps.
export class Svf {
  constructor() { this.s1 = 0; this.s2 = 0; this.lp = 0; this.bp = 0; this.hp = 0; }
  process(x, cutoff, q = .7) {
    const g = Math.tan(Math.PI * clamp(cutoff, 10, RATE * .45) / RATE), k = 1 / q;
    const a1 = 1 / (1 + g * (g + k));
    const v3 = x - this.s2, v1 = a1 * (this.s1 + g * v3), v2 = this.s2 + g * v1;
    this.s1 = 2 * v1 - this.s1; this.s2 = 2 * v2 - this.s2;
    this.lp = v2; this.bp = v1; this.hp = x - k * v1 - v2;
    return v2;
  }
}
export class OnePole {
  constructor(fc) { this.y = 0; this.set(fc); }
  set(fc) { this.a = 1 - Math.exp(-TAU * clamp(fc, 1, RATE * .49) / RATE); return this; }
  lp(x) { return (this.y += this.a * (x - this.y)); }
  hp(x) { return x - this.lp(x); }
}
export class DcBlock {
  constructor(fc = 12) { this.r = 1 - TAU * fc / RATE; this.x1 = 0; this.y1 = 0; }
  process(x) { const y = x - this.x1 + this.r * this.y1; this.x1 = x; this.y1 = y; return y; }
}

// --- oscillators -----------------------------------------------------------------------------------------------------
function blep(t, dt) {
  if (t < dt) { t /= dt; return t + t - t * t - 1; }
  if (t > 1 - dt) { t = (t - 1) / dt; return t * t + t + t + 1; }
  return 0;
}
// Band-limited oscillator (PolyBLEP saw / square / triangle) with a free-running phase.
export class Osc {
  constructor(phase = 0) { this.p = phase; this.tri = 0; }
  saw(freq) {
    const dt = freq / RATE, p = this.p;
    this.p += dt; if (this.p >= 1) this.p -= 1;
    return 2 * p - 1 - blep(p, dt);
  }
  square(freq, width = .5) {
    const dt = freq / RATE, p = this.p;
    this.p += dt; if (this.p >= 1) this.p -= 1;
    let v = p < width ? 1 : -1;
    v += blep(p, dt);
    v -= blep((p - width + 1) % 1, dt);
    return v;
  }
  triangle(freq) {
    // Leaky-integrated band-limited square.
    const sq = this.square(freq, .5);
    this.tri = this.tri * .999 + sq * 4 * freq / RATE;
    return this.tri;
  }
  sine(freq) {
    const p = this.p;
    this.p += freq / RATE; if (this.p >= 1) this.p -= 1;
    return Math.sin(TAU * p);
  }
}

// --- envelopes -------------------------------------------------------------------------------------------------------
export const smooth = t => (t <= 0 ? 0 : t >= 1 ? 1 : t * t * (3 - 2 * t));
// Attack / hold-free decay to sustain / release at `dur`, all in seconds; returns gain at time t.
export function adsr(t, a, d, s, r, dur) {
  let g;
  if (t < 0) return 0;
  if (t < a) g = smooth(t / a);
  else if (t < a + d) g = 1 - (1 - s) * smooth((t - a) / d);
  else g = s;
  if (t > dur) g *= Math.max(0, 1 - (t - dur) / r);
  return g;
}
// Click-free edges for any one-shot buffer.
export function fadeEdges(buf, inS = .002, outS = .01) {
  const a = samples(inS), b = samples(outS), n = buf.length;
  for (let i = 0; i < a && i < n; ++i) buf[i] *= smooth(i / a);
  for (let i = 0; i < b && i < n; ++i) buf[n - 1 - i] *= smooth(i / b);
  return buf;
}
export function normalize(buf, peak = 1) {
  let m = 0;
  for (let i = 0; i < buf.length; ++i) m = Math.max(m, Math.abs(buf[i]));
  if (m > 0) for (let i = 0; i < buf.length; ++i) buf[i] *= peak / m;
  return buf;
}
export function gainBuf(buf, g) { for (let i = 0; i < buf.length; ++i) buf[i] *= g; return buf; }
export function mixInto(dst, src, at = 0, g = 1) {
  const o = samples(at);
  for (let i = 0; i < src.length; ++i) { const j = o + i; if (j >= 0 && j < dst.length) dst[j] += src[i] * g; }
  return dst;
}
export function concatLen(...bufs) { return bufs.reduce((s, b) => Math.max(s, b.length), 0); }

// --- a stereo bus ----------------------------------------------------------------------------------------------------
// Constant-power pan: -1 left, 0 centre (unity both sides), +1 right.
export function panGains(pan) {
  const th = (clamp(pan, -1, 1) + 1) * Math.PI / 4;
  return [Math.cos(th) * Math.SQRT2, Math.sin(th) * Math.SQRT2];
}
export class Bus {
  constructor(seconds) {
    const n = samples(seconds);
    this.L = new Float32Array(n); this.R = new Float32Array(n);
    this.length = n;
  }
  // A mono buffer at `at` seconds, gain and pan; `width` spreads a mono source with a tiny Haas offset.
  add(buf, at, gain = 1, pan = 0) {
    if (!buf) return;
    if (buf.L) return this.addStereo(buf.L, buf.R, at, gain, pan);
    const o = samples(at), [gl, gr] = panGains(pan), L = this.L, R = this.R, n = this.length;
    const start = Math.max(0, -o), end = Math.min(buf.length, n - o);
    for (let i = start; i < end; ++i) { const v = buf[i] * gain; L[o + i] += v * gl; R[o + i] += v * gr; }
  }
  addStereo(bl, br, at, gain = 1, pan = 0) {
    const o = samples(at), n = this.length;
    const kl = gain * (pan > 0 ? 1 - pan : 1), kr = gain * (pan < 0 ? 1 + pan : 1);
    const start = Math.max(0, -o), end = Math.min(bl.length, n - o);
    for (let i = start; i < end; ++i) { this.L[o + i] += bl[i] * kl; this.R[o + i] += br[i] * kr; }
  }
  mixBus(other, gain = 1) {
    for (let i = 0; i < this.length; ++i) { this.L[i] += other.L[i] * gain; this.R[i] += other.R[i] * gain; }
  }
  peak() {
    let m = 0;
    for (let i = 0; i < this.length; ++i) m = Math.max(m, Math.abs(this.L[i]), Math.abs(this.R[i]));
    return m;
  }
}

// --- Freeverb (Jezar's public-domain algorithm) ----------------------------------------------------------------------
class Comb {
  constructor(n) { this.buf = new Float32Array(n); this.i = 0; this.store = 0; }
  process(x, feedback, damp1, damp2) {
    const out = this.buf[this.i];
    this.store = out * damp2 + this.store * damp1;
    this.buf[this.i] = x + this.store * feedback;
    if (++this.i >= this.buf.length) this.i = 0;
    return out;
  }
}
class Allpass {
  constructor(n) { this.buf = new Float32Array(n); this.i = 0; }
  process(x) {
    const b = this.buf[this.i];
    this.buf[this.i] = x + b * .5;
    if (++this.i >= this.buf.length) this.i = 0;
    return b - x;
  }
}
// Returns the wet signal only (stereo), from a stereo input bus. room 0..1, damp 0..1, width 0..1.
export function freeverb(inL, inR, { room = .7, damp = .45, width = 1, predelay = .012, lowcut = 180, highcut = 9000 } = {}) {
  const scale = RATE / 44100, spread = 23;
  const combT = [1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617], apT = [556, 441, 341, 225];
  const cl = combT.map(t => new Comb(Math.round(t * scale))), cr = combT.map(t => new Comb(Math.round((t + spread) * scale)));
  const al = apT.map(t => new Allpass(Math.round(t * scale))), ar = apT.map(t => new Allpass(Math.round((t + spread) * scale)));
  const feedback = room * .28 + .7, damp1 = damp * .4, damp2 = 1 - damp1;
  const n = inL.length, outL = new Float32Array(n), outR = new Float32Array(n);
  const pd = samples(predelay);
  const hp = new Biquad('highpass', lowcut, .7), lp = new Biquad('lowpass', highcut, .7);
  const w1 = width / 2 + .5, w2 = (1 - width) / 2;
  for (let i = 0; i < n; ++i) {
    const j = i - pd;
    let x = j >= 0 ? (inL[j] + inR[j]) * .015 : 0;
    x = lp.process(hp.process(x));
    let l = 0, r = 0;
    for (let k = 0; k < 8; ++k) { l += cl[k].process(x, feedback, damp1, damp2); r += cr[k].process(x, feedback, damp1, damp2); }
    for (let k = 0; k < 4; ++k) { l = al[k].process(l); r = ar[k].process(r); }
    outL[i] = l * w1 + r * w2;
    outR[i] = r * w1 + l * w2;
  }
  return { L: outL, R: outR };
}

// --- dynamics --------------------------------------------------------------------------------------------------------
const coef = ms => Math.exp(-1 / (Math.max(.01, ms) * RATE / 1000));
// Stereo-linked feed-forward compressor with a soft knee; returns gain reduction applied in place.
export function compress(L, R, { threshold = -18, ratio = 2.5, attack = 10, release = 120, knee = 6, makeup = 0 } = {}) {
  const ca = coef(attack), cr = coef(release), mk = db(makeup);
  let env = 0;
  for (let i = 0; i < L.length; ++i) {
    const lvl = Math.max(Math.abs(L[i]), Math.abs(R[i]));
    env = lvl > env ? ca * env + (1 - ca) * lvl : cr * env + (1 - cr) * lvl;
    const x = 20 * Math.log10(env + 1e-9);
    let over = x - threshold, gr = 0;
    if (over > knee / 2) gr = over * (1 - 1 / ratio);
    else if (over > -knee / 2) { over += knee / 2; gr = (over * over) / (2 * knee) * (1 - 1 / ratio); }
    const g = db(-gr) * mk;
    L[i] *= g; R[i] *= g;
  }
}
// The music ducks under the effects: envelope of the key bus drives a gain on the target, down to `depth` dB.
export function duck(target, key, { depth = -6, threshold = .02, full = .25, attack = 8, release = 220 } = {}) {
  const ca = coef(attack), cr = coef(release);
  let env = 0;
  const g = new Float32Array(target.length);
  for (let i = 0; i < target.length; ++i) {
    const lvl = Math.max(Math.abs(key.L[i]), Math.abs(key.R[i]));
    env = lvl > env ? ca * env + (1 - ca) * lvl : cr * env + (1 - cr) * lvl;
    const k = clamp((env - threshold) / (full - threshold), 0, 1);
    g[i] = db(depth * smooth(k));
    target.L[i] *= g[i]; target.R[i] *= g[i];
  }
  return g;
}
// The true (inter-sample) peak round each sample: the sample itself and three points between it and the next,
// interpolated with a 16-tap Hann-windowed sinc (4x oversampling, as BS.1770 true-peak meters do).
const TruePeakTaps = 8;
const TruePeakPhases = [.25, .5, .75].map(f => {
  const c = [];
  for (let k = -TruePeakTaps + 1; k <= TruePeakTaps; ++k) {
    const d = f - k, sinc = Math.abs(d) < 1e-9 ? 1 : Math.sin(Math.PI * d) / (Math.PI * d);
    c.push(sinc * (.5 + .5 * Math.cos(Math.PI * d / TruePeakTaps)));
  }
  return c;
});
export function truePeaks(x) {
  const n = x.length, out = new Float32Array(n);
  for (let i = 0; i < n; ++i) {
    let m = Math.abs(x[i]);
    if (i >= TruePeakTaps && i + TruePeakTaps < n)
      for (const c of TruePeakPhases) {
        let v = 0;
        for (let k = 0; k < c.length; ++k) v += c[k] * x[i - TruePeakTaps + 1 + k];
        m = Math.max(m, Math.abs(v));
      }
    out[i] = m;
  }
  return out;
}
// Lookahead true-peak limiter: never lets the signal (between samples too) past `ceiling`; gain dips are smoothed in
// over the lookahead window.
export function limit(L, R, { ceiling = db(-1), lookahead = .003, release = .08 } = {}) {
  const n = L.length, la = samples(lookahead), cr = Math.exp(-1 / (release * RATE));
  const need = new Float32Array(n), pl = truePeaks(L), pr = truePeaks(R);
  for (let i = 0; i < n; ++i) {
    const p = Math.max(pl[i], pr[i], pl[i + 1] ?? 0, pr[i + 1] ?? 0);
    need[i] = p > ceiling ? ceiling / p : 1;
  }
  // Minimum over the lookahead window (a simple sliding minimum is fine at this size).
  const want = new Float32Array(n);
  const dq = [];
  for (let i = 0; i < n + la; ++i) {
    if (i < n) { while (dq.length && need[dq[dq.length - 1]] >= need[i]) dq.pop(); dq.push(i); }
    const k = i - la;
    if (k >= 0) { while (dq.length && dq[0] < k) dq.shift(); want[k] = dq.length ? need[dq[0]] : 1; }
  }
  let g = 1;
  for (let i = 0; i < n; ++i) {
    const w = want[i];
    g = w < g ? w : cr * g + (1 - cr) * w;
    // Ramp into the dip across the lookahead so the reduction itself does not click.
    L[i] *= g; R[i] *= g;
  }
  for (let i = 0; i < n; ++i) { L[i] = clamp(L[i], -ceiling, ceiling); R[i] = clamp(R[i], -ceiling, ceiling); }
}
export const softClip = x => (Math.abs(x) < .6 ? x : Math.sign(x) * (.6 + .4 * Math.tanh((Math.abs(x) - .6) / .4)));

// --- loudness (ITU-R BS.1770-4, integrated, gated) -------------------------------------------------------------------
export function loudness(L, R) {
  const kw = buf => {
    const s1 = new Biquad('highshelf', 1681.97, .7072, 3.99984), s2 = new Biquad('highpass', 38.135, .5003);
    const y = new Float32Array(buf.length);
    for (let i = 0; i < buf.length; ++i) y[i] = s2.process(s1.process(buf[i]));
    return y;
  };
  const kl = kw(L), kr = kw(R);
  const block = samples(.4), hop = samples(.1), z = [];
  for (let s = 0; s + block <= kl.length; s += hop) {
    let a = 0;
    for (let i = s; i < s + block; ++i) a += kl[i] * kl[i] + kr[i] * kr[i];
    z.push(a / block);
  }
  const lk = v => -.691 + 10 * Math.log10(v + 1e-12);
  const abs = z.filter(v => lk(v) > -70);
  if (!abs.length) return -70;
  const rel = lk(abs.reduce((s, v) => s + v, 0) / abs.length) - 10;
  const gated = abs.filter(v => lk(v) > rel);
  return lk(gated.reduce((s, v) => s + v, 0) / gated.length);
}

// --- output ----------------------------------------------------------------------------------------------------------
export function writeWavFloat(file, L, R, fs) {
  const n = L.length, data = n * 8;
  const buf = Buffer.alloc(44 + data);
  buf.write('RIFF', 0); buf.writeUInt32LE(36 + data, 4); buf.write('WAVE', 8);
  buf.write('fmt ', 12); buf.writeUInt32LE(16, 16); buf.writeUInt16LE(3, 20); buf.writeUInt16LE(2, 22);
  buf.writeUInt32LE(RATE, 24); buf.writeUInt32LE(RATE * 8, 28); buf.writeUInt16LE(8, 32); buf.writeUInt16LE(32, 34);
  buf.write('data', 36); buf.writeUInt32LE(data, 40);
  for (let i = 0; i < n; ++i) { buf.writeFloatLE(L[i], 44 + i * 8); buf.writeFloatLE(R[i], 48 + i * 8); }
  fs.writeFileSync(file, buf);
}
