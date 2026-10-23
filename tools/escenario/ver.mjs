// Renderiza un escenario de field.dat sin correr el juego:
//   node tools/escenario/ver.mjs <n> <salida.png> [--sin mod_x,...] [--sintex tex_x.bmt,...]
// Busca los archivos como el juego (mods/vsobj2.dat si existe, después vsobj2, vsobj y vsdat de la copia del juego).
import { openSync, readSync, closeSync, existsSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { JUEGO, SCRATCH } from '../datos.mjs';
import { leerNmd } from './nmd.mjs';
import { leerBmt } from './bmt.mjs';
import { leerCampo } from './campo.mjs';
import { vista } from './vista.mjs';
import { Lienzo } from './pintura.mjs';

const archivos = [join(SCRATCH, 'mods', 'vsobj2.dat'), ...['vsobj2.dat', 'vsobj.dat', 'vsdat.dat'].map((a) => join(JUEGO, a))].filter(existsSync);
const tablas = archivos.map((ruta) => {
  const fd = openSync(ruta, 'r'), cab = Buffer.alloc(8); readSync(fd, cab, 0, 8, 0);
  const t = Buffer.alloc(Math.min(cab.readUInt32LE(4) * 300 + 8, 8 << 20)); readSync(fd, t, 0, t.length, 0);
  const e = new Map();
  for (let i = 0, p = 8; i < t.readUInt32LE(4) && p + 4 <= t.length; i++) {
    const l = t.readUInt32LE(p), nom = t.toString('latin1', p + 4, p + 4 + l).replace(/\0.*$/s, '');
    if (!e.has(nom)) e.set(nom, [t.readUInt32LE(p + 4 + l), t.readUInt32LE(p + 8 + l)]);
    p += 12 + l;
  }
  return { fd, e };
});
const sacar = (nombre) => {
  for (const { fd, e } of tablas) if (e.has(nombre)) { const [o, n] = e.get(nombre), b = Buffer.alloc(n); readSync(fd, b, 0, n, o); return b; }
  throw new Error(`no está: ${nombre}`);
};
const leerTex = (n) => {
  const b = sacar(`obj/${n}`);
  if (b.toString('latin1', 0, 4) === 'BMT_') return leerBmt(b);
  const w = b.readUInt16LE(4), h = b.readUInt16LE(6), rgba = Buffer.alloc(w * h * 4); // BMA_: ARGB sin paleta
  for (let i = 0; i < w * h; i++) { const c = b.readUInt32LE(8 + 4 * i); rgba.set([(c >>> 16) & 255, (c >>> 8) & 255, c & 255, c >>> 24], 4 * i); }
  return { w, h, rgba };
};

const [n, salida] = process.argv.slice(2), sin = process.argv.includes('--sin') ? process.argv[process.argv.indexOf('--sin') + 1].split(',') : [];
const sinTex = process.argv.includes('--sintex') ? process.argv[process.argv.indexOf('--sintex') + 1].split(',') : [];
const e = leerCampo(sacar('dat/field.dat'))[+n];
const modelos = e.modelos.filter((m) => !sin.includes(m.nombre.replace('.nmd', ''))).map((m) => { const nmd = leerNmd(sacar(`obj/${m.nombre}`)); nmd.mats = nmd.mats.filter((t) => !sinTex.includes(t.tex)); return { ...m, nmd }; });
const cache = new Map(), texturas = (t) => (cache.has(t) ? cache.get(t) : cache.set(t, leerTex(t)).get(t));
const W = 1332, H = 600, img = vista({ w: W, h: H, modelos, texturas, luz: { difusa: e.difusa, ambiente: e.ambiente, dir: e.dir }, cam: { pos: [0, 0.5, -5.7], rumbo: 90, cabeceo: 0, fovV: 30.6 } });
const L = new Lienzo(W, H);
for (let i = 0; i < W * H; i++) L.d.set([Math.min(1, img[3 * i]), Math.min(1, img[3 * i + 1]), Math.min(1, img[3 * i + 2]), 1], 4 * i);
writeFileSync(salida, L.png());
for (const { fd } of tablas) closeSync(fd);
console.log(`${modelos.map((m) => m.nombre).join(' ')} -> ${salida}`);
