// Sound effects placed on the cut: the motion graphics (paper, pencil, tape, stamps, wipes) from web/timeline.js, the
// HUD moments seen in the plates (ring clicks, result stamps and stars) and every gameplay event the capture wrote
// into Plates/<shot>/events.csv (shots, shells, blasts, sparks, deaths, builds, the throw), each panned to where it
// is on screen and thinned out where the fight gets dense, so a crowd of shots reads as play rather than noise.
import fs from 'node:fs';
import path from 'node:path';
import * as TL from '../web/timeline.js';
import * as X from './sfx.mjs';
import * as I from './instruments.mjs';
import { Bus, Rng, clamp } from './dsp.mjs';

const W = 1920, H = 1080;
const inOutCubic = t => { t = clamp(t, 0, 1); return t < .5 ? 4 * t * t * t : 1 - Math.pow(-2 * t + 2, 3) / 2; };
// The pop's sweep ends near 1420 Hz at pitch 1: this tunes it to a note.
const popTo = midi => 440 * Math.pow(2, (midi - 69) / 12) / 1420;

// How much of the screen the page wipes cover at t (sounds under the tiles are held back).
function cover(t) {
  let c = 0;
  for (const w of TL.Wipes) {
    const close = w.at - TL.WipeClose, until = w.open ?? TL.Clips[0].at;
    if (t >= close && t < w.at) c = Math.max(c, (t - close) / TL.WipeClose);
    else if (t >= w.at && t < until) c = 1;
    else if (w.open != null && t >= w.open && t < w.open + TL.WipeOpen) c = Math.max(c, 1 - (t - w.open) / TL.WipeOpen);
  }
  // td_open starts under the game's own wipe: its card until plate frame 38, its tiles gone by frame 56.
  if (t >= 6.45 && t < 7.25) c = 1;
  else if (t >= 7.25 && t < 7.72) c = Math.max(c, 1 - (t - 7.25) / .47);
  return clamp(c, 0, 1);
}

function readEvents(dir, shot) {
  const file = path.join(dir, shot, 'events.csv');
  if (!fs.existsSync(file)) return [];
  return fs.readFileSync(file, 'utf8').trim().split(/\r?\n/).slice(1).map(line => {
    const [frame, what, a, b, x, y, x2, y2, size] = line.split(',');
    return { frame: +frame, what: what.toLowerCase(), a: +a, b: +b, x: +x, y: +y, x2: +x2, y2: +y2, size: +size };
  });
}
// Where a plate point lands in the promo frame (0..1), following the clip's push-in (promo.js footage()).
function onScreen(c, t, x, y) {
  const k = clamp((t - c.at) / c.len, 0, 1), z = c.zoom[0] + (c.zoom[1] - c.zoom[0]) * inOutCubic(k);
  const f = c.focus || [W / 2, H / 2];
  const ox = Math.min(0, Math.max(W - W * z, W / 2 - f[0] * z)), oy = Math.min(0, Math.max(H - H * z, H / 2 - f[1] * z));
  return [(ox + x * W * z) / W, (oy + y * H * z) / H];
}

