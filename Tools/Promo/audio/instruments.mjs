// The band: every instrument renders one note (or one phrase, for the breathy leads) into a Float32Array at 48 kHz.
// Plucked strings are extended Karplus-Strong, mallets are modal (inharmonic decaying partials), the whistle and the
// melodica are continuous phrase voices with portamento and delayed vibrato, the kit is synthesized from noise and
// pitched sweeps. Seeds make every render identical.
import { RATE, TAU, hz, Rng, Biquad, Svf, OnePole, Osc, adsr, smooth, fadeEdges, samples, clamp, db, eq, normalize } from './dsp.mjs';

// --- plucked strings (extended Karplus-Strong) ------------------------------------------------------------------------
// A noise burst in a tuned delay loop with a one-zero loss filter (warmth), a first-order allpass for exact tuning, a
// pick-position comb on the excitation and a one-pole brightness filter. `decay` is the T60 in seconds.
export function pluck(freq, dur, { decay = 2.4, bright = .5, pick = .2, loss = .45, seed = 1, release = .04, level = 1 } = {}) {
  const n = samples(dur + release), y = new Float32Array(n);
  const period = RATE / freq, S = clamp(loss, .05, .5);
  let N = Math.floor(period - S), d = period - S - N;
  if (d < .1) { d += 1; N -= 1; }
  N = Math.max(2, N);
  const C = (1 - d) / (1 + d), rho = Math.pow(10, -3 / (freq * decay));
  const r = new Rng(seed), ex = new Float32Array(N), line = new Float32Array(N);
  const lp = new OnePole(clamp(freq * (2.5 + 16 * bright), 500, 14000)), lp2 = new OnePole(clamp(freq * (4 + 22 * bright), 700, 16000));
  for (let k = 0; k < N; ++k) ex[k] = lp2.lp(lp.lp(r.bi()));
  const P = Math.max(1, Math.round(pick * N));
  let mean = 0;
  for (let k = 0; k < N; ++k) { line[k] = ex[k] - .85 * (k >= P ? ex[k - P] : 0); mean += line[k]; }
  mean /= N;
  let peak = 1e-9;
  for (let k = 0; k < N; ++k) { line[k] -= mean; peak = Math.max(peak, Math.abs(line[k])); }
  for (let k = 0; k < N; ++k) line[k] *= level / peak;
  let idx = 0, prev = 0, apx = 0, apy = 0;
  const rel0 = samples(dur), rel = Math.max(1, samples(release));
  for (let i = 0; i < n; ++i) {
    const x = line[idx];
    const lf = rho * ((1 - S) * x + S * prev);
    prev = x;
    const ap = C * lf + apx - C * apy;
    apx = lf; apy = ap;
    line[idx] = ap;
    if (++idx >= N) idx = 0;
    y[i] = i < rel0 ? x : x * (1 - smooth((i - rel0) / rel));
  }
  // A plucked string's first instant is the sharpest: a 1 ms ramp keeps it from clicking.
  return fadeEdges(y, .0008, .004);
}

