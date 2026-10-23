// BMP de 24 bits (las capturas del port) -> PNG. Uso: node tools/bmp2png.mjs a.bmp [b.bmp ...]
import { readFileSync, writeFileSync } from 'node:fs';
import { deflateSync } from 'node:zlib';

const crcTabla = Array.from({ length: 256 }, (_, n) => { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; return c >>> 0; });
const crc = b => { let c = 0xffffffff; for (const x of b) c = crcTabla[(c ^ x) & 255] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; };
const trozo = (tipo, datos) => {
  const t = Buffer.alloc(12 + datos.length);
  t.writeUInt32BE(datos.length, 0); t.write(tipo, 4, 'latin1'); datos.copy(t, 8);
  t.writeUInt32BE(crc(t.subarray(4, 8 + datos.length)), 8 + datos.length);
  return t;
};

export function bmpAPng(bmp) {
  const off = bmp.readUInt32LE(10), w = bmp.readInt32LE(18), h0 = bmp.readInt32LE(22), bpp = bmp.readUInt16LE(28);
  if (bpp !== 24) throw new Error(`solo BMP de 24 bits (es de ${bpp})`);
  const h = Math.abs(h0), pitch = (w * 3 + 3) & ~3, crudo = Buffer.alloc((w * 3 + 1) * h);
  for (let y = 0; y < h; y++) {
    const fila = off + pitch * (h0 > 0 ? h - 1 - y : y);
    for (let x = 0; x < w; x++) {
      const d = y * (w * 3 + 1) + 1 + x * 3;
      crudo[d] = bmp[fila + x * 3 + 2]; crudo[d + 1] = bmp[fila + x * 3 + 1]; crudo[d + 2] = bmp[fila + x * 3];
    }
  }
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4); ihdr[8] = 8; ihdr[9] = 2;
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), trozo('IHDR', ihdr), trozo('IDAT', deflateSync(crudo)), trozo('IEND', Buffer.alloc(0))]);
}

if (import.meta.url === `file:///${process.argv[1].replaceAll('\\', '/')}`) {
  for (const a of process.argv.slice(2)) writeFileSync(a.replace(/\.bmp$/i, '.png'), bmpAPng(readFileSync(a)));
}
