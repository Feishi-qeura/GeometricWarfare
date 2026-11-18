// The promo's score: a cheerful, family-friendly tune for ukulele, whistle and friends at 120 BPM, laid bar by bar
// on the cut in web/timeline.js. The tempo never changes (a beat is always half a second, so every cut stays on a
// beat); short two-beat bars at the section breaks keep each section's footage landing on a downbeat.
//   0-6    intro      C   pops on the shapes, the logo stamp hits the band in
//   6-22   defense    C   theme A: whistle over island-strum ukulele, brushes, shaker
//   22-23  break      A7  the "联机" card (stop-time, a fill)
//   23-36  co-op      D   theme B: whistle and melodica trade phrases like two friends, group claps, ska chops, tuba
//   36-37  break      C7  the "即将更新" card
//   37-53  systems    F   theme C: music box and celesta, pizzicato, a woodblock clock: work in progress
//   53-54  hand-over  G7  the notepad drops
//   54-76  the list   C   pencil-paced, a rising note per item, the whistle returns and it builds
//   76-82  outro      C   theme A's hook in full, a final chord
import { Rng, Bus } from './dsp.mjs';
import * as I from './instruments.mjs';

const BEAT = .5;
const SWING = .08; // an off-beat eighth lands this many beats late: a light, bouncy swing

// --- the bar map -----------------------------------------------------------------------------------------------------
export const Bars = [];
function bars(from, chords, beats, section, opts = {}) {
  chords.forEach((c, k) => Bars.push({ at: from + k * beats * BEAT, beats, section, chords: Array.isArray(c) ? c : [c], index: k, ...opts }));
}
bars(0, ['C', 'C', ['F', 'G7']], 4, 'intro');
bars(6, ['C', 'Am', 'F', 'G', 'C', 'Am', ['Dm', 'G7'], 'C'], 4, 'defense');
bars(22, ['A7'], 2, 'break1');
bars(23, ['D', 'Bm', 'G', 'A', 'D', ['G', 'A7']], 4, 'coop');
bars(35, ['D'], 2, 'coopEnd');
bars(36, ['C7'], 2, 'break2');
bars(37, ['F', 'Dm', 'Bb', 'C7', 'F', 'Dm', ['Gm7', 'C7'], 'F'], 4, 'systems');
bars(53, ['G7'], 2, 'listIn');
bars(54, ['C', 'G', 'Am', 'F', 'C', 'Am', 'F', 'G', 'C', 'F', 'G7'], 4, 'list');
bars(76, ['C', ['F', 'G7'], 'C'], 4, 'outro');

// Chord tones (MIDI): the bass root in the second octave and the triad round middle C.
const Chords = {
  C: { root: 36, tones: [60, 64, 67] }, C7: { root: 36, tones: [60, 64, 67, 70] }, D: { root: 38, tones: [62, 66, 69] },
  Dm: { root: 38, tones: [62, 65, 69] }, E7: { root: 40, tones: [64, 68, 71, 74] }, F: { root: 41, tones: [60, 65, 69] },
  G: { root: 43, tones: [62, 67, 71] }, G7: { root: 43, tones: [62, 65, 67, 71] }, Gm7: { root: 43, tones: [62, 65, 67, 70] },
  A: { root: 45, tones: [61, 64, 69] }, A7: { root: 45, tones: [61, 64, 67, 69] }, Am: { root: 45, tones: [60, 64, 69] },
  Bb: { root: 46, tones: [62, 65, 70] }, Bm: { root: 47, tones: [62, 66, 71] },
};
// The chord sounding at beat b of a bar (two-chord bars split at the half).
const chordAt = (bar, b) => bar.chords[bar.chords.length === 2 && b >= bar.beats / 2 ? 1 : 0];
// Seconds for beat b of a bar, with swing on off-beat eighths.
function time(bar, b) {
  const whole = Math.floor(b + 1e-6), frac = b - whole;
  const swung = Math.abs(frac - .5) < 1e-6 ? .5 + SWING : Math.abs(frac - .25) < 1e-6 ? .25 + SWING / 2 : Math.abs(frac - .75) < 1e-6 ? .75 + SWING / 2 : frac;
  return bar.at + (whole + swung) * BEAT;
}
const section = name => Bars.filter(b => b.section === name);