// The ukulele: re-entrant G4 C4 E4 A4, chords as fret shapes, strummed down (G to A) or up (A to G) with a small
// spread between strings, through a body resonance. A muted strum is a short percussive chop.
export const UkeStrings = [67, 60, 64, 69];
export const UkeChords = {
  C: [0, 0, 0, 3], C7: [0, 0, 0, 1], Cmaj7: [0, 0, 0, 2], C6: [0, 0, 0, 0], D: [2, 2, 2, 0], D7: [2, 2, 2, 3], Dm: [2, 2, 1, 0],
  Dm7: [2, 2, 1, 3], E7: [1, 2, 0, 2], Em: [0, 4, 3, 2], E: [1, 4, 0, 2], F: [2, 0, 1, 0], Fm: [1, 0, 1, 3], Fmaj7: [2, 4, 1, 3],
  G: [0, 2, 3, 2], G7: [0, 2, 1, 2], Gsus: [0, 2, 3, 3], Gm7: [0, 2, 1, 1], A: [2, 1, 0, 0], A7: [0, 1, 0, 0], Am: [2, 0, 0, 0], Am7: [0, 0, 0, 0],
  Bb: [3, 2, 1, 1], Bm: [4, 2, 2, 2], B7: [2, 3, 2, 2], 'F#m': [2, 1, 2, 0], Eb: [0, 3, 3, 1], Ab: [5, 3, 4, 3],
};
function ukeBody(buf) {
  return eq(buf, ['highpass', 130, .7], ['peak', 290, 2.2, 4.5], ['peak', 560, 2.0, 2.5], ['peak', 1500, 1.2, -2], ['peak', 3200, 1.0, 1.5],
    ['lowpass', 9500, .7]);
}
export function ukeStrum(chord, { dir = 'down', vel = 1, mute = false, ring = .6, spread = .011, seed = 1, strings = null } = {}) {
  const frets = UkeChords[chord];
  if (!frets) throw new Error(`uke chord ${chord}`);
  const r = new Rng(seed);
  const order = dir === 'down' ? [0, 1, 2, 3] : [3, 2, 1, 0];
  const use = strings || (dir === 'up' ? [3, 2, 1] : [0, 1, 2, 3]);
  const len = mute ? .12 : ring + .25;
  const out = new Float32Array(samples(len + .1));
  let at = 0;
  for (const s of order) {
    if (!use.includes(s)) continue;
    const f = hz(UkeStrings[s] + frets[s]);
    const amp = vel * (dir === 'up' ? .62 : 1) * (.85 + .3 * r.next()) * (s === order[0] ? 1.05 : 1);
    const note = pluck(f, mute ? .05 : ring, {
      decay: mute ? .09 : 1.25 + .4 * r.next(), bright: (mute ? .18 : dir === 'up' ? .62 : .48) * (.85 + .3 * vel), pick: .14 + .1 * r.next(),
      loss: mute ? .5 : .42, seed: seed * 31 + s * 7 + 1, release: mute ? .03 : .06,
    });
    const o = samples(at);
    for (let i = 0; i < note.length && o + i < out.length; ++i) out[o + i] += note[i] * amp;
    at += spread * (.75 + .5 * r.next()) * (dir === 'up' ? .8 : 1);
  }
  if (mute) {
    // The palm slap: a dull knock under the chop.
    const k = new OnePole(900), rr = new Rng(seed + 5);
    for (let i = 0; i < samples(.03); ++i) out[i] += k.lp(rr.bi()) * Math.exp(-i / RATE / .006) * .9 * vel;
  }
  return ukeBody(out);
}

// --- bass ------------------------------------------------------------------------------------------------------------
// A round upright-style pluck: harmonic partials that darken as they ring, a finger thump and a small pitch settle.
export function bass(midi, dur, { vel = 1, seed = 1 } = {}) {
  const f = hz(midi), len = dur + .08, n = samples(len), y = new Float32Array(n);
  const amps = [1, .55, .28, .14, .07, .035], r = new Rng(seed);
  const ph = amps.map(() => r.next() * TAU * .1);
  for (let i = 0; i < n; ++i) {
    const t = i / RATE, bend = 1 + .009 * Math.exp(-t / .035);
    let v = 0;
    for (let k = 0; k < amps.length; ++k) {
      const fk = f * (k + 1) * bend;
      if (fk > 5000) break;
      v += amps[k] * Math.sin(TAU * fk * t + ph[k]) * Math.exp(-t / (.85 / Math.pow(k + 1, .8)));
    }
    const env = smooth(t / .006) * (t > dur ? Math.max(0, 1 - (t - dur) / .08) : 1);
    y[i] = v * env * vel * .55;
  }
  const thump = new OnePole(350);
  for (let i = 0; i < samples(.04); ++i) y[i] += thump.lp(r.bi()) * Math.exp(-i / RATE / .01) * .5 * vel;
  return eq(y, ['highpass', 32, .7], ['lowpass', 2600, .7]);
}
// The tuba: an oom-pah bass for the party section (brassy pulse through an opening lowpass).
export function tuba(midi, dur, { vel = 1, seed = 1 } = {}) {
  const f = hz(midi), n = samples(dur + .1), y = new Float32Array(n), o = new Osc(new Rng(seed).next()), sv = new Svf();
  for (let i = 0; i < n; ++i) {
    const t = i / RATE, env = adsr(t, .03, .12, .7, .08, dur);
    const bend = Math.pow(2, -.4 / 12 * Math.exp(-t / .04));
    y[i] = sv.process(o.saw(f * bend) * .6, f * (1.6 + 4 * env) + 120, .75) * env * vel;
  }
  return eq(y, ['highpass', 30, .7], ['peak', 220, 1.2, 2]);
}

