// Texturas BMT_ de Hinokakera CE (formato sacado del cargador del juego, 0x40c250 en el exe): color indexado.
//   "BMT_", u16 ancho, u16 alto, u8 n (bytes por índice), n bytes = colores - 1, paleta (u32 ARGB) y
//   ancho*alto índices de n bytes, fila 0 arriba.
import { deflateSync, crc32 } from 'node:zlib';

export function leerBmt(b) {
  if (b.toString('latin1', 0, 4) !== 'BMT_') throw new Error('no es BMT_');
  const w = b.readUInt16LE(4), h = b.readUInt16LE(6), n = b[8];
  let colores = 0;
  for (let i = 0; i < n; i++) colores |= b[9 + i] << (8 * i);
  colores += 1;
  const pal = 9 + n, ind = pal + colores * 4, rgba = Buffer.alloc(w * h * 4);
  for (let i = 0; i < w * h; i++) {
    let k = 0;
    for (let j = 0; j < n; j++) k |= b[ind + i * n + j] << (8 * j);
    const c = b.readUInt32LE(pal + k * 4); // ARGB
    rgba[4 * i] = (c >>> 16) & 255; rgba[4 * i + 1] = (c >>> 8) & 255; rgba[4 * i + 2] = c & 255; rgba[4 * i + 3] = c >>> 24;
  }
  return { w, h, rgba };
}

// RGBA -> BMT. Con muchos colores se usan índices de 2 o 3 bytes (como las texturas del juego).
export function escribirBmt({ w, h, rgba }) {
  const mapa = new Map(), pal = [], idx = new Uint32Array(w * h);
  for (let i = 0; i < w * h; i++) {
    const c = ((rgba[4 * i + 3] << 24) | (rgba[4 * i] << 16) | (rgba[4 * i + 1] << 8) | rgba[4 * i + 2]) >>> 0;
    let k = mapa.get(c);
    if (k === undefined) { k = pal.length; mapa.set(c, k); pal.push(c); }
    idx[i] = k;
  }
  const n = pal.length <= 256 ? 1 : pal.length <= 65536 ? 2 : 3;
  const b = Buffer.alloc(9 + n + pal.length * 4 + w * h * n);
  b.write('BMT_', 0, 'latin1'); b.writeUInt16LE(w, 4); b.writeUInt16LE(h, 6); b[8] = n;
  for (let j = 0; j < n; j++) b[9 + j] = ((pal.length - 1) >> (8 * j)) & 255;
  let p = 9 + n;
  for (const c of pal) { b.writeUInt32LE(c, p); p += 4; }
  for (let i = 0; i < w * h; i++) for (let j = 0; j < n; j++) b[p++] = (idx[i] >> (8 * j)) & 255;
  return b;
}

export function png({ w, h, rgba }) {
  const crudo = Buffer.alloc((w * 4 + 1) * h);
  for (let y = 0; y < h; y++) rgba.copy(crudo, y * (w * 4 + 1) + 1, y * w * 4, (y + 1) * w * 4);
  const trozo = (tipo, datos) => {
    const t = Buffer.from(tipo, 'ascii'), largo = Buffer.alloc(4), c = Buffer.alloc(4);
    largo.writeUInt32BE(datos.length); c.writeUInt32BE(crc32(Buffer.concat([t, datos])) >>> 0);
    return Buffer.concat([largo, t, datos, c]);
  };
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4); ihdr[8] = 8; ihdr[9] = 6;
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), trozo('IHDR', ihdr), trozo('IDAT', deflateSync(crudo)), trozo('IEND', Buffer.alloc(0))]);
}