// --- melodies (beats within the bar, length in beats, MIDI) ----------------------------------------------------------
// Theme A (C): a call that climbs to A and falls home, answered higher, ending on an upward run.
const ThemeA = [
  [[0, .5, 76], [.5, .5, 79], [1, .5, 81], [1.5, .5, 79], [2, 1, 76], [3, .9, 72]],
  [[0, .5, 74], [.5, .5, 76], [1, .5, 72], [1.5, 1.4, 69], [3.5, .5, 72]],
  [[0, .5, 77], [.5, .5, 81], [1, .5, 84], [1.5, .5, 81], [2, 1, 79], [3, .9, 77]],
  [[0, .5, 76], [.5, .5, 77], [1, .5, 74], [1.5, .5, 71], [2, 1.4, 67], [3.5, .5, 67]],
  [[0, .5, 76], [.5, .5, 79], [1, .5, 81], [1.5, .5, 79], [2, .5, 76], [2.5, .5, 79], [3, 1, 84]],
  [[0, .5, 83], [.5, .5, 81], [1, .5, 79], [1.5, .5, 76], [2, 1.9, 81]],
  [[0, .5, 77], [.5, .5, 76], [1, .5, 74], [1.5, .5, 77], [2, .5, 76], [2.5, .5, 74], [3, .5, 71], [3.5, .5, 74]],
  [[0, 1, 72], [1, .5, 76], [1.5, .5, 79], [2, 1.5, 84]],
];
// Theme B (D): two friends trading two-beat phrases (whistle calls, melodica answers), then together.
const ThemeBCall = [
  [[0, .5, 81], [.5, .5, 78], [1, .5, 81], [1.5, .5, 83]],
  [[0, .5, 83], [.5, .5, 81], [1, .5, 78], [1.5, .5, 74]],
  [[0, .5, 86], [.5, .5, 83], [1, .5, 79], [1.5, .5, 83]],
  [[0, .5, 85], [.5, .5, 83], [1, .5, 81], [1.5, .5, 79]],
];
const ThemeBAnswer = [
  [[2, .5, 81], [2.5, .5, 78], [3, .9, 74]],
  [[2, .5, 78], [2.5, .5, 76], [3, .5, 74], [3.5, .5, 71]],
  [[2, .5, 86], [2.5, .5, 83], [3, .9, 79]],
  [[2, .5, 78], [2.5, .5, 76], [3, .5, 73], [3.5, .5, 76]],
];
const ThemeBTogether = [
  [[0, .5, 78], [.5, .5, 81], [1, 1, 86], [2, .5, 81], [2.5, .5, 78], [3, 1, 81]],
  [[0, .5, 83], [.5, .5, 81], [1, .5, 79], [1.5, .5, 83], [2, .5, 81], [2.5, .5, 83], [3, .5, 85], [3.5, .5, 88]],
];
// Theme C (F): a music box tune in two-bar phrases, one per system card.
const ThemeC = [
  [[0, .5, 84], [.5, .5, 81], [1, 1, 77], [2, .5, 79], [2.5, .5, 81], [3, 1, 84]],
  [[0, .5, 86], [.5, .5, 84], [1, 1, 81], [2, 2, 77]],
  [[0, .5, 86], [.5, .5, 82], [1, 1, 77], [2, .5, 79], [2.5, .5, 82], [3, 1, 86]],
  [[0, .5, 88], [.5, .5, 86], [1, .5, 84], [1.5, .5, 82], [2, 2, 79]],
  [[0, .5, 84], [.5, .5, 81], [1, 1, 77], [2, .5, 79], [2.5, .5, 81], [3, 1, 84]],
  [[0, .5, 89], [.5, .5, 88], [1, 1, 86], [2, 2, 81]],
  [[0, .5, 82], [.5, .5, 81], [1, .5, 79], [1.5, .5, 77], [2, .5, 76], [2.5, .5, 79], [3, .5, 82], [3.5, .5, 88]],
  [[0, 1, 89], [1, .5, 84], [1.5, .5, 81], [2, 2, 77]],
];

