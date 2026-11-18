// Builds the soundtrack for Geometric Warfare's new promo video using the game's real 112-BPM stems,
// real in-game WAV sound effects (Assets/Audio/GeometricWarfare), and tactile paper/pencil/stamp foley.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import * as TL from './src/timeline.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const project = path.resolve(here, '..', '..');
const audioDir = path.join(project, 'Assets', 'Audio', 'GeometricWarfare');
const outDir = path.join(project, 'output');

const RATE = 48000;
const TOTAL_SAMPLES = Math.round(TL.END * RATE);
const musL = new Float32Array(TOTAL_SAMPLES);
const musR = new Float32Array(TOTAL_SAMPLES);
const sfxL = new Float32Array(TOTAL_SAMPLES);
const sfxR = new Float32Array(TOTAL_SAMPLES);

let rngState = 20261115;
function rand() {
  rngState ^= rngState << 13; rngState >>>= 0;
  rngState ^= rngState >>> 17;
  rngState ^= rngState << 5; rngState >>>= 0;
  return (rngState / 4294967296) * 2 - 1;
}
const clamp = (x, a, b) => Math.max(a, Math.min(b, x));

function readWav(name) {
  const file = path.join(audioDir, `${name}.wav`);
  const buf = fs.readFileSync(file);
  let p = 12, channels = 2, rate = 48000, bits = 16, data = null;
  while (p < buf.length) {
    const id = buf.toString('ascii', p, p + 4);
    const sz = buf.readUInt32LE(p + 4);
    if (id === 'fmt ') {
      channels = buf.readUInt16LE(p + 10);
      rate = buf.readUInt32LE(p + 12);
      bits = buf.readUInt16LE(p + 22);
    } else if (id === 'data') {
      data = buf.subarray(p + 8, p + 8 + sz);
      break;
    }
    p += 8 + sz + (sz % 2);
  }
  const frames = Math.floor(data.length / ((bits / 8) * channels));
  const L = new Float32Array(frames);
  const R = new Float32Array(frames);
  for (let i = 0; i < frames; i++) {
    const off = i * channels * 2;
    const l = data.readInt16LE(off) / 32768;
    const r = channels > 1 ? data.readInt16LE(off + 2) / 32768 : l;
    L[i] = l;
    R[i] = r;
  }
  return { L, R, frames, rate };
}

// Mix a segment of a music stem onto the music bus with smooth edge crossfades
function placeMusic(stem, outStartSec, durSec, srcOffsetSec = 0, gain = 1.0, fadeInSec = 0.05, fadeOutSec = 0.05) {
  const outStart = Math.round(outStartSec * RATE);
  const srcStart = Math.round(srcOffsetSec * RATE);
  const len = Math.round(durSec * RATE);
  const fi = Math.max(1, Math.round(fadeInSec * RATE));
  const fo = Math.max(1, Math.round(fadeOutSec * RATE));
  for (let i = 0; i < len; i++) {
    const dst = outStart + i;
    const src = (srcStart + i) % stem.frames;
    if (dst < 0 || dst >= TOTAL_SAMPLES) continue;
    let env = gain;
    if (i < fi) env *= Math.sin((i / fi) * Math.PI * 0.5);
    if (i > len - fo) env *= Math.sin(((len - i) / fo) * Math.PI * 0.5);
    musL[dst] += stem.L[src] * env;
    musR[dst] += stem.R[src] * env;
  }
}

// Load real game music stems (all 112 BPM!)
const hostAssist = readWav('HostAssist');
const battle = readWav('Battle');
const boss = readWav('Boss');
const results = readWav('Results');

// Arrange music on exact 112 BPM bar boundaries
placeMusic(hostAssist, TL.bar(0), TL.bar(2), 0, 1.05, 0.02, 0.06);
placeMusic(battle, TL.bar(2), TL.bar(6), 0, 1.05, 0.06, 0.06);
placeMusic(boss, TL.bar(8), TL.bar(2), TL.bar(6), 1.08, 0.06, 0.06);
placeMusic(battle, TL.bar(10), TL.bar(6), TL.bar(8), 1.08, 0.06, 0.08);
placeMusic(results, TL.bar(16), TL.bar(8), 0, 0.85, 0.08, 0.06);
placeMusic(results, TL.bar(24), TL.bar(8), 0, 0.80, 0.06, 0.04);
placeMusic(results, TL.bar(32), TL.bar(2), 0, 0.88, 0.04, 0.08);
placeMusic(battle, TL.bar(34), TL.bar(2), TL.bar(14), 1.08, 0.08, 0.06);
placeMusic(hostAssist, TL.bar(36), TL.bar(2), 0, 1.08, 0.06, 0.75);