// --- mallets and bells (modal) ---------------------------------------------------------------------------------------
// Sum of decaying partials {r: ratio, a: amplitude, t: T60 seconds}, a short raised-cosine strike, a mallet tick.
export function modal(freq, dur, modes, { strike = .0015, tick = 0, tickHz = 5000, seed = 1, vel = 1 } = {}) {
  const n = samples(dur), y = new Float32Array(n), r = new Rng(seed);
  for (const m of modes) {
    const f = freq * m.r;
    if (f > RATE * .45) continue;
    const tau = m.t / 6.91, w = TAU * f / RATE, ph = r.next() * .2;
    let a = m.a * vel;
    const k = Math.exp(-1 / (tau * RATE));
    for (let i = 0; i < n; ++i) { y[i] += a * Math.sin(w * i + ph); a *= k; }
  }
  const sN = Math.max(1, samples(strike));
  for (let i = 0; i < sN && i < n; ++i) y[i] *= .5 - .5 * Math.cos(Math.PI * i / sN);
  if (tick > 0) {
    const hp = new Biquad('bandpass', tickHz, .8);
    for (let i = 0; i < samples(.008) && i < n; ++i) y[i] += hp.process(r.bi()) * tick * vel * Math.exp(-i / RATE / .0015);
  }
  return fadeEdges(y, 0, .02);
}
export function glock(midi, { vel = 1, len = 2.2, seed = 1 } = {}) {
  const f = hz(midi), hi = clamp((midi - 72) / 36, 0, 1);
  return modal(f, len, [{ r: 1, a: 1, t: 2.4 - 1.1 * hi }, { r: 2.756, a: .3, t: .55 }, { r: 5.404, a: .12, t: .22 }, { r: 8.933, a: .05, t: .1 }],
    { strike: .0008, tick: .25, tickHz: 7000, seed, vel });
}
export function marimba(midi, { vel = 1, len = 1.4, seed = 1 } = {}) {
  const f = hz(midi), lo = clamp((72 - midi) / 24, 0, 1);
  const y = modal(f, len, [{ r: 1, a: 1, t: .7 + .9 * lo }, { r: 3.93, a: .22 * vel, t: .14 }, { r: 9.2, a: .05 * vel, t: .05 }, { r: 2.01, a: .04, t: .3 }],
    { strike: .0035, tick: .12, tickHz: 2200, seed, vel });
  return eq(y, ['highpass', 90, .7]);
}
export function musicBox(midi, { vel = 1, len = 2.0, seed = 1 } = {}) {
  const f = hz(midi);
  const y = modal(f, len, [{ r: 1, a: 1, t: 2.2 }, { r: 1.0018, a: .35, t: 1.8 }, { r: 4.07, a: .14, t: .5 }, { r: 10.2, a: .04, t: .12 }],
    { strike: .0006, tick: .3, tickHz: 9000, seed, vel });
  return eq(y, ['highpass', 300, .7]);
}
export function celesta(midi, { vel = 1, len = 1.8, seed = 1 } = {}) {
  const f = hz(midi);
  return modal(f, len, [{ r: 1, a: 1, t: 1.7 }, { r: 2, a: .18, t: .6 }, { r: 3.01, a: .08, t: .3 }, { r: 4.13, a: .05, t: .15 }],
    { strike: .001, tick: .15, tickHz: 6000, seed, vel });
}
// A soft electric-piano chord (FM tine: 1:1 with a decaying index and a bright 14:1 tine attack).
export function epiano(midi, dur, { vel = 1 } = {}) {
  const f = hz(midi), n = samples(dur + .5), y = new Float32Array(n);
  for (let i = 0; i < n; ++i) {
    const t = i / RATE, env = Math.exp(-t / 1.1) * smooth(t / .003) * (t > dur ? Math.max(0, 1 - (t - dur) / .45) : 1);
    const idx = 1.4 * vel * Math.exp(-t / .35) + .3;
    y[i] = (Math.sin(TAU * f * t + idx * Math.sin(TAU * f * t)) + .15 * Math.sin(TAU * f * 14 * t) * Math.exp(-t / .02)) * env * vel;
  }
  return y;
}

