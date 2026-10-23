// Ícono del APK: el del exe original (recurso HIKAKE_ICON, 32x32 de 256 colores; ubicado con REA
// inspect_pe_resources) agrandado sin filtrar a 192x192 para mipmap-xxxhdpi.
// Uso: node tools/icono.mjs
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { deflateSync, crc32 } from 'node:zlib';
import { join } from 'node:path';

import { JUEGO } from './datos.mjs';

const RAIZ = new URL('..', import.meta.url).pathname.replace(/^\/(\w:)/, '$1');
const EXE = join(JUEGO, 'hinokakera.exe');
const OFFSET = 1601728, BYTES = 2216, ESCALA = 6;

const d = readFileSync(EXE).subarray(OFFSET, OFFSET + BYTES);
const w = d.readInt32LE(4), h = d.readInt32LE(8) / 2, bpp = d.readUInt16LE(14);
if (d.readUInt32LE(0) !== 40 || w !== 32 || h !== 32 || bpp !== 8) throw new Error(`no es el ícono esperado (${w}x${h}, ${bpp} bpp)`);
const paleta = 40, xor = paleta + 256 * 4, and = xor + w * h, pitchAnd = ((w + 31) >> 5) * 4;
const W = w * ESCALA, H = h * ESCALA, crudo = Buffer.alloc((W * 4 + 1) * H);
for (let y = 0; y < H; y++) {
  crudo[y * (W * 4 + 1)] = 0; // filtro PNG: ninguno
  const fy = h - 1 - Math.floor(y / ESCALA); // el DIB va de abajo hacia arriba
  for (let x = 0; x < W; x++) {
    const fx = Math.floor(x / ESCALA), i = d[xor + fy * w + fx], o = y * (W * 4 + 1) + 1 + x * 4;
    const transparente = (d[and + fy * pitchAnd + (fx >> 3)] >> (7 - (fx & 7))) & 1;
    crudo[o] = d[paleta + i * 4 + 2]; crudo[o + 1] = d[paleta + i * 4 + 1]; crudo[o + 2] = d[paleta + i * 4]; crudo[o + 3] = transparente ? 0 : 255;
  }
}
const trozo = (tipo, datos) => {
  const t = Buffer.from(tipo, 'ascii'), largo = Buffer.alloc(4), c = Buffer.alloc(4);
  largo.writeUInt32BE(datos.length); c.writeUInt32BE(crc32(Buffer.concat([t, datos])) >>> 0);
  return Buffer.concat([largo, t, datos, c]);
};
const ihdr = Buffer.alloc(13);
ihdr.writeUInt32BE(W, 0); ihdr.writeUInt32BE(H, 4); ihdr[8] = 8; ihdr[9] = 6; // RGBA de 8 bits
const png = Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), trozo('IHDR', ihdr), trozo('IDAT', deflateSync(crudo)), trozo('IEND', Buffer.alloc(0))]);
const dir = join(RAIZ, 'android/app/src/main/res/mipmap-xxxhdpi');
mkdirSync(dir, { recursive: true });
writeFileSync(join(dir, 'ic_launcher.png'), png);
console.log(`ícono ${W}x${H} -> ${join(dir, 'ic_launcher.png')}`);