// ---- Procedural Paper / Pencil / Stamp Foley ------------------------------------------------------------------------
function synthMono(durSec, fn) {
  const n = Math.round(durSec * RATE);
  const y = new Float32Array(n);
  const st = {};
  for (let i = 0; i < n; i++) y[i] = fn(i / RATE, i, st);
  return y;
}

const foley = {
  pop: (pitch = 1) => synthMono(0.11, t => Math.sin(2 * Math.PI * (640 * pitch * t - 1800 * t * t)) * Math.exp(-t * 36) * 0.85),
  stamp: () => synthMono(0.32, (t, _, s) => {
    s.lp = (s.lp || 0) + (rand() - (s.lp || 0)) * 0.09;
    return (Math.sin(2 * Math.PI * (88 * t - 55 * t * t)) * Math.exp(-t * 16) * 0.9 + s.lp * Math.exp(-t * 34) * 1.9 + rand() * Math.exp(-t * 85) * 0.25);
  }),
  paperSlide: () => synthMono(0.28, (t, _, s) => {
    s.lp = (s.lp || 0) + (rand() - (s.lp || 0)) * 0.14;
    return s.lp * Math.sin(Math.PI * Math.min(1, t / 0.28)) * 0.95;
  }),
  paperDrop: () => synthMono(0.28, (t, _, s) => {
    s.lp = (s.lp || 0) + (rand() - (s.lp || 0)) * 0.07;
    return s.lp * Math.exp(-t * 16) * 2.2 + Math.sin(2 * Math.PI * 74 * t) * Math.exp(-t * 22) * 0.45;
  }),
  pageTurn: () => synthMono(0.42, (t, _, s) => {
    const u = clamp(t / 0.42, 0, 1);
    s.lp = (s.lp || 0) + (rand() - (s.lp || 0)) * (0.04 + 0.22 * u);
    const hp = s.lp - (s.prev || 0);
    s.prev = s.lp;
    return (s.lp * 0.65 + hp * 1.8) * Math.pow(Math.sin(Math.PI * u), 1.3) * 0.9;
  }),
  tape: () => synthMono(0.28, (t, _, s) => {
    const r = rand();
    const hp = r - (s.prev || 0);
    s.prev = r;
    return hp * (0.5 + 0.5 * Math.sin(2 * Math.PI * (42 + 55 * t) * t)) * Math.exp(-t * 7) * 0.55;
  }),
  confetti: () => synthMono(0.75, t => {
    let v = 0;
    for (let k = 0; k < 20; k++) {
      const at = Math.abs(Math.sin(k * 19.37)) * 0.55;
      const d = t - at;
      if (d >= 0 && d < 0.02) v += Math.sin(2 * Math.PI * (2800 + 380 * (k % 5)) * d) * Math.exp(-d * 280) * 0.36;
    }
    return v;
  }),
  scribble: () => synthMono(0.48, (t, _, s) => {
    const r = rand();
    const hp = r - (s.prev || 0);
    s.prev = r;
    const stroke = Math.max(0, Math.sin(2 * Math.PI * 10.5 * t + 1.8 * Math.sin(2 * Math.PI * 3 * t)));
    return hp * stroke * 0.30 * Math.min(1, t / 0.025) * Math.min(1, (0.48 - t) / 0.08);
  }),
  pencilTick: () => synthMono(0.16, (t, _, s) => {
    const r = rand();
    const hp = r - (s.prev || 0);
    s.prev = r;
    const burst = t < 0.045 || (t > 0.07 && t < 0.12) ? 1 : 0;
    return hp * burst * 0.28;
  }),
};