// --- breathy leads (whole phrases) -----------------------------------------------------------------------------------
// notes: [{t, d, m, v}] start and length in seconds (relative to the phrase), MIDI pitch, velocity.
function phraseVoice(notes, opts, osc) {
  const end = Math.max(...notes.map(n => n.t + n.d)) + .3, n = samples(end), y = new Float32Array(n);
  const r = new Rng(opts.seed || 1);
  const sorted = [...notes].sort((a, b) => a.t - b.t);
  // Per sample: which note sounds, its target pitch and the amplitude envelope.
  let pitch = sorted[0].m - (opts.scoop ?? .7), amp = 0, vibPh = r.next() * TAU, wob = 0;
  const glide = Math.exp(-1 / (RATE * (opts.glide ?? .016)));
  const ampK = a => Math.exp(-1 / (RATE * a));
  let ni = 0;
  const state = { phase: 0 };
  for (let i = 0; i < n; ++i) {
    const t = i / RATE;
    while (ni + 1 < sorted.length && t >= sorted[ni + 1].t) ++ni;
    const note = sorted[ni], since = t - note.t, sounding = since >= 0 && since < note.d;
    const next = sorted[ni + 1], legato = next && next.t - (note.t + note.d) < .06;
    if (since >= 0 && since < 1 / RATE * 1.5) {
      // A detached onset scoops up into the note; a legato one glides from where the last note was.
      const prev = sorted[ni - 1];
      if (!prev || note.t - (prev.t + prev.d) > .06) pitch = note.m - (opts.scoop ?? .7);
    }
    const target = note.m;
    pitch = target + (pitch - target) * glide;
    const want = sounding || (legato && since >= 0) ? (note.v ?? 1) : 0;
    const k = want > amp ? ampK(opts.attack ?? .02) : ampK(opts.release ?? .035);
    amp = want + (amp - want) * k;
    const vibIn = smooth((since - (opts.vibDelay ?? .18)) / .25) * (note.d > .25 ? 1 : .3);
    vibPh += TAU * (opts.vibRate ?? 5.4) / RATE;
    wob += (r.bi() * .5 - wob) * .0006;
    const m = pitch + (opts.vibDepth ?? .17) * vibIn * Math.sin(vibPh);
    const f = hz(m) * (1 + wob * .002);
    y[i] = osc(f, amp, state, r, t) * (1 + .05 * wob);
  }
  return fadeEdges(y, .001, .05);
}
// A human whistle: a nearly pure tone with a whisper of breath tuned to it and a trace of second harmonic.
export function whistle(notes, opts = {}) {
  const bp = new Svf(), hiss = new Biquad('highpass', 4500, .7);
  return phraseVoice(notes, { attack: .022, release: .04, vibRate: 5.6, vibDepth: .16, scoop: .6, glide: .014, ...opts }, (f, amp, s, r) => {
    s.phase += f / RATE; if (s.phase >= 1) s.phase -= 1;
    const p = TAU * s.phase;
    const tone = Math.sin(p) + .035 * Math.sin(2 * p) + .008 * Math.sin(3 * p);
    const noise = r.bi();
    bp.process(noise, f, 9);
    return (tone + bp.bp * .2 + hiss.process(noise) * .015) * amp;
  });
}
// A melodica: a free reed, saw and narrow pulse through a key-tracked lowpass with a nasal bump, a little breath.
export function melodica(notes, opts = {}) {
  const o = new Osc(), o2 = new Osc(.37), lp = new Svf(), nasal = new Biquad('peak', 1350, 1.4, 5), air = new Biquad('bandpass', 2600, 1.2);
  const y = phraseVoice(notes, { attack: .03, release: .05, vibRate: 5.1, vibDepth: .07, vibDelay: .3, scoop: .25, glide: .02, ...opts }, (f, amp, s, r) => {
    const raw = o.saw(f) * .55 + o2.square(f * 1.001, .32) * .45;
    const v = lp.process(raw, clamp(f * 4.5, 1100, 5200) * (.6 + .4 * amp), .9);
    return (nasal.process(v) + air.process(r.bi()) * .05) * amp * .7;
  });
  return y;
}

