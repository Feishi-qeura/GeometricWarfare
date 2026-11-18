// The promo's soundtrack: the score (audio/score.mjs: ukulele, whistle and friends at 120 BPM on the cut) and the
// sound effects (audio/foley.mjs: paper, pencil and stamps for the motion graphics; the game's own events from the
// capture's sound sheets) mixed and mastered. Deterministic. Writes Saved/Promo/promo-audio.wav (48 kHz stereo float,
// -14 LUFS, true peaks under -1.6 dBTP) and the two stems beside it (promo-music.wav, promo-sfx.wav, before the master).
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import * as TL from './web/timeline.js';
import { RATE, Bus, freeverb, duck, compress, limit, loudness, db, eq, smooth, truePeaks, writeWavFloat } from './audio/dsp.mjs';
import { renderScore } from './audio/score.mjs';
import { renderFoley } from './audio/foley.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const project = path.resolve(here, '..', '..');
const out = path.join(project, 'Saved', 'Promo');
const plates = process.env.PROMO_PLATES || path.join(project, 'Saved', 'PromoPackage', 'Windows', 'GeometricWarfare', 'Saved', 'Promo', 'Plates');
// Loudness of each bus before they meet (LUFS), and of the finished track; its true-peak ceiling leaves room for the
// AAC encoder's overshoot.
const MusicLufs = -18, SfxLufs = -17.5, MasterLufs = -14, CeilingDbtp = -1.6;

const started = Date.now();
const LEN = TL.END;
const score = renderScore(LEN);
const fx = renderFoley(LEN, plates);
console.log(`rendered score and effects in ${((Date.now() - started) / 1000).toFixed(1)} s`);

// Reverb returns: a warm hall for the band, a small room for the effects.
const hall = freeverb(score.wet.L, score.wet.R, { room: .8, damp: .42, width: 1, predelay: .022, lowcut: 220, highcut: 8000 });
const room = freeverb(fx.wet.L, fx.wet.R, { room: .5, damp: .55, width: .8, predelay: .006, lowcut: 250, highcut: 7000 });
const music = new Bus(LEN), effects = new Bus(LEN);
for (let i = 0; i < music.length; ++i) {
  music.L[i] = score.dry.L[i] + hall.L[i]; music.R[i] = score.dry.R[i] + hall.R[i];
  effects.L[i] = fx.dry.L[i] + room.L[i]; effects.R[i] = fx.dry.R[i] + room.R[i];
}
for (const ch of [music.L, music.R]) eq(ch, ['highpass', 32, .7], ['peak', 320, .9, -1.5]);
for (const ch of [effects.L, effects.R]) eq(ch, ['highpass', 38, .7]);

// Balance the two buses by loudness, then let the music make room under the effects.
const lm = loudness(music.L, music.R), lf = loudness(effects.L, effects.R);
const gm = db(MusicLufs - lm), gf = db(SfxLufs - lf);
for (let i = 0; i < music.length; ++i) { music.L[i] *= gm; music.R[i] *= gm; effects.L[i] *= gf; effects.R[i] *= gf; }
writeWavFloat(path.join(out, 'promo-music.wav'), music.L, music.R, fs);
writeWavFloat(path.join(out, 'promo-sfx.wav'), effects.L, effects.R, fs);
duck(music, effects, { depth: -5, threshold: .04, full: .35, attack: 8, release: 260 });

// The master: sum, glue, a little air, the ends faded (the picture settles to paper over the last half second).
const L = new Float32Array(music.length), R = new Float32Array(music.length);
for (let i = 0; i < L.length; ++i) { L[i] = music.L[i] + effects.L[i]; R[i] = music.R[i] + effects.R[i]; }
compress(L, R, { threshold: -16, ratio: 2, attack: 15, release: 180, knee: 8 });
eq(L, ['highshelf', 9000, .7, 1], ['lowshelf', 90, .7, .5]);
eq(R, ['highshelf', 9000, .7, 1], ['lowshelf', 90, .7, .5]);
for (let i = 0; i < L.length; ++i) {
  const t = i / RATE, g = smooth(t / .03) * (1 - smooth((t - (LEN - .7)) / .7));
  L[i] *= g; R[i] *= g;
}
const before = loudness(L, R), gain = db(MasterLufs - before);
for (let i = 0; i < L.length; ++i) { L[i] *= gain; R[i] *= gain; }
limit(L, R, { ceiling: db(CeilingDbtp), lookahead: .004, release: .09 });
const final = loudness(L, R);
let peak = 0;
const tpl = truePeaks(L), tpr = truePeaks(R);
for (let i = 0; i < L.length; ++i) peak = Math.max(peak, tpl[i], tpr[i]);
fs.mkdirSync(out, { recursive: true });
writeWavFloat(path.join(out, 'promo-audio.wav'), L, R, fs);
console.log(`music ${lm.toFixed(1)} -> ${MusicLufs} LUFS, effects ${lf.toFixed(1)} -> ${SfxLufs} LUFS; master ${final.toFixed(1)} LUFS, ` +
  `true peak ${(20 * Math.log10(peak)).toFixed(1)} dBTP; wrote promo-audio.wav (+ stems) in ${((Date.now() - started) / 1000).toFixed(1)} s`);
