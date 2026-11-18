// Renders the promo page (web/index.html) frame by frame in headless Edge and pipes the frames into ffmpeg.
//   node render.mjs video [from] [to]          -> Saved/Promo/promo-video.mp4 (no audio; music.mjs + mux add it)
//   node render.mjs stills t1 t2 ...           -> Saved/Promo/stills/<t>.png
//   node render.mjs sheet from to count        -> Saved/Promo/stills/sheet.jpg (count stills tiled 4 wide)
// The page draws everything on one 1920x1080 canvas; plates (Saved/Promo/Plates/<shot>/NNNN.png from
// GW.Promo.Capture) are served from the capture package's Saved folder.
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const project = path.resolve(here, '..', '..');
const out = path.join(project, 'Saved', 'Promo');
const plates = process.env.PROMO_PLATES || path.join(project, 'Saved', 'PromoPackage', 'Windows', 'GeometricWarfare', 'Saved', 'Promo', 'Plates');
const ffmpeg = process.env.FFMPEG || path.join(project, 'douyinkaifa', 'tmp', 'video128-deps', 'imageio_ffmpeg', 'binaries', 'ffmpeg-win-x86_64-v7.1.exe');
const edge = ['C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe', 'C:/Program Files/Google/Chrome/Application/chrome.exe'].find(p => fs.existsSync(p));
const engineFonts = 'F:/UNREAL/UE_5.8/UE_5.8/Engine/Content/Slate/Fonts';
const fonts = {
  'Roboto-Black.ttf': path.join(engineFonts, 'Roboto-Black.ttf'),
  'Roboto-Bold.ttf': path.join(engineFonts, 'Roboto-Bold.ttf'),
  'Roboto-Medium.ttf': path.join(engineFonts, 'Roboto-Medium.ttf'),
  'DroidSansFallback.ttf': path.join(engineFonts, 'DroidSansFallback.ttf'),
  'NotoSansSC-VF.ttf': 'C:/Windows/Fonts/NotoSansSC-VF.ttf',
};
const W = 1920, H = 1080, FPS = 30;
const types = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.mjs': 'text/javascript; charset=utf-8', '.png': 'image/png', '.webp': 'image/webp', '.jpg': 'image/jpeg', '.ttf': 'font/ttf', '.json': 'application/json' };

const [mode = 'stills', ...rest] = process.argv.slice(2);
fs.mkdirSync(path.join(out, 'stills'), { recursive: true });

function plateIndex() {
  const index = {};
  if (fs.existsSync(plates))
    for (const shot of fs.readdirSync(plates)) {
      const dir = path.join(plates, shot);
      if (fs.statSync(dir).isDirectory())
        index[shot] = fs.readdirSync(dir).filter(f => f.endsWith('.png')).map(f => parseInt(f, 10)).sort((a, b) => a - b);
    }
  return index;
}

let encoder = null, encoderDone = null, framesIn = 0, finish, stillsDone = [];
const finished = new Promise(r => (finish = r));

function startEncoder(file) {
  encoder = spawn(ffmpeg, ['-hide_banner', '-loglevel', 'error', '-y', '-f', 'rawvideo', '-pix_fmt', 'rgba', '-s', `${W}x${H}`, '-r', String(FPS), '-i', '-',
    '-c:v', 'libx264', '-preset', 'slow', '-crf', '15', '-pix_fmt', 'yuv420p', '-movflags', '+faststart', file], { stdio: ['pipe', 'inherit', 'inherit'] });
  encoderDone = new Promise(r => encoder.on('close', r));
}

function serveFile(res, file) {
  fs.readFile(file, (err, data) => {
    if (err) { res.writeHead(404); res.end(); return; }
    res.writeHead(200, { 'Content-Type': types[path.extname(file)] || 'application/octet-stream', 'Cache-Control': 'no-store' });
    res.end(data);
  });
}