// --- brass and pads --------------------------------------------------------------------------------------------------
// FM brass (1:1 with the index riding the envelope) for stabs and the fanfare: a scoop into pitch, bright on the push.
export function brass(midi, dur, { vel = 1, seed = 1, bright = 1 } = {}) {
  const f = hz(midi), n = samples(dur + .25), y = new Float32Array(n), r = new Rng(seed), lp = new OnePole(5200);
  const ph = r.next();
  for (let i = 0; i < n; ++i) {
    const t = i / RATE, env = adsr(t, .025, .15, .72, .14, dur);
    const bend = Math.pow(2, (-.35 * Math.exp(-t / .03) + .03 * Math.sin(TAU * 5 * t) * smooth((t - .2) / .2)) / 12);
    const index = (.8 + 2.6 * env) * bright * vel;
    const p = TAU * (f * bend * t + ph);
    y[i] = lp.lp(Math.sin(p + index * Math.sin(p))) * env * vel;
  }
  return eq(y, ['highpass', 120, .7], ['peak', 1200, 1, 2]);
}
// A soft string pad: detuned saws per note under a breathing lowpass; stereo (two detune sets).
export function pad(midis, dur, { attack = .35, release = .7, cutoff = 2200, vel = 1, seed = 1 } = {}) {
  const n = samples(dur + release + .05), L = new Float32Array(n), R = new Float32Array(n), r = new Rng(seed);
  for (const [side, out] of [[0, L], [1, R]]) {
    const oscs = [];
    for (const m of midis) for (const c of side ? [-6, 4, 11] : [-11, -3, 7]) oscs.push({ o: new Osc(r.next()), f: hz(m) * Math.pow(2, c / 1200) });
    const sv = new Svf(), lfo = r.next() * TAU;
    for (let i = 0; i < n; ++i) {
      const t = i / RATE;
      let v = 0;
      for (const s of oscs) v += s.o.saw(s.f);
      const env = adsr(t, attack, .5, .85, release, dur);
      out[i] = sv.process(v / Math.sqrt(oscs.length), cutoff * (1 + .15 * Math.sin(TAU * .3 * t + lfo)), .6) * env * vel * .5;
    }
  }
  eq(L, ['highpass', 160, .7]); eq(R, ['highpass', 160, .7]);
  return { L, R };
}

