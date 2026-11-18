import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.dirname(fileURLToPath(import.meta.url));
const markdown = fs.readFileSync(path.join(root, 'itch-details.md'), 'utf8');
const escape = s => s.replaceAll('&', '&amp;').replaceAll('<', '&lt;').replaceAll('>', '&gt;').replaceAll('"', '&quot;');
const inline = s => escape(s).replace(/\*\*(.+?)\*\*/g, '<strong>$1</strong>').replace(/\*([^*]+)\*/g, '<em>$1</em>');
const blocks = markdown.trim().split(/\r?\n\s*\r?\n/);
const imageEntries = [];
let section = 'Opening';
const body = blocks.map(block => {
  const image = block.match(/^!\[(.*?)\]\((.*?)\)$/);
  if (image) {
    imageEntries.push({ file: image[2], caption: image[1], section });
    return `<figure><img src="${escape(image[2])}" alt="${escape(image[1])}" width="1600" height="900"><figcaption>${escape(image[1])}</figcaption></figure>`;
  }
  if (block.startsWith('## ')) {
    section = block.slice(3);
    return `<h2>${inline(section)}</h2>`;
  }
  if (block.startsWith('# ')) return `<h1>${inline(block.slice(2))}</h1>`;
  if (block.startsWith('- ')) return `<ul>\n${block.split(/\r?\n/).map(line => `<li>${inline(line.slice(2))}</li>`).join('\n')}\n</ul>`;
  return `<p>${inline(block.replace(/\r?\n/g, ' '))}</p>`;
}).join('\n\n');

const textHtml = body.replace(/<figure>.*?<\/figure>/gs, '').replace(/\n{3,}/g, '\n\n');
fs.writeFileSync(path.join(root, 'itch-details-text.html'), textHtml + '\n', 'utf8');
const plain = markdown.replace(/^!\[.*?\]\(.*?\)\s*$/gm, '').replace(/^#{1,2} /gm, '').replace(/\*\*/g, '').replace(/^\*(.*?)\*$/gm, '$1').replace(/\n{3,}/g, '\n\n').trim();
fs.writeFileSync(path.join(root, 'itch-details.txt'), plain + '\n', 'utf8');

const preview = `<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>Geometric Warfare — itch.io Details Preview</title>
<style>
*{box-sizing:border-box}body{margin:0;background:#e8e3d8;color:#242a34;font-family:system-ui,-apple-system,"Segoe UI",sans-serif;font-size:17px;line-height:1.75}main{max-width:920px;margin:42px auto;background:#fffdf7;padding:48px 48px 28px;border:1px solid #d4cdbf;box-shadow:0 8px 32px #272d3310}h1,h2{line-height:1.2;letter-spacing:-.035em}h1{font-size:46px;margin:0 0 20px}h2{font-size:30px;margin:48px 0 20px}p{margin:0 0 20px}strong{font-weight:750}figure{margin:30px -48px 34px}img{display:block;width:100%;height:auto;background:#e9e2ce}figcaption{font-size:13px;color:#666e76;padding:10px 24px 0;text-align:center;line-height:1.5}ul{padding-left:24px}li{margin-bottom:10px}p:last-child{font-size:13px;color:#73706a;border-top:1px solid #e7e1d5;padding-top:20px}aside{max-width:920px;margin:20px auto -20px;padding:0 16px;color:#6a645b;font-size:13px;line-height:1.5}@media(max-width:640px){body{font-size:16px}main{padding:28px 22px;margin:32px 12px 16px}h1{font-size:36px}h2{font-size:26px}figure{margin-left:-22px;margin-right:-22px}aside{margin-top:12px}}
</style></head><body><aside>Local preview of the itch.io description. Actual typography follows your itch.io theme.</aside><main>${body}</main></body></html>`;
fs.writeFileSync(path.join(root, 'preview.html'), preview, 'utf8');
const sources = [
  'Saved/PromoPackage/Windows/GeometricWarfare/Saved/Promo/Plates/coop_defend/0060.png',
  'Saved/PromoPackage/Windows/GeometricWarfare/Saved/Promo/Plates/td_boss/0078.png',
  'Saved/ExpeditionVerification/expedition-boss-zh.png',
  'Saved/ExpeditionVerification/expedition-revive-zh.png',
  'Saved/PromoPackage/Windows/GeometricWarfare/Saved/Promo/Plates/coop_throw/0082.png',
  'Saved/PromoPackage/Windows/GeometricWarfare/Saved/Promo/Plates/hub_hats/0060.png',
  'Saved/HubVerification/hub-board-en.png',
];
const csv = s => `"${s.replaceAll('"', '""')}"`;
fs.writeFileSync(path.join(root, 'screenshot-map.csv'), '\uFEFF' + ['Order,File,Insert after section,English caption,Source (project-relative)', ...imageEntries.map((image, i) => [String(i+1), image.file, image.section, image.caption, sources[i]].map(csv).join(','))].join('\r\n'), 'utf8');
for (const image of imageEntries) {
  const full = path.join(root, image.file);
  if (!fs.existsSync(full) || fs.statSync(full).size === 0) throw new Error(`Missing screenshot: ${full}`);
}
if (imageEntries.length !== 7) throw new Error('Expected seven screenshots');
if (/[\u3400-\u9fff]/u.test(plain)) throw new Error('Unexpected Chinese in English description');
if (/file:\/\/|D:\\/.test(textHtml)) throw new Error('Local file path in pasteable HTML');
console.log(JSON.stringify({words: plain.split(/\s+/).length, screenshots: imageEntries.length, output: root}, null, 2));