const server = http.createServer((req, res) => {
  const url = new URL(req.url, 'http://x');
  const p = decodeURIComponent(url.pathname);
  if (req.method === 'POST') {
    const chunks = [];
    req.on('data', c => chunks.push(c));
    req.on('end', async () => {
      const body = Buffer.concat(chunks);
      if (p === '/frame') {
        if (body.length !== W * H * 4) console.error(`frame ${url.searchParams.get('i')} has ${body.length} bytes`);
        if (!encoder.stdin.write(body)) await new Promise(r => encoder.stdin.once('drain', r));
        ++framesIn;
        if (framesIn % 150 === 0) console.log(`  ${framesIn} frames (${(framesIn / FPS).toFixed(1)} s)`);
      } else if (p === '/still') {
        const name = url.searchParams.get('name');
        const file = path.join(out, 'stills', `${name}.png`);
        fs.writeFileSync(file, body);
        stillsDone.push(file);
      } else if (p === '/log') {
        console.log('[page]', body.toString());
      } else if (p === '/done') {
        finish(body.toString());
      }
      res.writeHead(200);
      res.end('ok');
    });
    return;
  }
  if (p === '/' || p === '/index.html') return serveFile(res, path.join(here, 'web', 'index.html'));
  if (p === '/plates.json') { res.writeHead(200, { 'Content-Type': 'application/json' }); res.end(JSON.stringify(plateIndex())); return; }
  if (p.startsWith('/web/')) return serveFile(res, path.join(here, 'web', p.slice(5)));
  if (p.startsWith('/assets/')) return serveFile(res, path.join(here, 'assets', p.slice(8)));
  if (p.startsWith('/fonts/') && fonts[p.slice(7)]) return serveFile(res, fonts[p.slice(7)]);
  if (p.startsWith('/plates/')) return serveFile(res, path.join(plates, ...p.slice(8).split('/')));
  res.writeHead(404);
  res.end();
});

await new Promise(r => server.listen(0, '127.0.0.1', r));
const port = server.address().port;
let query;
if (mode === 'video') {
  const from = parseFloat(rest[0] ?? '0'), to = parseFloat(rest[1] ?? '-1');
  const file = path.join(out, rest[2] || 'promo-video.mp4');
  startEncoder(file);
  query = `mode=video&from=${from}&to=${to}`;
  console.log(`rendering ${from}..${to < 0 ? 'end' : to} s -> ${file}`);
} else if (mode === 'sheet') {
  query = `mode=sheet&from=${rest[0] ?? 0}&to=${rest[1] ?? 82}&count=${rest[2] ?? 24}`;
} else if (mode === 'cover') {
  query = 'mode=cover';
} else if (mode === 'serve') {
  console.log(`http://127.0.0.1:${port}/?mode=preview`);
  await new Promise(() => {});
} else {
  query = `mode=stills&times=${rest.join(',')}`;
}

const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'gw-promo-edge-'));
const browser = spawn(edge, ['--headless=new', '--disable-gpu', '--hide-scrollbars', '--mute-audio', '--no-first-run', '--no-default-browser-check',
  '--disable-background-timer-throttling', '--disable-renderer-backgrounding', '--disable-backgrounding-occluded-windows', '--disable-extensions',
  `--user-data-dir=${profile}`, `--window-size=${W},${H}`, '--force-device-scale-factor=1', `http://127.0.0.1:${port}/?${query}`], { stdio: 'ignore' });

const started = Date.now();
const result = await finished;
browser.kill();
if (encoder) {
  encoder.stdin.end();
  await encoderDone;
}
if (mode === 'sheet') {
  // Tile the stills 4 wide at 480x270.
  const list = path.join(out, 'stills', 'sheet.txt');
  fs.writeFileSync(list, stillsDone.sort().map(f => `file '${f.replace(/\\/g, '/')}'\nduration 1\n`).join(''));
  const rows = Math.ceil(stillsDone.length / 4);
  await new Promise(r => spawn(ffmpeg, ['-hide_banner', '-loglevel', 'error', '-y', '-f', 'concat', '-safe', '0', '-i', list, '-vf', `scale=480:270,tile=4x${rows}`, '-frames:v', '1',
    path.join(out, 'stills', 'sheet.jpg')], { stdio: 'inherit' }).on('close', r));
}
console.log(`${result || 'done'} in ${((Date.now() - started) / 1000).toFixed(1)} s`);
server.close();
try { fs.rmSync(profile, { recursive: true, force: true }); } catch { }
process.exit(0);