// --- the kit ---------------------------------------------------------------------------------------------------------
export function kick({ vel = 1, tone = 54, punch = 1, len = .42, seed = 1 } = {}) {
  const n = samples(len), y = new Float32Array(n), r = new Rng(seed), click = new OnePole(3500);
  let ph = 0;
  for (let i = 0; i < n; ++i) {
    const t = i / RATE, f = tone * (1 + 2.4 * punch * Math.exp(-t / .028));
    ph += f / RATE;
    const body = Math.sin(TAU * ph) * Math.exp(-t / .16) * smooth(t / .0012);
    y[i] = Math.tanh((body + click.lp(r.bi()) * Math.exp(-t / .0025) * .35 * punch) * 1.4) * vel;
  }
  return fadeEdges(y, 0, .03);
}
export function snare({ vel = 1, brush = false, seed = 1, len = .3 } = {}) {
  const n = samples(len), y = new Float32Array(n), r = new Rng(seed);
  const bp = new Biquad('bandpass', brush ? 4200 : 3300, .7), hp = new Biquad('highpass', brush ? 2000 : 1300, .7);
  for (let i = 0; i < n; ++i) {
    const t = i / RATE;
    const body = (Math.sin(TAU * 186 * t) + .5 * Math.sin(TAU * 332 * t)) * Math.exp(-t / .05) * (brush ? .25 : .6);
    const att = brush ? smooth(t / .006) : smooth(t / .001);
    const nz = hp.process(bp.process(r.bi())) * Math.exp(-t / (brush ? .12 : .09)) * 2.2;
    y[i] = (body + nz) * att * vel;
  }
  return fadeEdges(y, 0, .03);
}
// One clapper: a few fast bursts then a short room tail, band-limited round 1-1.5 kHz.
export function clap({ vel = 1, tone = 1250, seed = 1 } = {}) {
  const n = samples(.24), y = new Float32Array(n), r = new Rng(seed);
  const bp = new Biquad('bandpass', tone, 1.5), hp = new Biquad('highpass', 650, .7);
  const bursts = [0, .009 + .003 * r.next(), .018 + .004 * r.next(), .028 + .004 * r.next()];
  for (let i = 0; i < n; ++i) {
    const t = i / RATE;
    let e = 0;
    for (const b of bursts) if (t >= b) e = Math.max(e, Math.exp(-(t - b) / .0042));
    e = Math.max(e, t >= bursts[3] ? .45 * Math.exp(-(t - bursts[3]) / .075) : 0);
    y[i] = hp.process(bp.process(r.bi())) * e * vel * 3;
  }
  return fadeEdges(y, .0005, .02);
}
// A room full of friends clapping on the beat: several clappers, loose timing, spread across the stereo field.
export function groupClap({ people = 7, vel = 1, loose = .007, seed = 1 } = {}) {
  const r = new Rng(seed), n = samples(.32), L = new Float32Array(n), R = new Float32Array(n);
  for (let p = 0; p < people; ++p) {
    const c = clap({ vel: vel * (.6 + .5 * r.next()), tone: 950 + 750 * r.next(), seed: seed * 13 + p * 7 + 1 });
    const off = samples(Math.abs(r.gauss()) * loose + .002 * r.next()), pan = (p / Math.max(1, people - 1)) * 1.6 - .8;
    const gl = Math.cos((pan + 1) * Math.PI / 4) * Math.SQRT2, gr = Math.sin((pan + 1) * Math.PI / 4) * Math.SQRT2;
    for (let i = 0; i < c.length && i + off < n; ++i) { L[i + off] += c[i] * gl / Math.sqrt(people); R[i + off] += c[i] * gr / Math.sqrt(people); }
  }
  return { L, R };
}
export function shaker({ vel = 1, len = .12, seed = 1 } = {}) {
  const n = samples(len), y = new Float32Array(n), r = new Rng(seed);
  const bp = new Biquad('bandpass', 6800, .9), hp = new Biquad('highpass', 3800, .7);
  for (let i = 0; i < n; ++i) {
    const t = i / RATE, e = smooth(t / .014) * Math.exp(-Math.max(0, t - .014) / .035);
    y[i] = hp.process(bp.process(r.bi())) * e * vel * 1.4;
  }
  return fadeEdges(y, 0, .01);
}
export function tambourine({ vel = 1, seed = 1, len = .32 } = {}) {
  const n = samples(len), y = new Float32Array(n), r = new Rng(seed);
  const jingles = [6100, 7900, 9700, 11800].map(f => new Biquad('bandpass', f * (.97 + .06 * r.next()), 7));
  const hit = new Biquad('bandpass', 3200, 1);
  for (let i = 0; i < n; ++i) {
    const t = i / RATE, x = r.bi();
    let j = 0;
    for (const b of jingles) j += b.process(x);
    y[i] = (j * 1.6 * Math.exp(-t / .1) + hit.process(x) * Math.exp(-t / .006) * .6) * vel * smooth(t / .0015);
  }
  return fadeEdges(y, 0, .02);
}
export function hat({ vel = 1, open = false, seed = 1 } = {}) {
  const len = open ? .45 : .07, n = samples(len), y = new Float32Array(n);
  const freqs = [205.3, 304.4, 369.6, 522.7, 540, 800].map(f => f * 1.7), oscs = freqs.map((_, k) => new Osc(k * .13));
  const bp = new Biquad('bandpass', 9500, .8), hp = new Biquad('highpass', 7000, .7);
  for (let i = 0; i < n; ++i) {
    const t = i / RATE;
    let v = 0;
    for (let k = 0; k < 6; ++k) v += oscs[k].square(freqs[k]);
    y[i] = hp.process(bp.process(v / 6)) * Math.exp(-t / (open ? .16 : .022)) * vel * 1.5;
  }
  return fadeEdges(y, .0005, .01);
}
export function woodblock({ pitch = 1, vel = 1, seed = 1 } = {}) {
  return eq(modal(880 * pitch, .16, [{ r: 1, a: 1, t: .09 }, { r: 2.29, a: .35, t: .05 }, { r: 3.94, a: .12, t: .03 }],
    { strike: .0004, tick: .4, tickHz: 3000, seed, vel }), ['highpass', 300, .7]);
}
export function cowbell({ vel = 1 } = {}) {
  const n = samples(.4), y = new Float32Array(n), a = new Osc(), b = new Osc(.3), bp = new Biquad('bandpass', 1000, 1.6);
  for (let i = 0; i < n; ++i) {
    const t = i / RATE;
    y[i] = bp.process(a.square(562) + b.square(845)) * (Math.exp(-t / .045) * .7 + Math.exp(-t / .2) * .3) * vel;
  }
  return fadeEdges(y, .0005, .02);
}
export function triangleBell({ vel = 1, seed = 1 } = {}) {
  return modal(3150, 1.6, [{ r: 1, a: .6, t: 1.4 }, { r: 1.72, a: .5, t: 1.2 }, { r: 2.61, a: .35, t: .9 }, { r: 3.38, a: .25, t: .7 }],
    { strike: .0005, tick: .2, tickHz: 9000, seed, vel });
}
export function crash({ vel = 1, len = 2.2, seed = 1, bright = 1 } = {}) {
  const n = samples(len), L = new Float32Array(n), R = new Float32Array(n), r = new Rng(seed);
  const freqs = [347, 512, 693, 841, 1117, 1393].map(f => f * 2.1);
  const hpL = new Biquad('highpass', 3800, .7), hpR = new Biquad('highpass', 3600, .7), oscs = freqs.map((_, k) => new Osc(k * .21));
  const lp = new OnePole(12000 * bright);
  for (let i = 0; i < n; ++i) {
    const t = i / RATE;
    let m = 0;
    for (let k = 0; k < 6; ++k) m += oscs[k].square(freqs[k]);
    const env = Math.exp(-t / (.55 * len / 2.2)) * smooth(t / .002);
    const shimmer = lp.lp(m / 6 * .5);
    L[i] = hpL.process(r.bi() * .8 + shimmer) * env * vel * .6;
    R[i] = hpR.process(r.bi() * .8 + shimmer) * env * vel * .6;
  }
  return { L, R };
}
export function tom(midi, { vel = 1, seed = 1 } = {}) {
  const f0 = hz(midi), n = samples(.45), y = new Float32Array(n), r = new Rng(seed), lp = new OnePole(2000);
  let ph = 0;
  for (let i = 0; i < n; ++i) {
    const t = i / RATE;
    ph += f0 * (1 + .5 * Math.exp(-t / .03)) / RATE;
    y[i] = (Math.sin(TAU * ph) * Math.exp(-t / .2) + lp.lp(r.bi()) * Math.exp(-t / .02) * .3) * smooth(t / .001) * vel;
  }
  return fadeEdges(y, 0, .03);
}
export function snap({ vel = 1, seed = 1 } = {}) {
  const n = samples(.12), y = new Float32Array(n), r = new Rng(seed), bp = new Biquad('bandpass', 2400, 2.2);
  for (let i = 0; i < n; ++i) {
    const t = i / RATE;
    y[i] = bp.process(r.bi()) * Math.exp(-t / .012) * vel * 3 * smooth(t / .0004);
  }
  return y;
}
