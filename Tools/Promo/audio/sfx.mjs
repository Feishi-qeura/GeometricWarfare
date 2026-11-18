// Sound effects for the promo: a paper-and-pencil world, so the motion graphics sound like paper, pencils, tape and
// rubber stamps, and the game's fights sound like toys (pop-gun cannons, cartoon booms, paper creatures popping).
// Each generator returns a mono Float32Array or a stereo {L, R}; `seed` gives a variation of the same sound.
import { RATE, TAU, hz, Rng, Biquad, Svf, OnePole, Osc, smooth, fadeEdges, samples, clamp, db, eq, normalize, gainBuf } from './dsp.mjs';
import { modal, glock, woodblock, pluck } from './instruments.mjs';

const sec = n => n / RATE;
function buf(seconds) { return new Float32Array(samples(seconds)); }
function stereo(seconds) { return { L: buf(seconds), R: buf(seconds) }; }
// A mono layer written into a stereo pair with a pan that may move (pan(t) -1..1).
function place(st, y, at = 0, gain = 1, pan = () => 0) {
  const o = samples(at);
  for (let i = 0; i < y.length && o + i < st.L.length; ++i) {
    const th = (clamp(pan(sec(i)), -1, 1) + 1) * Math.PI / 4;
    st.L[o + i] += y[i] * gain * Math.cos(th) * Math.SQRT2;
    st.R[o + i] += y[i] * gain * Math.sin(th) * Math.SQRT2;
  }
  return st;
}
function add(dst, src, at = 0, g = 1) {
  const o = samples(at);
  for (let i = 0; i < src.length && o + i < dst.length; ++i) if (o + i >= 0) dst[o + i] += src[i] * g;
  return dst;
}
// A pitched sweep: frequency follows f(t) (Hz), amplitude env(t).
function sweep(seconds, f, env, shape = Math.sin) {
  const y = buf(seconds);
  let ph = 0;
  for (let i = 0; i < y.length; ++i) { const t = sec(i); ph += f(t) / RATE; y[i] = shape(TAU * ph) * env(t); }
  return y;
}
function noise(seconds, seed, env = () => 1, ...filters) {
  const y = buf(seconds), r = new Rng(seed), fs = filters.map(f => new Biquad(...f));
  for (let i = 0; i < y.length; ++i) { let v = r.bi(); for (const b of fs) v = b.process(v); y[i] = v * env(sec(i)); }
  return y;
}
const decay = tau => t => Math.exp(-t / tau);
const attackDecay = (a, tau) => t => smooth(t / a) * Math.exp(-Math.max(0, t - a) / tau);

