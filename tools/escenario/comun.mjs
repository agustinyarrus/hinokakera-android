// Lo común de los escenarios nuevos (moonlit, playa, zombis y caldera): leer archivos de la copia del juego, juntar
// las texturas y modelos que se generan, armar la rueda de selección y escribir <scratch>/mods/vsobj2.dat.
import { openSync, readSync, closeSync, writeFileSync, mkdirSync, readFileSync } from 'node:fs';
import { join } from 'node:path';
import { JUEGO, SCRATCH } from '../datos.mjs';
import { escribirDat1 } from '../dat1.mjs';
import { escribirNmd } from './nmd.mjs';
import { leerBmt, escribirBmt, png } from './bmt.mjs';
import { vista } from './vista.mjs';

const ARCHIVOS = ['vsobj2.dat', 'vsobj.dat', 'vsdat.dat']; // orden en que el juego los busca
export const PNG = process.argv.includes('--png') ? process.argv[process.argv.indexOf('--png') + 1] : null;
if (PNG) mkdirSync(PNG, { recursive: true });

// Como leerDat1 pero solo con la tabla (los dat grandes no se leen enteros).
export function leerDat1Tabla(t) {
  const n = t.readUInt32LE(4), r = [];
  for (let i = 0, p = 8; i < n && p + 4 <= t.length; i++) {
    const l = t.readUInt32LE(p); r.push({ nombre: t.toString('latin1', p + 4, p + 4 + l).replace(/\0.*$/s, ''), offset: t.readUInt32LE(p + 4 + l), tam: t.readUInt32LE(p + 8 + l) });
    p += 12 + l;
  }
  return r;
}

export function sacar(nombre) {
  for (const a of ARCHIVOS) {
    const fd = openSync(join(JUEGO, a), 'r');
    try {
      const cab = Buffer.alloc(8); readSync(fd, cab, 0, 8, 0);
      const tabla = Buffer.alloc(Math.min(cab.readUInt32LE(4) * 300 + 8, 8 << 20)); readSync(fd, tabla, 0, tabla.length, 0);
      const e = leerDat1Tabla(tabla).find((x) => x.nombre === nombre);
      if (e) { const b = Buffer.alloc(e.tam); readSync(fd, b, 0, e.tam, e.offset); return b; }
    } finally { closeSync(fd); }
  }
  throw new Error(`no está en el juego: ${nombre}`);
}

export const salidas = []; // [{nombre, datos}] que van a vsobj2.dat
export const generadas = new Map(); // textura -> RGBA8, para las vistas
export const modelos = new Map(); // nombre -> nmd para las vistas (los animados, en pose de descanso y colocados)

export function textura(nombre, L) {
  generadas.set(`${nombre}.bmt`, { w: L.w, h: L.h, rgba: L.rgba8() });
  salidas.push({ nombre: `obj/${nombre}.bmt`, datos: L.bmt() });
  if (PNG) writeFileSync(join(PNG, `${nombre}.png`), L.png());
}
// RGBA8 tal cual (sin tramado: para retocar texturas del juego sin cambiar los píxeles que no se tocan).
export function texturaRGBA(nombre, img) {
  generadas.set(`${nombre}.bmt`, img);
  salidas.push({ nombre: `obj/${nombre}.bmt`, datos: escribirBmt(img) });
  if (PNG) writeFileSync(join(PNG, `${nombre}.png`), png(img));
}
export function modelo(m, paraVista = m) {
  modelos.set(m.nombre, paraVista);
  salidas.push({ nombre: `obj/${m.nombre}.nmd`, datos: escribirNmd(m) });
}

// Textura para las vistas: generada o del juego (BMT con paleta o BMA ARGB).
export function texDe(n) {
  if (generadas.has(n)) return generadas.get(n);
  const b = sacar(`obj/${n}`);
  if (b.toString('latin1', 0, 4) === 'BMT_') return leerBmt(b);
  const w = b.readUInt16LE(4), h = b.readUInt16LE(6), rgba = Buffer.alloc(w * h * 4);
  for (let i = 0; i < w * h; i++) { const c = b.readUInt32LE(8 + 4 * i); rgba.set([(c >>> 16) & 255, (c >>> 8) & 255, c & 255, c >>> 24], 4 * i); }
  return { w, h, rgba };
}

