// Renders Geometric Warfare's new promo video via headless Edge + FFmpeg.
// Usage:
//   node build-video.mjs sheet [from] [to] [count]
//   node build-video.mjs stills t1 t2 ...
//   node build-video.mjs video [from] [to]
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const project = path.resolve(here, '..', '..');
const outDir = path.join(project, 'output');
const stillsDir = path.join(outDir, 'stills');
const platesDir = path.join(project, 'Saved', 'PromoPackage', 'Windows', 'GeometricWarfare', 'Saved', 'Promo', 'Plates');
const ffmpeg = path.join(project, 'douyinkaifa', 'tmp', 'video128-deps', 'imageio_ffmpeg', 'binaries', 'ffmpeg-win-x86_64-v7.1.exe');
const edge = [
  'C:/Program Files/Google/Chrome/Application/chrome.exe',
  'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',
].find(p => fs.existsSync(p));

const engineFonts = 'F:/UNREAL/UE_5.8/UE_5.8/Engine/Content/Slate/Fonts';
const fonts = {
  'Roboto-Black.ttf': path.join(engineFonts, 'Roboto-Black.ttf'),
  'Roboto-Bold.ttf': path.join(engineFonts, 'Roboto-Bold.ttf'),
  'Roboto-Medium.ttf': path.join(engineFonts, 'Roboto-Medium.ttf'),
  'NotoSansSC-VF.ttf': 'C:/Windows/Fonts/NotoSansSC-VF.ttf',
};

const mime = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.mjs': 'text/javascript; charset=utf-8',
  '.png': 'image/png',
  '.jpg': 'image/jpeg',
  '.ttf': 'font/ttf',
  '.json': 'application/json',
};

const W = 1920, H = 1080, FPS = 30;
const [mode = 'sheet', ...args] = process.argv.slice(2);
fs.mkdirSync(stillsDir, { recursive: true });

function buildPlateIndex() {
  const index = {};
  if (!fs.existsSync(platesDir)) return index;
  for (const shot of fs.readdirSync(platesDir)) {
    const dir = path.join(platesDir, shot);
    if (fs.statSync(dir).isDirectory()) {
      index[shot] = fs.readdirSync(dir)
        .filter(f => f.endsWith('.png'))
        .map(f => parseInt(f, 10))
        .sort((a, b) => a - b);
    }
  }
  return index;
}

let encoder = null;
let encoderDone = null;
let framesCount = 0;
let resolveDone;
const donePromise = new Promise(r => (resolveDone = r));
const savedStills = [];

function startEncoder(outFile) {
  encoder = spawn(ffmpeg, [
    '-hide_banner', '-loglevel', 'error', '-y',
    '-f', 'rawvideo', '-pix_fmt', 'rgba', '-s', `${W}x${H}`, '-r', String(FPS), '-i', '-',
    '-c:v', 'libx264', '-preset', 'medium', '-crf', '15', '-pix_fmt', 'yuv420p', '-movflags', '+faststart',
    outFile,
  ], { stdio: ['pipe', 'inherit', 'inherit'] });
  encoderDone = new Promise(r => encoder.on('close', r));
}

function sendFile(res, file) {
  fs.readFile(file, (err, data) => {
    if (err) { res.writeHead(404); res.end(); return; }
    res.writeHead(200, { 'Content-Type': mime[path.extname(file)] || 'application/octet-stream', 'Cache-Control': 'no-store' });
    res.end(data);
  });
}

const server = http.createServer((req, res) => {
  const url = new URL(req.url, 'http://127.0.0.1');
  const p = decodeURIComponent(url.pathname);
  if (req.method === 'POST') {
    const chunks = [];
    req.on('data', c => chunks.push(c));
    req.on('end', async () => {
      const body = Buffer.concat(chunks);
      if (p === '/frame') {
        if (!encoder.stdin.write(body)) await new Promise(r => encoder.stdin.once('drain', r));
        framesCount++;
        if (framesCount % 150 === 0) console.log(`  rendered ${framesCount} frames (${(framesCount / FPS).toFixed(1)}s)`);
      } else if (p === '/still') {
        const name = url.searchParams.get('name');
        const out = path.join(stillsDir, `${name}.png`);
        fs.writeFileSync(out, body);
        savedStills.push(out);
      } else if (p === '/log') {
        console.log('[browser]', body.toString());
      } else if (p === '/done') {
        resolveDone(body.toString());
      }
      res.writeHead(200);
      res.end('ok');
    });
    return;
  }

  if (p === '/' || p === '/index.html') return sendFile(res, path.join(here, 'index.html'));
  if (p === '/plates.json') {
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify(buildPlateIndex()));
    return;
  }
  if (p.startsWith('/src/')) return sendFile(res, path.join(here, 'src', p.slice(5)));
  if (p.startsWith('/assets/')) return sendFile(res, path.join(here, 'assets', p.slice(8)));
  if (p.startsWith('/fonts/') && fonts[p.slice(7)]) return sendFile(res, fonts[p.slice(7)]);
  if (p.startsWith('/plates/')) return sendFile(res, path.join(platesDir, ...p.slice(8).split('/')));
  res.writeHead(404);
  res.end();
});