// --- pencil ----------------------------------------------------------------------------------------------------------
// Graphite on paper: hiss shaped by each stroke's pressure and speed (faster is brighter), a fibrous stick-slip grain,
// a tiny tick where the point lands and a faint knock of the desk under the paper. `strokes` per second sets how busy
// the hand is (a Chinese heading is many short strokes; a long underline is one).
export function pencil(dur, { strokes = 10, pressure = 1, seed = 1, plan = null, bright = 1 } = {}) {
  const r = new Rng(seed);
  if (!plan) {
    plan = [];
    let t = .004;
    while (t < dur - .02) {
      const len = Math.min(r.range(.04, .13) * (r.next() < .18 ? 1.8 : 1), dur - t);
      plan.push({ t, len, p: r.range(.7, 1.1) * pressure, v: r.range(.75, 1.25) });
      t += len + r.range(.012, .06) * (10 / strokes);
    }
  }
  const n = samples(dur + .06), y = new Float32Array(n);
  const hp = new Biquad('highpass', 1100, .7), body = new Biquad('peak', 3400, 1.0, 5), air = new Biquad('peak', 8200, 1.4, 3);
  const sv = new Svf(), grit = new OnePole(320), grit2 = new OnePole(45);
  const env = new Float32Array(n), speed = new Float32Array(n);
  for (const s of plan) {
    const a = samples(s.t), b = Math.min(n, samples(s.t + s.len));
    for (let i = a; i < b; ++i) {
      const u = (i - a) / Math.max(1, b - a);
      const e = smooth(u / .12) * (1 - smooth((u - .78) / .22)) * s.p;
      if (e > env[i]) { env[i] = e; speed[i] = Math.sin(Math.PI * u) * s.v; }
    }
  }
  for (let i = 0; i < n; ++i) {
    if (env[i] < 1e-4 && (i === 0 || env[i - 1] < 1e-4)) { grit.lp(0); continue; }
    const x = r.bi();
    const rough = Math.max(0, 1 + 2.2 * grit.lp(r.bi()) + 1.2 * grit2.lp(r.bi()));
    let v = air.process(body.process(hp.process(x)));
    v = sv.process(v, (2600 + 5200 * speed[i]) * bright, .6);
    // Fibres: now and then the point catches and snaps forward.
    const catchy = r.next() < .004 * env[i] ? r.range(1.5, 3) : 1;
    y[i] = v * env[i] * (.5 + .5 * speed[i]) * rough * catchy * .55;
  }
  // The point touching down, and the desk under the paper.
  const tick = new Biquad('bandpass', 2600, 2), knock = new Biquad('lowpass', 420, .8);
  for (const s of plan) {
    const a = samples(s.t);
    for (let i = 0; i < samples(.012) && a + i < n; ++i) {
      const t = sec(i);
      y[a + i] += tick.process(r.bi()) * Math.exp(-t / .0018) * .5 * s.p + knock.process(r.bi()) * Math.exp(-t / .004) * .35 * s.p;
    }
  }
  return fadeEdges(eq(y, ['lowpass', 12000, .7]), .001, .01);
}
// One quick firm line (a checkbox side, a tick).
export const pencilLine = (len = .07, seed = 1, pressure = 1.1) => pencil(len + .02, { plan: [{ t: .002, len, p: pressure, v: 1.2 }], seed });
// A loop drawn round something: one long stroke whose speed swells twice.
export function pencilLoop(len = .6, seed = 1) {
  const plan = [{ t: .002, len: len * .55, p: 1, v: 1.25 }, { t: len * .52, len: len * .46, p: .95, v: 1.15 }];
  return pencil(len + .03, { plan, seed, strokes: 3 });
}
export function pencilTap(seed = 1) {
  const y = buf(.05), r = new Rng(seed), bp = new Biquad('bandpass', 2300, 2.5), lp = new Biquad('lowpass', 500, .8);
  for (let i = 0; i < y.length; ++i) { const t = sec(i); y[i] = (bp.process(r.bi()) * .8 + lp.process(r.bi()) * .5) * Math.exp(-t / .004); }
  return y;
}