const wavCache = new Map();
function getSfx(name, pitch = 1) {
  const key = `${name}:${pitch.toFixed(2)}`;
  if (wavCache.has(key)) return wavCache.get(key);
  let mono;
  if (foley[name]) {
    mono = foley[name](pitch);
  } else {
    const w = readWav(name);
    const maxLen = Math.min(w.frames, Math.round(RATE * 1.4));
    mono = new Float32Array(maxLen);
    if (Math.abs(pitch - 1) < 0.01) {
      for (let i = 0; i < maxLen; i++) mono[i] = w.L[i];
    } else {
      const outLen = Math.min(Math.round(maxLen / pitch), RATE * 1.4);
      mono = new Float32Array(outLen);
      for (let i = 0; i < outLen; i++) {
        const idx = i * pitch;
        const i0 = Math.floor(idx);
        const f = idx - i0;
        const s0 = w.L[Math.min(w.frames - 1, i0)];
        const s1 = w.L[Math.min(w.frames - 1, i0 + 1)];
        mono[i] = s0 + (s1 - s0) * f;
      }
    }
    const tail = Math.min(2000, mono.length);
    for (let i = 0; i < tail; i++) mono[mono.length - 1 - i] *= i / tail;
  }
  wavCache.set(key, mono);
  return mono;
}

for (const cue of TL.audioCues()) {
  const y = getSfx(cue.fx, cue.pitch || 1);
  const start = Math.round(cue.t * RATE);
  const pan = rand() * 0.28;
  const gL = 0.26 * cue.gain * (1 - pan * 0.5);
  const gR = 0.26 * cue.gain * (1 + pan * 0.5);
  for (let i = 0; i < y.length; i++) {
    const j = start + i;
    if (j < 0 || j >= TOTAL_SAMPLES) continue;
    sfxL[j] += y[i] * gL;
    sfxR[j] += y[i] * gR;
  }
}

// ---- Final Mix & Soft Limit -----------------------------------------------------------------------------------------
const mixL = new Float32Array(TOTAL_SAMPLES);
const mixR = new Float32Array(TOTAL_SAMPLES);
let env = 0;
let peak = 0;

for (let i = 0; i < TOTAL_SAMPLES; i++) {
  const sLvl = Math.max(Math.abs(sfxL[i]), Math.abs(sfxR[i]));
  env = Math.max(sLvl, env * 0.9994);
  const duck = 1 - Math.min(0.32, env * 1.1);
  const t = i / RATE;
  const fadeIn = Math.min(1, t / 0.04);
  const fadeOut = Math.min(1, (TL.END - t) / 0.8);

  // Boost music cleanly relative to game stems' -18dB headroom
  const l = (musL[i] * 1.65 * duck + sfxL[i] * 1.25) * fadeIn * fadeOut;
  const r = (musR[i] * 1.65 * duck + sfxR[i] * 1.25) * fadeIn * fadeOut;
  mixL[i] = l;
  mixR[i] = r;
  peak = Math.max(peak, Math.abs(l), Math.abs(r));
}

const norm = 0.92 / Math.max(peak, 1e-6);
const soft = x => Math.tanh(x * 1.2) / Math.tanh(1.2);
const outBuf = Buffer.alloc(44 + TOTAL_SAMPLES * 4);
outBuf.write('RIFF', 0);
outBuf.writeUInt32LE(36 + TOTAL_SAMPLES * 4, 4);
outBuf.write('WAVE', 8);
outBuf.write('fmt ', 12);
outBuf.writeUInt32LE(16, 16);
outBuf.writeUInt16LE(1, 20);
outBuf.writeUInt16LE(2, 22);
outBuf.writeUInt32LE(RATE, 24);
outBuf.writeUInt32LE(RATE * 4, 28);
outBuf.writeUInt16LE(4, 32);
outBuf.writeUInt16LE(16, 34);
outBuf.write('data', 36);
outBuf.writeUInt32LE(TOTAL_SAMPLES * 4, 40);

for (let i = 0; i < TOTAL_SAMPLES; i++) {
  const l = Math.round(clamp(soft(mixL[i] * norm), -1, 1) * 32000);
  const r = Math.round(clamp(soft(mixR[i] * norm), -1, 1) * 32000);
  outBuf.writeInt16LE(l, 44 + i * 4);
  outBuf.writeInt16LE(r, 46 + i * 4);
}

fs.mkdirSync(outDir, { recursive: true });
const outFile = path.join(outDir, 'promo-audio.wav');
fs.writeFileSync(outFile, outBuf);
console.log(`Generated ${outFile} (${TL.END.toFixed(2)}s, peak=${peak.toFixed(3)})`);