export function renderFoley(length, platesDir) {
  const dry = new Bus(length), wet = new Bus(length), r = new Rng(4242);
  let seed = 1;
  const put = (y, at, gain = 1, pan = 0, send = .1) => {
    if (!y || gain <= .001 || at >= length) return;
    dry.add(y, at, gain, clamp(pan, -1, 1));
    if (send > 0) wet.add(y, at, gain * send, clamp(pan, -1, 1));
  };
  // A stamp's buffer hits .045 s in; a paper drop's slap lands a breath in.
  const stampAt = (impact, gain, pan, size = 1) => put(X.stamp({ size, seed: ++seed }), impact - .045, gain, pan, .15);
  const dropAt = (land, gain, pan, size = 1) => put(X.paperDrop({ size, seed: ++seed }), land - .03 * size, gain, pan, .1);
  const marker = (at, len, gain, pan) => put(X.pencil(len, { strokes: 2.5, pressure: .8, bright: .55, seed: ++seed }), at, gain, pan, .05);

  // --- the intro ---------------------------------------------------------------------------------------------------
  // The shapes bounce in on C, E and G (the marimba plays them an octave down), the logo is slammed down, poppers.
  [[.35, 84], [.55, 88], [.75, 91]].forEach(([t, m], k) => put(X.pop({ pitch: popTo(m), seed: ++seed }), t, .3, (k - 1) * .45, .15));
  put(X.whoosh(.45, { seed: ++seed, from: 300, peak: 2300, to: 700, panFrom: 0, panTo: 0 }), 1.08, .32, 0, .15);
  stampAt(1.5, .75, 0, 1.6);
  put(X.confetti({ seed: ++seed }), 1.5, .5, -.65, .2);
  put(X.confetti({ seed: ++seed }), 1.53, .5, .65, .2);
  put(X.tape({ seed: ++seed }), 2.0, .38, 0, .1);
  for (let i = 0; i < 6; ++i) put(X.tap({ seed: ++seed, pitch: 1.5 + .1 * i }), 2.08 + .2 + i * .045, .09, -.35 + .14 * i, .05);
  put(X.pop({ pitch: popTo(96), seed: ++seed }), 2.52, .14, 0, .1);
  [[3.0, 88], [3.25, 91], [3.5, 96]].forEach(([t, m], k) => {
    put(X.pop({ pitch: popTo(m), seed: ++seed }), t + .06, .26, (k - 1) * .55, .12);
    put(X.click({ pitch: 1.2, seed: ++seed }), t + .04, .12, (k - 1) * .55, .05);
  });

  // --- page wipes and their section cards ----------------------------------------------------------------------------
  for (const w of TL.Wipes) {
    put(X.tiles({ seed: ++seed }), w.at - TL.WipeClose, .42, 0, .15);
    if (w.card !== 'outro') {
      const popT = w.at - TL.CardLead;
      put(X.pop({ pitch: popTo(84), seed: ++seed }), popT + .03, .3, 0, .15);
      put(X.paperSlide(.2, { seed: ++seed }), popT, .15, 0, .05);
      stampAt(popT + .62, .6, .35, 1.1);
    }
    if (w.open != null) put(X.tiles({ open: true, seed: ++seed }), w.open, .32, 0, .15);
  }
  // The defense card hands over to the game's own: it shrinks away, the game's card shrinks, its tiles open.
  put(X.pop({ pitch: popTo(79), seed: ++seed }), 6.3, .16, 0, .1);
  put(X.pop({ pitch: popTo(76), seed: ++seed }), 7.1, .18, 0, .1);
  put(X.tiles({ open: true, seed: ++seed, len: .47 }), 7.25, .3, 0, .15);

  // --- captions and badges --------------------------------------------------------------------------------------------
  for (const c of TL.Captions) {
    put(X.paperSlide(.28, { seed: ++seed }), c.at - .02, .3, .55, .08);
    marker(c.at + .2, .38, .2, .45);
    put(X.paperSlide(.18, { seed: ++seed, bright: 1.3 }), c.at + c.len, .14, .55, .05);
  }
  for (const b of TL.Badges) {
    put(X.paperSlide(.26, { seed: ++seed }), b.from - .02, .22, -.7, .08);
    put(X.paperSlide(.16, { seed: ++seed, bright: 1.3 }), b.to, .1, -.7, .05);
  }

  // --- HUD moments in the plates ---------------------------------------------------------------------------------------
  // td_build (1.6x): the build ring opens on a cell and a slot is taken (the tower itself is in the sound sheet).
  const build = TL.Clips.find(c => c.shot === 'td_build');
  const atBuild = f => build.at + (f - build.from) / (TL.FPS * build.speed);
  [[50, 68, .46], [84, 102, .86], [118, 136, .31], [154, 172, .46]].forEach(([open, take, x]) => {
    const pan = (x - .5) * 1.4;
    put(X.ringOpen({ seed: ++seed }), atBuild(open), .34, pan, .12);
    put(X.click({ pitch: 1.1, seed: ++seed }), atBuild(open) + .25, .12, pan, .05);
    put(X.click({ pitch: .9, seed: ++seed }), atBuild(take), .32, pan, .08);
  });
  // td_victory: the 胜利 stamp lands at plate frame 15 with confetti; two stars pop (under the closing wipe).
  const vic = TL.Clips.find(c => c.shot === 'td_victory'), atVic = f => vic.at + (f - vic.from) / TL.FPS;
  stampAt(atVic(15), .62, 0, 1.3);
  put(X.confetti({ seed: ++seed, amount: .7 }), atVic(15.5), .35, 0, .2);
  [[25, 84], [31, 88]].forEach(([f, m]) => put(X.pop({ pitch: popTo(m), seed: ++seed }), atVic(f), .22 * (1 - .7 * cover(atVic(f))), -.2, .12));
  // coop_results: the card drops in, 胜利 is stamped, three stars, the buttons and the titles of the room.
  const res = TL.Clips.find(c => c.shot === 'coop_results'), atRes = f => res.at + (f - res.from) / TL.FPS;
  dropAt(atRes(4), .45, 0, 1.5);
  stampAt(atRes(15), .62, 0, 1.3);
  put(X.confetti({ seed: ++seed, amount: .8 }), atRes(15.5), .38, 0, .2);
  [[24, 84], [30, 88], [39, 91]].forEach(([f, m], k) => put(X.pop({ pitch: popTo(m), seed: ++seed }), atRes(f), .24, -.35 + .1 * k, .12));
  [-.3, 0, .3].forEach((p, k) => put(X.click({ pitch: 1 + .1 * k, seed: ++seed }), atRes(34) + k * .03, .1, p, .05));
  [-.4, .4].forEach((p, k) => put(X.pop({ pitch: popTo(96 + 4 * k), seed: ++seed }), atRes(43) + k * .05, .2, p, .1));

  // --- the systems ---------------------------------------------------------------------------------------------------
  put(X.paperSlide(.3, { seed: ++seed }), TL.Systems[0].at + .2, .22, -.75, .08);
  TL.Systems.forEach((s, i) => {
    dropAt(s.at + .2, .42, -.35, 1.2);
    dropAt(s.at + .32, .36, .5, 1.0);
    if (s.inset) dropAt(s.at + .75, .26, .1, .7);
    marker(s.at + .3, .4, .22, .45);
    for (let k = 0; k < s.lines.length; ++k)
      put(X.pencil(.45, { strokes: 11, pressure: .95, seed: ++seed }), s.at + .55 + k * .22, .34, .5, .05);
    stampAt(s.at + 1.3, .5, .55, 1.0);
    // Shoved off the desk to the left as the next card drops on top.
    const exit = s.at + TL.SystemLen - TL.SystemExit;
    put(X.whoosh(TL.SystemExit + TL.SystemOverlap, { seed: ++seed, from: 250, peak: 1600, to: 500, panFrom: .3, panTo: -.9, at: .7 }), exit, .3, 0, .1);
    put(X.paperSlide(.4, { seed: ++seed }), exit + .1, .18, -.4, .05);
  });
  put(X.paperSlide(.22, { seed: ++seed, bright: 1.2 }), TL.ListStart - .26, .16, -.75, .05);

  // --- the shopping list -----------------------------------------------------------------------------------------------
  {
    const T = TL.ListTitle;
    dropAt(TL.ListStart + .26, .55, .1, 1.7);
    [[0, 84], [1, 88], [2, 91]].forEach(([i, m]) => put(X.pop({ pitch: popTo(m), seed: ++seed }), TL.ListStart + .4 + i * .12 + .18, .18, -.75, .1));
    put(X.whoosh(.4, { seed: ++seed, from: 400, peak: 1500, to: 700, panFrom: .8, panTo: 0 }), T.penIn, .1, 0, .05);
    put(X.pencil(T.done - T.write, { strokes: 15, pressure: 1.15, seed: ++seed }), T.write, .55, -.1, .05);
    marker(T.done + .05, .4, .24, -.05);
    put(X.pop({ pitch: popTo(91), seed: ++seed }), T.done + .32, .22, .75, .1);
    dropAt(T.done + .36, .2, .75, .6);
    TL.ListItems.forEach((it, i) => {
      const a = TL.listItemAt(i);
      for (let k = 0; k < 4; ++k) put(X.pencilLine(.06, ++seed, 1.05), a + k * .07, .38, -.45, .04);
      put(X.pencil(.55, { strokes: 12, pressure: 1.1, seed: ++seed }), a + .2, .5, -.1, .05);
      put(X.pencil(.35, { strokes: 10, pressure: .7, bright: .9, seed: ++seed }), a + .62, .28, 0, .05);
      put(X.pop({ pitch: popTo(84 + [0, 2, 4, 5, 7, 9, 11, 12][i]), seed: ++seed }), a + .8, .2, .55, .1);
    });
    dropAt(72.62, .45, -.65, 1.2);
    put(X.pencilLoop(.6, ++seed), 72.55, .5, -.1, .05);
    put(X.whoosh(.5, { seed: ++seed, from: 400, peak: 1300, to: 600, panFrom: 0, panTo: .9 }), 73.2, .09, 0, .05);
  }

  // --- the outro --------------------------------------------------------------------------------------------------------
  stampAt(76.3, .8, 0, 1.7);
  put(X.confetti({ seed: ++seed }), 76.25, .5, -.65, .2);
  put(X.confetti({ seed: ++seed }), 76.28, .5, .65, .2);
  put(X.partyHorn({ seed: ++seed }), 76.4, .16, -.3, .15);
  put(X.partyHorn({ seed: ++seed, pitch: 1.19 }), 76.48, .14, .35, .15);
  put(X.tape({ seed: ++seed }), 76.9, .36, 0, .1);

  // --- gameplay from the sound sheets ------------------------------------------------------------------------------
  const last = {};
  const gate = (key, t, gap) => { if (last[key] != null && t - last[key] < gap) return false; last[key] = t; return true; };
  const guns = ['pistol', 'rifle', 'shotgun', 'sniper', 'mg'];
  for (const c of TL.Clips) {
    const events = readEvents(platesDir, c.shot);
    // A note lands on the plate whose frame spawned the effect; its particles show a frame or so later, and sound a
    // touch late reads better than early, so the gameplay sounds trail by 15 ms.
    const timeOf = f => c.at + (f - c.from) / (TL.FPS * c.speed) + .015;
    const inClip = t => t >= c.at && t < c.at + c.len - .02;
    // Shells in flight: a launch pairs with the next landing of the same kind.
    const landings = { 10: events.filter(e => e.what === 'gone' && e.a === 10), 11: events.filter(e => e.what === 'gone' && e.a === 11) };
    let telegraph = null;
    if (c.shot === 'td_wave') put(X.refWhistle({ seed: ++seed }), c.at + .08, .22, .2, .15);
    for (const e of events) {
      const t = timeOf(e.frame);
      if (e.what === 'fx' && e.a === 5 && e.b === 25) telegraph = t;
      if (!inClip(t)) continue;
      const [sx, sy] = onScreen(c, t, e.x, e.y);
      const out = Math.max(0, -sx, sx - 1, -sy, sy - 1);
      if (out > .3) continue;
      const g = (out > 0 ? .45 * (1 - out / .3) : 1) * (1 - cover(t)), pan = clamp((sx - .5) * 1.5, -.85, .85);
      if (g < .02) continue;
      const v = .92 + .16 * r.next();
      if (e.what === 'shot') {
        if (e.a === 6) { if (gate('cannon', t, .075)) put(X.cannon({ seed: ++seed, pitch: v }), t, .3 * g, pan, .1); }
        else if (e.a === 11) put(X.mortarLaunch({ seed: ++seed }), t, .42 * g, pan, .12);
        else if (e.a === 10) {
          put(X.mortarLaunch({ seed: ++seed }), t, .45 * g, pan, .12);
          put(X.whoosh(.45, { seed: ++seed, from: 500, peak: 2600, to: 1200, panFrom: pan, panTo: pan * .3 }), t, .3 * g, 0, .1);
          const land = landings[10].find(l => l.frame > e.frame);
          const flight = land ? (land.frame - e.frame) / TL.FPS : .7;
          put(X.fallWhistle(Math.max(.3, flight - .12), { seed: ++seed }), t + .1, .16 * g, pan * .5, .15);
        } else if (e.a <= 4) {
          if (gate('gun', t, .07) && gate(`gun${e.a}`, t, .12)) put(X.blaster(guns[e.a], { seed: ++seed }), t, (e.a === 3 ? .16 : .11) * g * v, pan, .06);
        }
      } else if (e.what === 'gone') {
        if (!(e.b & 256) && (e.a <= 4 || e.a === 6) && gate('hit', t, .1)) put(X.hit({ seed: ++seed }), t, .07 * g, pan, .04);
      } else if (e.what === 'die') {
        if (e.a === 2 && gate('die', t, .06)) {
          const size = clamp(e.size / 25, .8, 3);
          put(X.creaturePop({ size, seed: ++seed }), t, .25 * g * Math.min(1.5, Math.sqrt(size)), pan, .12);
        }
      } else if (e.what === 'fx') {
        const kind = e.a, style = e.b;
        if (kind === 1 && style === 4) put(X.boom({ size: clamp(e.size / 150, .8, 1.6), seed: ++seed }), t, .5 * g, pan, .15);
        else if (kind === 1 && style === 26) {
          // The boss's laser: its charge whines from the telegraph (or the cut) to the shot, then the beam.
          const charge = clamp(t - Math.max(c.at, telegraph ?? t - .4), .3, 1.4);
          put(X.laser({ charge, len: .9, seed: ++seed }), t - charge, .42 * g, pan, .15);
        } else if (kind === 8) {
          switch (style) {
            case 48: if (e.size === 0) put(X.zap({ seed: ++seed }), t, .26 * g, pan, .1); else put(X.zap({ seed: ++seed, len: .09, bright: 1.25 }), t + .012 * e.size, .1 * g, pan, .08); break;
            case 22: put(X.crunch({ seed: ++seed }), t + .04, .38 * g, pan, .12); break;
            case 27: if (gate('core', t, .3)) put(X.bonk({ seed: ++seed }), t, .2 * g, pan, .1); break;
            case 15: if (gate('spawn', t, .2)) put(X.puff({ seed: ++seed }), t, .06 * g, pan, .05); break;
            case 28: put(X.bloop({ seed: ++seed, pitch: v }), t, .14 * g, pan, .1); break;
            case 23: put(X.crunch({ seed: ++seed, size: .7 }), t, .25 * g, pan, .1); break;
            case 19: if (gate('shove', t, .12)) put(X.boop({ seed: ++seed }), t, .15 * g, pan, .08); break;
            case 32: put(X.shimmer({ seed: ++seed }), t, .2 * g, pan, .2); break;
            case 20: put(X.wallDraw({ seed: ++seed }), t, .3 * g, pan, .08); break;
            case 43:
              if (e.size >= 5) { put(X.sparkleUp({ seed: ++seed }), t + .05, .32 * g, pan, .2); put(X.towerBuild({ seed: ++seed, kind: 0 }), t, .3 * g, pan, .1); }
              else put(X.towerBuild({ seed: ++seed, kind: [0, 1, 2][Math.round(e.x * 10) % 3] }), t, .46 * g, pan, .1);
              break;
            case 12:
              put(X.slideWhistle(.55, { seed: ++seed }), t, .34 * g, pan, .2);
              put(X.whoosh(.5, { seed: ++seed, from: 400, peak: 2000, to: 900, panFrom: pan, panTo: pan * .2 }), t, .3 * g, 0, .1);
              break;
            case 35: put(X.landing({ seed: ++seed }), t, .6 * g, pan, .15); put(X.whomp({ seed: ++seed }), t + .05, .35 * g, pan, .1); break;
            case 34: put(X.yoink({ seed: ++seed }), t, .3 * g, pan, .1); break;
            case 29: put(X.refWhistle({ seed: ++seed }), t, .22 * g, pan, .15); break;
            case 30: put(X.shimmer({ seed: ++seed }), t, .25 * g, pan, .2); break;
            default: break;
          }
        }
      } else if (e.what === 'impact') {
        // The page assembling: blocks knocking into place, pitched by where they land.
        if (gate('block', t, .03)) put(I.woodblock({ pitch: .55 + .35 * clamp(e.x, 0, 1), vel: .8, seed: ++seed }), t, .16 * g, pan, .1);
      } else if (e.what === 'spawn') {
        put(X.pop({ pitch: popTo(84 + 3 * Math.round(clamp(e.x, 0, 1) * 4)), seed: ++seed }), t, .2 * g, pan, .12);
      }
    }
    // The throw's wind-up: a ratchet tightening while the throw charges (plate frames 34 to 52).
    if (c.shot === 'coop_throw') {
      const from = timeOf(34), to = timeOf(52);
      for (let k = 0, t = from; t < to - .02; ++k, t += .06 - .025 * (t - from) / (to - from))
        put(X.click({ pitch: .7 + .6 * (t - from) / (to - from), seed: ++seed }), t, .1, -.05, .03);
    }
  }
  return { dry, wet };
}