// --- paper, tape, stamps, UI -----------------------------------------------------------------------------------------
// A sheet slid across the desk: soft friction that swells and fades.
export function paperSlide(len = .3, { seed = 1, bright = 1 } = {}) {
  const r = new Rng(seed), y = buf(len), sv = new Svf(), hp = new Biquad('highpass', 500, .7), grit = new OnePole(90);
  for (let i = 0; i < y.length; ++i) {
    const u = sec(i) / len, e = Math.pow(Math.sin(Math.PI * clamp(u, 0, 1)), 1.4);
    const v = sv.process(hp.process(r.bi()), (1400 + 2600 * e) * bright, .7);
    y[i] = v * e * (1 + 1.6 * Math.abs(grit.lp(r.bi()))) * .7;
  }
  return fadeEdges(y, .002, .02);
}
// Air moved by a fast sheet or a sweeping wipe: band-passed noise whose centre sweeps, panned along the motion.
export function whoosh(len = .45, { seed = 1, from = 350, peak = 2400, to = 800, panFrom = -.6, panTo = .6, q = 1.4, at = .6 } = {}) {
  const r = new Rng(seed), y = buf(len), sv = new Svf();
  for (let i = 0; i < y.length; ++i) {
    const u = sec(i) / len, e = u < at ? Math.pow(u / at, 2) : Math.pow(1 - (u - at) / (1 - at), 1.6);
    const f = u < at ? from + (peak - from) * smooth(u / at) : peak + (to - peak) * smooth((u - at) / (1 - at));
    sv.process(r.bi(), f, q);
    y[i] = (sv.bp * 1.3 + sv.lp * .4) * e;
  }
  return place(stereo(len), fadeEdges(y, .003, .02), 0, 1, t => panFrom + (panTo - panFrom) * smooth(t / len));
}
// A card or notepad landing on the desk: a breath of air, then a papery slap with a little low body.
export function paperDrop({ size = 1, seed = 1 } = {}) {
  const len = .32, y = buf(len), r = new Rng(seed);
  const pre = .028 * size;
  add(y, noise(pre, seed + 1, t => smooth(t / pre) * .25, ['bandpass', 1600, .8]), 0);
  const slap = noise(.12, seed + 2, attackDecay(.0015, .018 * size), ['bandpass', 1900 / Math.sqrt(size), .9], ['highpass', 500, .7]);
  add(y, slap, pre, 1.2);
  const thump = sweep(.16, t => (130 / size) * (1 + .6 * Math.exp(-t / .01)), attackDecay(.001, .028 * size));
  add(y, thump, pre, .55 * Math.min(1.3, size));
  add(y, noise(.2, seed + 3, t => Math.exp(-t / .05) * .12, ['bandpass', 4200, 1.2]), pre + .01);
  return fadeEdges(y, .001, .03);
}
// A rubber stamp slammed down: a short whoosh in, a fat low thud, the rubber's squish and a crisp tack of ink.
export function stamp({ size = 1, seed = 1 } = {}) {
  const len = .45, y = buf(len);
  add(y, noise(.05, seed, t => Math.pow(t / .05, 2) * .3, ['bandpass', 900, .9]), 0);
  const hitAt = .045;
  add(y, sweep(.3, t => 62 * size ** -.3 * (1 + 1.4 * Math.exp(-t / .012)), attackDecay(.0008, .07)), hitAt, 1.1);
  add(y, noise(.12, seed + 1, attackDecay(.0006, .016), ['bandpass', 1100, 1.1]), hitAt, 1.5);
  add(y, noise(.2, seed + 2, attackDecay(.004, .06), ['lowpass', 700, .8]), hitAt + .004, .5);
  add(y, noise(.03, seed + 3, decay(.002), ['highpass', 3500, .7]), hitAt, .5);
  return fadeEdges(eq(y, ['highpass', 35, .7]), .001, .04);
}
// Sticky tape pulled off the roll (fast stick-slip crackle) and pressed down.
export function tape({ seed = 1, len = .22 } = {}) {
  const y = buf(len + .1), r = new Rng(seed), bp = new Biquad('bandpass', 2400, 1.1);
  let ph = 0;
  for (let i = 0; i < samples(len); ++i) {
    const t = sec(i), u = t / len, f = 180 + 520 * u + 160 * r.next();
    ph += f / RATE;
    const slip = (ph % 1) < .35 ? 1 : .15;
    y[i] = bp.process(r.bi()) * slip * Math.pow(Math.sin(Math.PI * u), .6) * .9;
  }
  add(y, paperDrop({ size: .6, seed: seed + 9 }), len - .02, .7);
  return fadeEdges(y, .002, .02);
}
// The UI pop (buttons, chips, cards popping in): a rising bloop over a soft click.
export function pop({ pitch = 1, seed = 1, len = .12 } = {}) {
  const y = sweep(len, t => (520 + 900 * (1 - Math.exp(-t / .018))) * pitch, attackDecay(.001, .035));
  add(y, noise(.01, seed, decay(.0012), ['highpass', 2500, .7]), 0, .35);
  add(y, sweep(.05, t => 160 * pitch, attackDecay(.001, .012)), 0, .25);
  return fadeEdges(y, .0005, .02);
}
// A crisp button click (the build ring's slots).
export function click({ pitch = 1, seed = 1 } = {}) {
  const y = modal(1900 * pitch, .06, [{ r: 1, a: 1, t: .035 }, { r: 2.4, a: .4, t: .02 }], { strike: .0003, tick: .5, tickHz: 4500, seed });
  add(y, sweep(.04, () => 300 * pitch, attackDecay(.0008, .008)), 0, .35);
  return y;
}
// The build ring unfolding: a quick paper flick and a little rising chime.
export function ringOpen({ seed = 1 } = {}) {
  const y = buf(.5);
  add(y, paperSlide(.14, { seed, bright: 1.3 }), 0, .5);
  [84, 88, 91].forEach((m, k) => add(y, glock(m, { len: .45, vel: .5, seed: seed + k }), .015 + k * .035, .3));
  return y;
}
// A party popper: the pop, then paper confetti flickering down across the stereo field.
export function confetti({ seed = 1, len = 1.1, amount = 1 } = {}) {
  const st = stereo(len), r = new Rng(seed);
  const popY = noise(.06, seed, decay(.0022), ['highpass', 900, .7]);
  add(popY, sweep(.06, t => 150 * (1 + Math.exp(-t / .006)), attackDecay(.0005, .014)), 0, .9);
  place(st, popY, 0, 1.1);
  const flicks = Math.round(70 * amount);
  for (let k = 0; k < flicks; ++k) {
    const t = .02 + Math.pow(r.next(), 1.6) * (len - .1), f = r.range(2500, 8000), pan = r.range(-.9, .9);
    const fl = noise(.006, seed * 7 + k, decay(.0011), ['bandpass', f, 3]);
    place(st, fl, t, r.range(.15, .45) * (1 - .6 * t / len), () => pan);
  }
  const rustle = noise(len, seed + 3, t => .09 * Math.exp(-t / .35) * smooth(t / .02), ['highpass', 3000, .7]);
  place(st, rustle, 0, 1, () => 0);
  return st;
}
// The page wipe: triangle tiles flipping one after another across the screen (left to right), ticking up in pitch
// as they close; opening plays them back softer and falling.
export function tiles({ count = 15, len = .55, open = false, seed = 1 } = {}) {
  const st = stereo(len + .15), r = new Rng(seed);
  for (let k = 0; k < count; ++k) {
    const u = k / (count - 1), t = u * (len - .08) + r.range(0, .012);
    const f = open ? 2600 - 900 * u : 1500 + 1200 * u;
    const flap = noise(.05, seed * 11 + k, attackDecay(.0015, .009), ['bandpass', f, 1.6]);
    add(flap, sweep(.03, () => (open ? 260 - 60 * u : 210 + 90 * u), attackDecay(.0008, .006)), 0, .5);
    place(st, flap, t, (open ? .5 : .75) * r.range(.75, 1.05), () => -.85 + 1.7 * u);
  }
  return st;
}

