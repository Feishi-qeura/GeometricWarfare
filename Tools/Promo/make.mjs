// Builds the finished promo: the soundtrack (music.mjs: score + sound effects, mastered to -14 LUFS), every frame
// (render.mjs video -> promo-video-silent.mp4), then one MP4 with the audio as AAC.
// Output: Saved/Promo/GeometricWarfare-DevUpdate-2026-11.mp4, copied to Saved/Promo/promo-video.mp4.
import { spawn } from 'node:child_process';
import path from 'node:path';
import fs from 'node:fs';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const project = path.resolve(here, '..', '..');
const out = path.join(project, 'Saved', 'Promo');
const ffmpeg = process.env.FFMPEG || path.join(project, 'douyinkaifa', 'tmp', 'video128-deps', 'imageio_ffmpeg', 'binaries', 'ffmpeg-win-x86_64-v7.1.exe');
const run = (cmd, args) => new Promise((resolve, reject) => {
  const p = spawn(cmd, args, { stdio: 'inherit', cwd: here });
  p.on('close', code => (code === 0 ? resolve() : reject(new Error(`${path.basename(cmd)} exited ${code}`))));
});

const started = Date.now();
const skipVideo = process.argv.includes('--audio-only');
await run(process.execPath, ['music.mjs']);
if (!skipVideo) await run(process.execPath, ['render.mjs', 'video', '0', '-1', 'promo-video-silent.mp4']);
const final = path.join(out, 'GeometricWarfare-DevUpdate-2026-11.mp4');
await run(ffmpeg, ['-hide_banner', '-loglevel', 'error', '-y', '-i', path.join(out, 'promo-video-silent.mp4'), '-i', path.join(out, 'promo-audio.wav'),
  '-map', '0:v', '-map', '1:a', '-c:v', 'copy', '-c:a', 'aac', '-b:a', '256k', '-ar', '48000', '-shortest', '-movflags', '+faststart', final]);
fs.copyFileSync(final, path.join(out, 'promo-video.mp4'));
console.log(`${final} (${(fs.statSync(final).size / 1048576).toFixed(1)} MB), copied to promo-video.mp4, in ${((Date.now() - started) / 1000).toFixed(0)} s`);