// --- the renderer ----------------------------------------------------------------------------------------------------
export function renderScore(length, opts = {}) {
  const dry = new Bus(length), wet = new Bus(length);
  const r = new Rng(20261113);
  // Stems by instrument family (for balancing): play() files each sound under the current tag, which T() sets.
  const stems = {};
  let tag = 'misc';
  const T = name => { tag = name; };
  const human = (s = .006) => r.gauss() * s;
  // play(buffer, at, gain, pan, reverb send)
  const play = (y, at, gain = 1, pan = 0, send = .2) => {
    if (!y) return;
    dry.add(y, at, gain, pan);
    if (send > 0) wet.add(y, at, gain * send, pan);
    if (opts.stems) (stems[tag] ??= new Bus(length)).add(y, at, gain, pan);
  };
  // A melody line for the breathy leads: notes as {t, d, m, v} in seconds from `origin`.
  const line = (barsList, phrases, transpose = 0, vel = 1) => {
    const notes = [];
    barsList.forEach((bar, k) => {
      for (const [b, len, m] of phrases[k] || []) {
        const t = time(bar, b), end = time(bar, b + len);
        notes.push({ t, d: Math.max(.08, end - t - .015), m: m + transpose, v: vel * (b % 1 === 0 ? 1 : .88) });
      }
    });
    return notes;
  };
  const voice = (fn, notes, gain, pan, send, seed) => {
    if (!notes.length) return;
    T(fn === I.whistle ? 'whistle' : 'melodica');
    const origin = notes[0].t - .05;
    const y = fn(notes.map(n => ({ ...n, t: n.t - origin })), { seed });
    play(y, origin, gain, pan, send);
  };
  const mallets = (fn, notes, gain, pan, send, len = 1.6) => {
    T('mallets');
    for (const n of notes) play(fn(n.m, { vel: n.v, len, seed: Math.round(n.t * 1000) }), n.t, gain, pan, send);
  };

  // Rhythm section helpers ------------------------------------------------------------------------------------------
  const strum = (bar, pattern, vel = .8, pan = -.28, send = .12) => {
    T('uke');
    for (const [b, dir, mute] of pattern) {
      const chord = chordAt(bar, b);
      const ring = mute ? .05 : BEAT * 1.15;
      const y = I.ukeStrum(chord, { dir, mute, vel: vel * (dir === 'up' ? .8 : 1) * (1 + .08 * r.bi()), ring, seed: Math.round(time(bar, b) * 997) + 3 });
      play(y, time(bar, b) + human(.004), .45, pan, send);
    }
  };
  const Island = [[0, 'down'], [1, 'down'], [1.5, 'up'], [2.5, 'up'], [3, 'down'], [3.5, 'up']];
  const Ska = [[0, 'down'], [.5, 'up', true], [1.5, 'up', true], [2, 'down'], [2.5, 'up', true], [3.5, 'up', true]];
  const bassLine = (bar, steps, gain = .5, fn = I.bass) => {
    T('bass');
    for (const [b, len, interval] of steps) {
      if (b >= bar.beats) continue;
      const root = Chords[chordAt(bar, b)].root;
      play(fn(root + interval, len * BEAT * .92, { vel: 1 + .06 * r.bi(), seed: Math.round(time(bar, b) * 101) }), time(bar, b), gain, 0, .03);
    }
  };
  const drums = (bar, kit) => {
    T('drums');
    const at = b => time(bar, b) + human(.003);
    for (const b of kit.kick || []) if (b < bar.beats) play(I.kick({ vel: 1, seed: Math.round(b * 7) }), at(b), kit.kickGain ?? .5, 0, 0);
    for (const b of kit.snare || []) if (b < bar.beats) play(I.snare({ brush: true, seed: Math.round(time(bar, b) * 31) }), at(b), kit.snareGain ?? .16, .05, .12);
    for (const b of kit.clap || []) if (b < bar.beats) play(I.clap({ seed: Math.round(time(bar, b) * 17) }), at(b), kit.clapGain ?? .14, -.1, .2);
    for (const b of kit.group || []) if (b < bar.beats) play(I.groupClap({ people: 8, seed: Math.round(time(bar, b) * 13) }), at(b), kit.groupGain ?? .55, 0, .25);
    for (const b of kit.shaker || []) if (b < bar.beats) play(I.shaker({ vel: b % 1 ? 1 : .7, seed: Math.round(time(bar, b) * 53) }), at(b), kit.shakerGain ?? .075, .35, .05);
    for (const b of kit.tamb || []) if (b < bar.beats) play(I.tambourine({ vel: b % 1 ? .7 : 1, seed: Math.round(time(bar, b) * 61) }), at(b), kit.tambGain ?? .06, -.4, .1);
    for (const b of kit.wood || []) if (b < bar.beats) play(I.woodblock({ pitch: Math.floor(b) % 2 ? 1.33 : 1, vel: .8, seed: Math.round(b * 5) }), at(b), kit.woodGain ?? .1, .25, .15);
    for (const b of kit.snap || []) if (b < bar.beats) play(I.snap({ seed: Math.round(time(bar, b) * 7) }), at(b), kit.snapGain ?? .12, .2, .2);
  };
  const eighths = n => Array.from({ length: n * 2 }, (_, k) => k / 2);
  const padChord = (bar, gain = .07, cutoff = 1800) => {
    T('pad');
    bar.chords.forEach((c, k) => {
      const len = bar.beats / bar.chords.length * BEAT;
      play(I.pad(Chords[c].tones, len - .05, { cutoff, seed: Math.round(bar.at * 10) + k, attack: .25, release: .5 }), bar.at + k * len, gain * 2, 0, .35);
    });
  };
  // A fanfare stab: brass voices on chord tones, an octave up.
  const stab = (at, chord, len = .3, gain = .16, up = 12) => {
    T('brass');
    for (const m of Chords[chord].tones) play(I.brass(m + up - 12, len, { vel: .9, seed: Math.round(at * 100) + m }), at, gain, (m % 3 - 1) * .3, .3);
  };
  const crash = (at, gain = .22) => T('drums') ?? play(I.crash({ seed: Math.round(at * 10) }), at, gain, .2, .2);

  // --- intro -------------------------------------------------------------------------------------------------------
  {
    // The three shapes bouncing onto the page sing C E G; the logo stamp brings everyone in.
    T('mallets');
    [[.35, 72], [.55, 76], [.75, 79]].forEach(([t, m]) => play(I.marimba(m, { vel: .9, len: 1.2, seed: m }), t, .28, (m - 76) / 8, .3));
    const hit = 1.5;
    T('drums');
    play(I.kick({ vel: 1.1, punch: 1.2 }), hit, .6, 0, 0);
    play(I.groupClap({ people: 9, seed: 5 }), hit, .5, 0, .3);
    crash(hit, .26);
    stab(hit, 'C', .45, .16);
    T('uke');
    play(I.ukeStrum('C', { vel: 1, ring: .9, seed: 77 }), hit, .46, -.28, .15);
    T('mallets');
    [96, 100, 103, 108].forEach((m, k) => play(I.glock(m, { vel: .6, len: 1.4, seed: m }), hit + .06 + k * .05, .13, .4, .4));
    const [, b1, b2] = section('intro');
    strum(b1, Island, .8);
    strum(b2, Island, .85);
    bassLine(b1, [[0, 1.5, 0], [2, 1.5, 7]], .45);
    bassLine(b2, [[0, 1.5, 0], [2, 1, 0], [3, .5, 7]], .45);
    drums(b1, { kick: [0], shaker: eighths(4), snare: [3] });
    drums(b2, { kick: [0, 2], shaker: eighths(4), snare: [1, 3] });
    // Over the chips: a glockenspiel answer to the pops (E G C an octave up).
    T('mallets');
    [[3.0, 88], [3.25, 91], [3.5, 96]].forEach(([t, m]) => play(I.glock(m, { vel: .5, len: .9, seed: m }), t, .09, .3, .4));
    // Into defense: a tom fill under the page wipe.
    T('drums');
    [[5.0, 50], [5.25, 47], [5.5, 45], [5.625, 43], [5.75, 41]].forEach(([t, m], k) => play(I.tom(m, { vel: .8 + k * .05, seed: k }), t, .3, -.3 + k * .15, .1));
    padChord(b2, .05);
  }

  // --- defense: theme A ----------------------------------------------------------------------------------------------
  const def = section('defense');
  {
    def.forEach((bar, k) => {
      strum(bar, Island, k < 4 ? .78 : .85);
      bassLine(bar, k % 2 ? [[0, 1.5, 0], [2, 1, 7], [3, .5, 12]] : [[0, 1.5, 0], [2, 1.5, 7]], .5);
      drums(bar, { kick: [0, 2], snare: [1, 3], clap: k >= 4 ? [1, 3] : [], shaker: eighths(4), tamb: k >= 4 ? [1, 3] : [] });
      padChord(bar, .045);
    });
    T('drums');
    play(I.kick({ vel: 1 }), 6, .55, 0, 0);
    crash(6, .2);
    const melody = line(def, ThemeA, 12);
    voice(I.whistle, melody, .25, .05, .28, 11);
    // The glockenspiel doubles the second half an octave up, for sparkle.
    mallets(I.glock, line(def.slice(4), ThemeA.slice(4), 24, .8), .07, .35, .35, 1.2);
    // The marimba keeps eighth-note chord tones under the second half.
    T('mallets');
    def.slice(4).forEach(bar => eighths(4).forEach(b => {
      const tones = Chords[chordAt(bar, b)].tones, m = tones[(b * 2) % tones.length] + 12;
      play(I.marimba(m, { vel: .55 + .2 * (b % 1 === 0), len: .5, seed: Math.round(time(bar, b) * 7) }), time(bar, b), .07, -.45, .2);
    }));
    // The page is won: a glockenspiel run up onto the 胜利 stamp (21.3 s), brass on the stamp.
    T('mallets');
    [[21.06, 72], [21.14, 76], [21.22, 79], [21.3, 84]].forEach(([t, m]) => play(I.glock(m + 12, { vel: .8, len: 1.2, seed: m }), t, .1, .2, .4));
    stab(21.3, 'C', .35, .13);
  }

  // --- break into co-op: stop-time on A7, a fill ---------------------------------------------------------------------
  {
    stab(22.0, 'A7', .25, .15);
    T('uke');
    play(I.ukeStrum('A7', { vel: .9, ring: .4, seed: 9 }), 22.0, .44, -.28, .15);
    T('bass');
    play(I.bass(45, .4), 22.0, .5, 0, .03);
    T('drums');
    play(I.kick({ vel: 1 }), 22.0, .5, 0, 0);
    [[22.5, 52], [22.625, 50], [22.75, 47], [22.875, 45]].forEach(([t, m], k) => play(I.tom(m, { vel: .85, seed: 20 + k }), t, .3, .3 - k * .2, .1));
    play(I.snare({ brush: false, seed: 4 }), 22.875, .12, 0, .1);
  }

  // --- co-op: theme B, two friends ------------------------------------------------------------------------------------
  const coop = section('coop');
  {
    T('drums');
    play(I.kick({ vel: 1.1, punch: 1.1 }), 23, .55, 0, 0);
    crash(23, .22);
    coop.forEach((bar, k) => {
      strum(bar, Ska, .85, -.3);
      bassLine(bar, [[0, 1, 0], [2, 1, 7], [3.5, .5, 12]], .42, I.tuba);
      drums(bar, { kick: [0, 2, 2.5], group: [1, 3], snare: [1, 3], snareGain: .1, tamb: eighths(4), tambGain: .05, shaker: eighths(4), shakerGain: .05 });
      if (k % 2 === 1) stab(time(bar, 3.5), chordAt(bar, 3.5), .16, .1);
      padChord(bar, .04, 2200);
    });
    // The friends: whistle calls, melodica answers, then both together (the melodica a third below).
    voice(I.whistle, line(coop.slice(0, 4), ThemeBCall, 0), .26, -.25, .28, 21);
    voice(I.melodica, line(coop.slice(0, 4), ThemeBAnswer, 0), .42, .3, .28, 22);
    const together = line(coop.slice(4, 6), ThemeBTogether, 0);
    voice(I.whistle, together, .24, -.25, .28, 23);
    // A diatonic third below in D: major thirds under F#, B and C#, minor thirds under the rest.
    voice(I.melodica, together.map(n => ({ ...n, m: n.m - ([6, 11, 1].includes(n.m % 12) ? 4 : 3) })), .3, .3, .28, 24);
    // The room's result: an A7 fanfare onto D.
    T('brass');
    [[34.0, 69], [34.25, 73], [34.5, 76], [34.75, 79]].forEach(([t, m]) => play(I.brass(m, .22, { vel: .9 }), t, .14, 0, .3));
    stab(35.0, 'D', .5, .17);
    T('uke');
    play(I.ukeStrum('D', { vel: 1, ring: .8, seed: 35 }), 35.0, .46, -.28, .15);
    T('drums');
    play(I.kick({ vel: 1.1 }), 35.0, .55, 0, 0);
    crash(35.0, .22);
    play(I.groupClap({ people: 9, seed: 35 }), 35.0, .5, 0, .3);
    T('bass');
    play(I.tuba(38, .9), 35.0, .4, 0, .03);
    voice(I.whistle, [{ t: 35.0, d: .9, m: 86, v: 1 }], .26, -.25, .3, 25);
  }

  // --- break into the systems: C7, a little hammering --------------------------------------------------------------
  {
    stab(36.0, 'C7', .22, .12);
    T('uke');
    play(I.ukeStrum('C7', { vel: .8, ring: .45, seed: 36 }), 36.0, .42, -.28, .15);
    T('bass');
    play(I.bass(36, .4), 36.0, .45, 0, .03);
    T('drums');
    [36.5, 36.625, 36.75].forEach((t, k) => play(I.woodblock({ pitch: .7 + .15 * k, vel: .9, seed: k }), t, .14, .2, .15));
  }

  // --- systems: theme C, work in progress ----------------------------------------------------------------------------
  const sys = section('systems');
  {
    sys.forEach((bar, k) => {
      // Pizzicato bass, a woodblock clock, brushes and a soft shaker; a light strum.
      bassLine(bar, [[0, 1, 0], [2, 1, 7], [3.5, .5, 12]], .46);
      drums(bar, {
        kick: [0, 2.5], kickGain: .38, snare: [1, 3], snareGain: .1, wood: [1, 3], woodGain: .08, shaker: eighths(4), shakerGain: .06,
        snap: k % 2 ? [3.5] : [],
      });
      strum(bar, [[0, 'down'], [1, 'down'], [2, 'down'], [2.5, 'up'], [3.5, 'up']], .68, -.3, .18);
      // Marimba arpeggio, rising through the chord.
      T('mallets');
      eighths(4).forEach(b => {
        const tones = Chords[chordAt(bar, b)].tones, m = tones[(b * 2) % tones.length] + (b >= 2 ? 12 : 0);
        play(I.marimba(m, { vel: .5, len: .45, seed: Math.round(time(bar, b) * 3) }), time(bar, b), .08, .45, .25);
      });
      padChord(bar, .055, 1500);
    });
    const tune = line(sys, ThemeC, 0);
    mallets(I.musicBox, tune, .27, -.15, .4, 1.8);
    mallets(I.celesta, tune.map(n => ({ ...n, m: n.m - 12, v: n.v * .6 })), .11, .2, .35, 1.4);
  }

  // --- hand-over to the list: G7 under the notepad ---------------------------------------------------------------------
  {
    const bar = section('listIn')[0];
    T('uke');
    play(I.ukeStrum('G7', { vel: .75, ring: .9, seed: 53 }), 53.0, .44, -.28, .2);
    T('bass');
    play(I.bass(43, .9), 53.0, .45, 0, .03);
    T('mallets');
    [79, 83, 86, 89].forEach((m, k) => play(I.glock(m, { vel: .5, len: 1, seed: m }), 53.0 + k * .07, .09, .3, .4));
    drums(bar, { kick: [0], kickGain: .35, shaker: eighths(2), shakerGain: .06 });
  }

  // --- the list: pencil-paced, building ------------------------------------------------------------------------------
  const list = section('list');
  {
    list.forEach((bar, k) => {
      const late = k >= 4, last = k >= 8;
      bassLine(bar, late ? [[0, 1, 0], [1.5, .5, 7], [2, 1, 7], [3, .5, 12]] : [[0, 1.5, 0], [2, 1, 7], [3.5, .5, 12]], .4);
      // A music box spelling each chord upward in eighths, like a hand ticking things off.
      T('mallets');
      eighths(4).forEach(b => {
        const tones = Chords[chordAt(bar, b)].tones, m = tones[(b * 2) % tones.length] + 12 + (b >= 2 ? 12 : 0);
        play(I.musicBox(m, { vel: .45, len: .8, seed: Math.round(time(bar, b) * 11) }), time(bar, b), k >= 4 ? .15 : .26, .35, .35);
      });
      drums(bar, {
        kick: last ? [0, 2] : [0, 2.5], kickGain: last ? .45 : .36, snap: !last ? [1, 3] : [], snapGain: .11, snare: last ? [1, 3] : [1, 3],
        snareGain: last ? .16 : .08, shaker: eighths(4), shakerGain: late ? .07 : .06, clap: last ? [1, 3] : [],
      });
      strum(bar, last ? Island : late ? [[0, 'down'], [1.5, 'up'], [2, 'down'], [3, 'down'], [3.5, 'up']] : [[0, 'down'], [2, 'down'], [3, 'up']],
        last ? .82 : late ? .66 : .56, -.3);
      padChord(bar, .055, 1700);
    });
    // Each item written gets the next note of a rising scale (C6 up to C7) as its quantity pops.
    T('mallets');
    [84, 86, 88, 89, 91, 93, 95, 96].forEach((m, i) => play(I.glock(m, { vel: .75, len: 1.4, seed: m }), 56 + 2 * i + .8, .1, -.2 + .05 * i, .4));
    // The whistle comes back with the first phrase of theme A over the second half of the list.
    voice(I.whistle, line(list.slice(4, 8), ThemeA.slice(0, 4), 12, .85), .26, .05, .3, 31);
    // "你想要什么？" — the melodica asks (rising), the whistle answers.
    voice(I.melodica, [{ t: 72.5, d: .22, m: 79, v: .9 }, { t: 72.75, d: .22, m: 81, v: .9 }, { t: 73.0, d: .22, m: 84, v: .9 }, { t: 73.25, d: .6, m: 86, v: 1 }], .36, .3, .3, 32);
    // The answer climbs G7 to its seventh, which falls onto the outro's first note.
    voice(I.whistle, [{ t: 74.0, d: .22, m: 79, v: .9 }, { t: 74.25, d: .22, m: 83, v: .9 }, { t: 74.5, d: .22, m: 86, v: .95 }, { t: 74.75, d: .45, m: 89, v: 1 }], .26, -.2, .3, 33);
    // The build into the outro: a snare roll and rising strums.
    T('drums');
    for (let t = 75.0, k = 0; t < 76; t += t < 75.5 ? .125 : .0625, ++k)
      play(I.snare({ brush: false, seed: 700 + k }), t, .05 + .09 * (t - 75), (k % 2 ? .15 : -.15), .1);
  }

  // --- outro: theme A's hook, a final chord ----------------------------------------------------------------------------
  const outro = section('outro');
  {
    T('drums');
    play(I.kick({ vel: 1.2, punch: 1.2 }), 76, .6, 0, 0);
    crash(76, .26);
    play(I.groupClap({ people: 10, seed: 76 }), 76, .55, 0, .3);
    stab(76, 'C', .4, .15);
    outro.slice(0, 2).forEach((bar, k) => {
      strum(bar, Island, .85);
      bassLine(bar, [[0, 1.5, 0], [2, 1.5, 7]], .5);
      drums(bar, { kick: [0, 2], snare: [1, 3], clap: [1, 3], shaker: eighths(4), tamb: [1, 3] });
      padChord(bar, .05);
    });
    const hook = [
      [[0, .5, 76], [.5, .5, 79], [1, .5, 81], [1.5, .5, 79], [2, 1, 76], [3, .9, 72]],
      [[0, .5, 77], [.5, .5, 81], [1, .5, 84], [1.5, .5, 81], [2, .5, 79], [2.5, .5, 83], [3, 1, 86]],
    ];
    voice(I.whistle, line(outro.slice(0, 2), hook, 12), .26, .05, .3, 41);
    mallets(I.glock, line(outro.slice(0, 2), hook, 24, .8), .06, .35, .4, 1.2);
    // The last chord: everyone on C, the glockenspiel climbing out, a long ring.
    const end = 80;
    T('drums');
    play(I.kick({ vel: 1.2 }), end, .55, 0, 0);
    crash(end, .24);
    stab(end, 'C', 1.4, .14);
    T('uke');
    play(I.ukeStrum('C', { vel: 1, ring: 1.8, seed: 80 }), end, .46, -.28, .25);
    T('bass');
    play(I.bass(36, 1.6), end, .5, 0, .03);
    T('pad');
    play(I.pad([60, 64, 67, 72], 1.6, { cutoff: 2000, seed: 80, attack: .1, release: .5 }), end, .06, 0, .4);
    voice(I.whistle, [{ t: end, d: 1.5, m: 96, v: 1 }], .22, .05, .35, 42);
    T('mallets');
    [84, 88, 91, 96, 100].forEach((m, k) => play(I.glock(m, { vel: .7, len: 2, seed: m }), end + .1 + k * .08, .09, -.3 + .15 * k, .45));
    play(I.triangleBell({ vel: .7 }), end + 1.0, .06, .4, .5);
  }
  return { dry, wet, stems };
}