// --- weapons and fights (toys, not guns) -----------------------------------------------------------------------------
// The bolt tower: a toy cannon's "pomp" (a cork-gun pop with a round low body and a puff of air).
export function cannon({ seed = 1, size = 1, pitch = 1 } = {}) {
  const r = new Rng(seed), p = pitch * (.94 + .12 * r.next()), len = .4, y = buf(len);
  add(y, sweep(.25, t => 92 * p / size * (1 + 1.2 * Math.exp(-t / .016)), attackDecay(.0006, .055 * size)), 0, 1);
  add(y, noise(.06, seed, attackDecay(.0004, .012), ['bandpass', 1300 * p, 1.2]), 0, .9);
  add(y, sweep(.03, t => 380 * p * (1 + 1.4 * t / .03), attackDecay(.0005, .007)), 0, .28);
  add(y, noise(.3, seed + 1, attackDecay(.004, .07 * size), ['lowpass', 900, .7]), .004, .35);
  return fadeEdges(eq(y, ['highpass', 40, .7]), .0005, .03);
}
// A mortar shell leaving its tube: a deep resonant "thoomp".
export function mortarLaunch({ seed = 1 } = {}) {
  const y = buf(.5);
  add(y, sweep(.35, t => 70 * (1 + .9 * Math.exp(-t / .03)), attackDecay(.003, .09)), 0, 1.1);
  add(y, noise(.3, seed, attackDecay(.006, .05), ['bandpass', 260, 3.5]), 0, 2.2);
  add(y, noise(.25, seed + 1, attackDecay(.01, .06), ['bandpass', 1100, .8]), .01, .3);
  return fadeEdges(eq(y, ['highpass', 35, .7]), .001, .03);
}
// A lobbed shell's cartoon falling whistle, ending as it lands.
export function fallWhistle(len = .8, { seed = 1, from = 1900, to = 950 } = {}) {
  const r = new Rng(seed), ph0 = r.next();
  const y = sweep(len, t => (from + (to - from) * Math.pow(t / len, 1.3)) * (1 + .006 * Math.sin(TAU * 7 * t)),
    t => smooth(t / (len * .35)) * (1 - smooth((t - len + .03) / .03)) * .45);
  return fadeEdges(y, .01, .02);
}
// A cartoon boom: a crack, a body of noise whose lowpass closes, a sub drop and paper debris pattering down.
export function boom({ size = 1, seed = 1 } = {}) {
  const len = .9 * Math.sqrt(size) + .2, st = stereo(len), r = new Rng(seed), y = buf(len);
  add(y, noise(.03, seed, decay(.004), ['highpass', 1200, .7]), 0, .7);
  const sv = new Svf(), body = buf(len);
  for (let i = 0; i < body.length; ++i) {
    const t = sec(i);
    body[i] = sv.process(r.bi(), 180 + 3000 * Math.exp(-t / (.07 * size)), .7) * attackDecay(.002, .2 * size)(t) * 1.6;
  }
  add(y, body, 0, 1);
  add(y, sweep(.6 * size, t => (58 / Math.sqrt(size)) * (1 + 1.1 * Math.exp(-t / .03)), attackDecay(.002, .16 * size)), 0, 1.0);
  place(st, y, 0, 1);
  for (let k = 0; k < Math.round(24 * size); ++k) {
    const t = .05 + Math.pow(r.next(), 1.5) * (len - .2), pan = r.range(-.8, .8);
    place(st, noise(.012, seed * 5 + k, decay(.0025), ['bandpass', r.range(1500, 5000), 2.5]), t, r.range(.1, .3) * (1 - t / len), () => pan);
  }
  return st;
}
// The tesla coil: a buzzing arc with crackle and a bright zing.
export function zap({ seed = 1, len = .2, bright = 1 } = {}) {
  const r = new Rng(seed), y = buf(len + .05), o = new Osc(r.next()), bp = new Biquad('bandpass', 2200 * bright, .9);
  let gate = 1, f = 95;
  for (let i = 0; i < samples(len); ++i) {
    const t = sec(i);
    if (i % 480 === 0) { gate = r.next() < .75 ? 1 : .25; f = 85 + 40 * r.next(); }
    const buzz = Math.tanh(o.saw(f) * 3) * gate;
    const crackle = r.next() < .03 ? r.bi() * 3 : 0;
    y[i] = (bp.process(buzz + crackle) * .9) * attackDecay(.002, len * .45)(t);
  }
  add(y, sweep(.08, t => 2600 * bright * (1 - .45 * t / .08), attackDecay(.0005, .02)), 0, .25);
  return fadeEdges(eq(y, ['highpass', 400, .7]), .0005, .02);
}
// Toy blasters for the players' guns: soft, short and pitched so a crowd of them reads as play, not war.
export function blaster(kind, { seed = 1 } = {}) {
  const r = new Rng(seed), p = .93 + .14 * r.next();
  switch (kind) {
    case 'pistol': {
      const y = sweep(.09, t => 1500 * p * Math.exp(-t / .03) + 260, attackDecay(.0008, .025), x => Math.sin(x) + .2 * Math.sin(2 * x));
      return add(y, noise(.015, seed, decay(.002), ['bandpass', 3000, 1]), 0, .3);
    }
    case 'rifle': {
      const y = noise(.07, seed, attackDecay(.0005, .012), ['bandpass', 1700 * p, 1.1]);
      return add(y, sweep(.08, t => 140 * p * (1 + Math.exp(-t / .01)), attackDecay(.0005, .02)), 0, .7);
    }
    case 'shotgun': {
      const y = noise(.16, seed, attackDecay(.0008, .035), ['lowpass', 2400, .7]);
      return add(y, sweep(.15, t => 95 * p * (1 + Math.exp(-t / .015)), attackDecay(.0008, .04)), 0, .9);
    }
    case 'sniper': {
      const y = add(buf(.35), noise(.03, seed, decay(.004), ['highpass', 1800, .7]));
      add(y, sweep(.3, t => 3200 * p * Math.exp(-t / .12) + 700, attackDecay(.001, .08)), .005, .35);
      return add(y, sweep(.12, t => 110 * (1 + Math.exp(-t / .01)), attackDecay(.0008, .03)), 0, .8);
    }
    case 'mg': {
      const y = noise(.04, seed, attackDecay(.0004, .008), ['bandpass', 2100 * p, 1.3]);
      return add(y, sweep(.04, () => 170 * p, attackDecay(.0004, .01)), 0, .5);
    }
    default: return blaster('pistol', { seed });
  }
}
// A bolt landing on a paper creature: a tiny papery tup.
export function hit({ seed = 1 } = {}) {
  const r = new Rng(seed), y = noise(.05, seed, attackDecay(.0006, .008), ['bandpass', r.range(1100, 1700), 1.4]);
  return add(y, sweep(.04, () => 230, attackDecay(.0005, .007)), 0, .4);
}
// A paper creature popping: a balloon-ish snap, a bloop falling as it crumples and a short crumple of paper.
export function creaturePop({ size = 1, seed = 1 } = {}) {
  const r = new Rng(seed), p = (.9 + .2 * r.next()) / Math.sqrt(size), len = .28 + .15 * size, y = buf(len);
  add(y, noise(.012, seed, decay(.0016), ['highpass', 1500, .7]), 0, .55);
  add(y, sweep(.16, t => 720 * p * Math.exp(-t / .07) + 180 * p, attackDecay(.001, .04 * Math.sqrt(size))), .002, .7);
  for (let k = 0; k < 6 + 6 * size; ++k)
    add(y, noise(.01, seed * 3 + k, decay(.002), ['bandpass', r.range(1800, 4800), 2.2]), .01 + r.next() * .12 * size, r.range(.08, .22));
  return fadeEdges(y, .0005, .03);
}
// A spent shot or a block dropping onto the floor: a dull little tap.
export function tap({ seed = 1, pitch = 1 } = {}) {
  const y = noise(.04, seed, attackDecay(.0008, .006), ['bandpass', 900 * pitch, 1.5]);
  return add(y, sweep(.03, () => 180 * pitch, attackDecay(.0005, .006)), 0, .5);
}
// The boss stamping: a huge thud with a rumble.
export function stomp({ seed = 1 } = {}) {
  const st = boom({ size: 2.2, seed });
  const sub = sweep(1.0, t => 38 * (1 + .8 * Math.exp(-t / .05)), attackDecay(.002, .3));
  place(st, sub, 0, 1.1);
  return st;
}
// The boss's laser: a charge whine, then a fat buzzing beam that sizzles, then it lets go.
export function laser({ len = 1.0, seed = 1, charge = .35 } = {}) {
  const total = charge + len + .2, st = stereo(total), r = new Rng(seed);
  const ch = sweep(charge, t => 300 + 1100 * Math.pow(t / charge, 1.5), t => smooth(t / charge) * (.6 + .4 * Math.sin(TAU * 32 * t)) * .35);
  place(st, ch, 0, 1);
  const beam = buf(len + .15), oscs = [110, 110.7, 165.3, 220.9].map(f => ({ o: new Osc(r.next()), f })), sv = new Svf(), sizzle = new Biquad('highpass', 4000, .7);
  for (let i = 0; i < beam.length; ++i) {
    const t = sec(i), env = smooth(t / .02) * (1 - smooth((t - len) / .15));
    let v = 0;
    for (const s of oscs) v += s.o.saw(s.f * (1 + .01 * Math.sin(TAU * 6 * t)));
    v = sv.process(v * .3, 900 + 700 * Math.sin(TAU * 3 * t) ** 2, 2.2);
    beam[i] = (Math.tanh(v * 2) * .6 + sizzle.process(r.bi()) * .25 * (r.next() < .5 ? 1 : .3)) * env;
  }
  place(st, beam, charge, .9, t => .5 * Math.sin(TAU * .8 * t));
  return st;
}
// A wall block cracking: a cardboard crunch.
export function crunch({ seed = 1, size = 1 } = {}) {
  const r = new Rng(seed), y = buf(.35 * size + .1);
  for (let k = 0; k < 7; ++k)
    add(y, noise(.05, seed * 9 + k, attackDecay(.001, .012), ['bandpass', r.range(600, 1900), 1.3]), r.next() * .14 * size, r.range(.5, 1));
  add(y, sweep(.2, t => 90 * (1 + Math.exp(-t / .01)), attackDecay(.001, .05)), 0, .8);
  return fadeEdges(y, .0005, .03);
}
// Drawing a wall: one firm pencil stroke and the block knocking into place.
export function wallDraw({ seed = 1 } = {}) {
  const y = buf(.35);
  add(y, pencil(.16, { plan: [{ t: .002, len: .13, p: 1.15, v: 1.3 }], seed }), 0, 1.1);
  add(y, woodblock({ pitch: .55 + .1 * new Rng(seed).next(), vel: .7, seed }), .12, .45);
  return y;
}
// A tower popping up like jelly: blocks stacking (three knocks, rising) and a wobbling boing.
export function towerBuild({ seed = 1, kind = 0 } = {}) {
  const y = buf(.9), base = [1, .84, 1.12][kind % 3];
  [0, .055, .11].forEach((t, k) => add(y, woodblock({ pitch: base * (.6 + .12 * k), vel: .8, seed: seed + k }), t, .7));
  const boing = sweep(.6, t => 230 * base * (1 + .28 * Math.sin(TAU * 11 * t) * Math.exp(-t / .16)) * (1 + .5 * Math.exp(-t / .03)),
    attackDecay(.004, .18), x => Math.sin(x) + .15 * Math.sin(2 * x));
  add(y, boing, .15, .55);
  add(y, glock(96 + kind * 2, { len: .5, vel: .5, seed }), .2, .18);
  return fadeEdges(y, .0005, .03);
}
// A tower growing a side: a rising sparkle.
export function sparkleUp({ seed = 1, notes = [84, 88, 91, 96] } = {}) {
  const y = buf(.9);
  notes.forEach((m, k) => add(y, glock(m, { len: .6, vel: .7, seed: seed + k }), k * .045, .35));
  return y;
}
// The core takes a hit: a cartoon bonk.
export function bonk({ seed = 1 } = {}) {
  const y = add(buf(.25), woodblock({ pitch: .38, vel: 1, seed }));
  add(y, sweep(.2, t => 160 * (1 - .4 * t / .2), attackDecay(.001, .05)), 0, .5);
  return y;
}
// An enemy appearing at a gate: a soft paper puff.
export function puff({ seed = 1 } = {}) {
  return noise(.14, seed, attackDecay(.01, .04), ['bandpass', 1300, .8], ['lowpass', 2500, .7]);
}
// Ink picked up: a drop of ink, "bloop".
export function bloop({ seed = 1, pitch = 1 } = {}) {
  return fadeEdges(sweep(.12, t => (380 + 900 * Math.pow(t / .12, .6)) * pitch, attackDecay(.003, .03)), .001, .02);
}
// A friendly bullet nudging a teammate: a soft rubbery boop.
export function boop({ seed = 1 } = {}) {
  const r = new Rng(seed);
  return fadeEdges(sweep(.08, t => (300 + 120 * r.next()) * (1 + .5 * Math.exp(-t / .015)), attackDecay(.002, .02)), .001, .02);
}
// A teammate grabbed: "yoink".
export function yoink({ seed = 1 } = {}) {
  const y = sweep(.16, t => 320 * Math.pow(4, smooth(t / .12)), t => smooth(t / .01) * (1 - smooth((t - .1) / .06)) * .6);
  add(y, paperSlide(.12, { seed, bright: 1.4 }), 0, .4);
  return fadeEdges(y, .001, .02);
}
// The throw: a slide whistle going up as the friend flies, with the air rushing past.
export function slideWhistle(len = .55, { seed = 1, from = 560, to = 1900, down = false } = {}) {
  const a = down ? to : from, b = down ? from : to;
  const y = sweep(len, t => a * Math.pow(b / a, smooth(t / len)) * (1 + .012 * Math.sin(TAU * 6.5 * t)),
    t => smooth(t / .03) * (1 - smooth((t - len + .06) / .06)) * .5, x => Math.sin(x) + .06 * Math.sin(2 * x));
  const breath = noise(len, seed, t => smooth(t / .03) * (1 - smooth((t - len + .06) / .06)) * .04, ['bandpass', 1800, 1.2]);
  return fadeEdges(add(y, breath), .002, .02);
}
// The thrown friend smacking onto the paper: a fat thud and a spring's boing.
export function landing({ seed = 1 } = {}) {
  const st = boom({ size: 1.2, seed });
  const spring = sweep(.7, t => 175 * (1 + .35 * Math.sin(TAU * 13 * t) * Math.exp(-t / .2)), attackDecay(.003, .2), x => Math.sin(x) + .1 * Math.sin(3 * x));
  place(st, spring, .03, .55);
  return st;
}
// A deep "whomp" as the bait's ring pulls everything in.
export function whomp({ seed = 1 } = {}) {
  return fadeEdges(sweep(.5, t => 70 + 50 * Math.sin(Math.PI * clamp(t / .5, 0, 1)), t => Math.sin(Math.PI * clamp(t / .5, 0, 1)) * .8), .002, .03);
}
// The wave starts: a sports-day whistle, two short blasts (a pea rattling in the whistle).
export function refWhistle({ blasts = [[0, .16], [.22, .34]], seed = 1 } = {}) {
  const len = Math.max(...blasts.map(b => b[0] + b[1])) + .05, y = buf(len), r = new Rng(seed);
  for (const [at, d] of blasts) {
    const tone = sweep(d, t => 2750 * (1 + .03 * Math.sin(TAU * 34 * t + r.next())), t => smooth(t / .012) * (1 - smooth((t - d + .02) / .02)) * .5);
    add(y, tone, at);
    add(y, noise(d, seed + at * 100, t => smooth(t / .012) * (1 - smooth((t - d + .02) / .02)) * .1, ['bandpass', 2900, 2]), at);
  }
  return y;
}
// A magic shimmer (respawns, level-ups): a quick glockenspiel run.
export function shimmer({ seed = 1, notes = [96, 100, 103, 108] } = {}) {
  const y = buf(.9);
  notes.forEach((m, k) => add(y, glock(m, { len: .5, vel: .45, seed: seed + k }), k * .03, .3));
  return y;
}
// A party blower: a buzzy reed that unrolls with a flutter.
export function partyHorn({ len = .45, seed = 1, pitch = 1 } = {}) {
  const o = new Osc(), bp = new Biquad('bandpass', 1300, 1), y = buf(len), r = new Rng(seed);
  for (let i = 0; i < y.length; ++i) {
    const t = sec(i), f = 470 * pitch * (1 + .04 * Math.sin(TAU * 23 * t)) * (1 + .06 * smooth(t / .1));
    y[i] = bp.process(Math.tanh(o.saw(f) * 2)) * smooth(t / .02) * (1 - smooth((t - len + .05) / .05)) * (.8 + .2 * r.next()) * .7;
  }
  return y;
}
// A noise riser into a section: rising bandpass noise.
export function riser(len = 1.5, { seed = 1, from = 300, to = 6000 } = {}) {
  const r = new Rng(seed), y = buf(len), sv = new Svf();
  for (let i = 0; i < y.length; ++i) {
    const u = sec(i) / len;
    y[i] = sv.process(r.bi(), from * Math.pow(to / from, u), 1.2) * Math.pow(u, 2.2) * (1 - smooth((u - .97) / .03)) * .7;
  }
  return y;
}
