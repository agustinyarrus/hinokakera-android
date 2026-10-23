// Compara dos capturas BMP de 24 bits píxel por píxel. Uso: node tools/comparar.mjs a.bmp b.bmp [dif.png]
// Informa cuántos píxeles son idénticos, la diferencia máxima por canal y dónde está; la imagen de
// diferencias marca en rojo lo distinto (más intenso cuanto más difiere).
import { readFileSync, writeFileSync } from 'node:fs';
import { bmpAPng } from './bmp2png.mjs';

function leer(ruta) {
  const b = readFileSync(ruta), off = b.readUInt32LE(10), w = b.readInt32LE(18), h0 = b.readInt32LE(22);
  const h = Math.abs(h0), pitch = (w * 3 + 3) & ~3, px = Buffer.alloc(w * h * 3);
  for (let y = 0; y < h; y++) b.copy(px, y * w * 3, off + pitch * (h0 > 0 ? h - 1 - y : y), off + pitch * (h0 > 0 ? h - 1 - y : y) + w * 3);
  return { w, h, px };
}

const [ra, rb, salida] = process.argv.slice(2);
const a = leer(ra), b = leer(rb);
if (a.w !== b.w || a.h !== b.h) { console.log(`tamaños distintos: ${a.w}x${a.h} vs ${b.w}x${b.h}`); process.exit(1); }
let iguales = 0, max = 0, donde = null, suma = 0;
const hist = new Array(256).fill(0);
const dif = Buffer.alloc(54 + ((a.w * 3 + 3) & ~3) * a.h);
for (let i = 0; i < a.w * a.h; i++) {
  let m = 0;
  for (let k = 0; k < 3; k++) m = Math.max(m, Math.abs(a.px[3 * i + k] - b.px[3 * i + k]));
  hist[m]++;
  suma += m;
  if (!m) iguales++;
  if (m > max) { max = m; donde = [i % a.w, Math.floor(i / a.w)]; }
}
const total = a.w * a.h;
console.log(`${iguales}/${total} píxeles idénticos (${(100 * iguales / total).toFixed(3)} %), diferencia máx ${max}${donde ? ` en (${donde})` : ''}, media ${(suma / total).toFixed(4)}`);
console.log('distribución de la diferencia máx por píxel: ' + hist.map((n, d) => n ? `${d}:${n}` : '').filter(Boolean).slice(0, 20).join(' '));
if (salida) {
  // BMP de diferencias -> PNG.
  dif.write('BM', 0, 'latin1'); dif.writeUInt32LE(dif.length, 2); dif.writeUInt32LE(54, 10); dif.writeUInt32LE(40, 14);
  dif.writeInt32LE(a.w, 18); dif.writeInt32LE(-a.h, 22); dif.writeUInt16LE(1, 26); dif.writeUInt16LE(24, 28);
  const pitch = (a.w * 3 + 3) & ~3;
  for (let y = 0; y < a.h; y++) for (let x = 0; x < a.w; x++) {
    const i = y * a.w + x;
    let m = 0;
    for (let k = 0; k < 3; k++) m = Math.max(m, Math.abs(a.px[3 * i + k] - b.px[3 * i + k]));
    const gris = (a.px[3 * i] + a.px[3 * i + 1] + a.px[3 * i + 2]) / 12;
    const o = 54 + y * pitch + x * 3;
    if (m) { dif[o] = 0; dif[o + 1] = 0; dif[o + 2] = Math.min(255, 96 + m * 16); }
    else { dif[o] = dif[o + 1] = dif[o + 2] = gris; }
  }
  writeFileSync(salida, bmpAPng(dif));
}