// La rueda de selección con 11 lugares (analisis/parches.txt): tex_sys42 pasa a 512x1024 (franjas de 64) y tex_sys24
// a 256x512 (filas de 32). Quedan 0-6 las del juego, 10 Random y 15 otra vez Random (el anterior de la 0, y el nombre
// del Random elegido, -1); en las miniaturas la 11 repite la 0 (el siguiente de Random) y las 7-9, de los escenarios
// nuevos, salen con el alfa de las del juego (bordes redondeados): cada escenario pinta encima la suya.
export function rueda(sys42, sys24) {
  const crecer = (t, alto, copias) => {
    const o = { w: t.w, h: 16 * alto, rgba: Buffer.alloc(t.w * 16 * alto * 4) }, fila = t.w * alto * 4;
    for (const [de, a] of copias) t.rgba.copy(o.rgba, a * fila, de * fila, (de + 1) * fila);
    return o;
  };
  const juego = [0, 1, 2, 3, 4, 5, 6].map((f) => [f, f]);
  return [crecer(sys42, 64, [...juego, [7, 10], [7, 15], [0, 11], [3, 7], [3, 8], [3, 9]]), crecer(sys24, 32, [...juego, [7, 10], [7, 15]])];
}

// Miniatura de la selección de escenario: el escenario `e` sin HUD ni personajes, promediado 4x4 en la franja de
// 256x64 número `fila` de tex_sys42 (`sys`, RGBA8; el alfa no se toca). Con --png deja además la vista grande con la
// cámara del juego.
export function miniatura(e, fila, sys, nombrePng, cam = { pos: [0, 0.55, -5.7], rumbo: 86, cabeceo: 3, fovV: 17 }) {
  const mods = e.modelos.map((m) => ({ ...m, nmd: modelos.get(m.nombre.replace('.nmd', '')) }));
  const luz = { difusa: e.difusa, ambiente: e.ambiente, dir: e.dir }, cache = new Map(), texturas = (n) => (cache.has(n) ? cache.get(n) : cache.set(n, texDe(n)).get(n));
  const W = 1024, img = vista({ w: W, h: 256, modelos: mods, texturas, luz, cam });
  for (let y = 0; y < 64; y++) for (let x = 0; x < 256; x++) {
    const c = [0, 0, 0];
    for (let dy = 0; dy < 4; dy++) for (let dx = 0; dx < 4; dx++) for (let k = 0; k < 3; k++) c[k] += Math.min(1, img[3 * ((4 * y + dy) * W + 4 * x + dx) + k]) / 16;
    const i = 4 * ((fila * 64 + y) * sys.w + x);
    for (let k = 0; k < 3; k++) sys.rgba[i + k] = Math.round(c[k] * 255);
  }
  if (PNG) {
    const G = vista({ w: 1332, h: 600, modelos: mods, texturas, luz, cam: { pos: [0, 0.5, -5.7], rumbo: 90, cabeceo: 0, fovV: 30.6 } }), V = Buffer.alloc(1332 * 600 * 4);
    for (let i = 0; i < 1332 * 600; i++) V.set([0, 1, 2].map((k) => Math.round(Math.min(1, G[3 * i + k]) * 255)).concat(255), 4 * i);
    writeFileSync(join(PNG, nombrePng), png({ w: 1332, h: 600, rgba: V }));
  }
}

// Escribe <scratch>/mods/vsobj2.dat: lo generado más lo que traía el vsobj2.dat del juego (su parche).
export function escribirMods() {
  const v2 = readFileSync(join(JUEGO, 'vsobj2.dat')), nuevos = new Set(salidas.map((s) => s.nombre));
  if (nuevos.size !== salidas.length) throw new Error(`archivos repetidos: ${salidas.map((s) => s.nombre).filter((n, i, a) => a.indexOf(n) !== i).join(' ')}`);
  const viejos = leerDat1Tabla(v2).filter((x) => !nuevos.has(x.nombre)).map((x) => ({ nombre: x.nombre, datos: v2.subarray(x.offset, x.offset + x.tam) }));
  mkdirSync(join(SCRATCH, 'mods'), { recursive: true });
  const destino = join(SCRATCH, 'mods', 'vsobj2.dat');
  writeFileSync(destino, escribirDat1([...salidas, ...viejos]));
  console.log(`${salidas.length} archivos nuevos (${[...modelos.keys()].join(', ')}) -> ${destino}`);
}