await new Promise(r => server.listen(0, '127.0.0.1', r));
const port = server.address().port;

let query = '';
const rawVideoPath = path.join(outDir, 'promo-raw-video.mp4');
if (mode === 'video') {
  const from = parseFloat(args[0] ?? '0');
  const to = parseFloat(args[1] ?? '-1');
  startEncoder(rawVideoPath);
  query = `mode=video&from=${from}&to=${to}`;
  console.log(`Rendering video ${from}..${to < 0 ? 'end' : to}s -> ${rawVideoPath}`);
} else if (mode === 'stills') {
  query = `mode=stills&times=${args.join(',')}`;
} else if (mode === 'sheet') {
  const from = args[0] ?? '0';
  const to = args[1] ?? '81.4';
  const count = args[2] ?? '24';
  query = `mode=sheet&from=${from}&to=${to}&count=${count}`;
} else if (mode === 'serve') {
  console.log(`Preview server: http://127.0.0.1:${port}/?mode=preview`);
  await new Promise(() => {});
}

const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'gw-newpromo-'));
console.log(`Launching Edge -> http://127.0.0.1:${port}/?${query}`);
const browser = spawn(edge, [
  '--headless=new', '--disable-gpu', '--hide-scrollbars', '--mute-audio',
  '--no-first-run', '--no-default-browser-check',
  '--disable-background-timer-throttling', '--disable-renderer-backgrounding',
  '--disable-backgrounding-occluded-windows', '--disable-extensions',
  `--user-data-dir=${profile}`, `--window-size=${W},${H}`, '--force-device-scale-factor=1',
  `http://127.0.0.1:${port}/?${query}`,
], { stdio: ['ignore', 'inherit', 'inherit'] });
let browserDone = false;
browser.on('error', err => console.error('Browser spawn error:', err));
browser.on('exit', (code, sig) => {
  console.log('Browser exited:', code, sig);
  if (!browserDone && code !== 0) {
    server.close();
    process.exit(code || 1);
  }
});

const t0 = Date.now();
const status = await donePromise;
browserDone = true;
browser.kill();
if (encoder) {
  encoder.stdin.end();
  await encoderDone;
}

if (mode === 'sheet' && savedStills.length > 0) {
  const listTxt = path.join(stillsDir, 'sheet.txt');
  fs.writeFileSync(listTxt, savedStills.sort().map(f => `file '${f.replace(/\\/g, '/')}'\nduration 1\n`).join(''));
  const rows = Math.ceil(savedStills.length / 4);
  await new Promise(resolve => {
    spawn(ffmpeg, [
      '-hide_banner', '-loglevel', 'error', '-y',
      '-f', 'concat', '-safe', '0', '-i', listTxt,
      '-vf', `scale=480:270,tile=4x${rows}`, '-frames:v', '1',
      path.join(stillsDir, 'contact-sheet.jpg'),
    ], { stdio: 'inherit' }).on('close', resolve);
  });
}

if (mode === 'video') {
  const audioWav = path.join(outDir, 'promo-audio.wav');
  const finalMp4 = path.join(outDir, 'GeometricWarfare-Promo-2026.mp4');
  if (fs.existsSync(audioWav)) {
    await new Promise((resolve, reject) => {
      spawn(ffmpeg, [
        '-hide_banner', '-loglevel', 'error', '-y',
        '-i', rawVideoPath, '-i', audioWav,
        '-map', '0:v', '-map', '1:a', '-c:v', 'copy',
        '-af', 'loudnorm=I=-14:TP=-1.5:LRA=11', '-ar', '48000', '-c:a', 'aac', '-b:a', '256k',
        '-shortest', '-movflags', '+faststart', finalMp4,
      ], { stdio: 'inherit' }).on('close', code => code === 0 ? resolve() : reject(new Error(`mux failed ${code}`)));
    });
    console.log(`Final video: ${finalMp4} (${(fs.statSync(finalMp4).size / 1048576).toFixed(1)} MB)`);
  }
}

console.log(`${status} in ${((Date.now() - t0) / 1000).toFixed(1)}s`);
server.close();
try { fs.rmSync(profile, { recursive: true, force: true }); } catch { }
process.exit(0);
