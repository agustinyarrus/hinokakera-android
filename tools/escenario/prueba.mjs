// Prueba de las herramientas de escenarios (no necesita el juego): node tools/escenario/prueba.mjs
import assert from 'node:assert/strict';
import { triangular, limpiar, areaFirmada } from './geometria.mjs';
import { leerNmd, escribirNmd } from './nmd.mjs';
import { leerCampo, escribirCampo, escribirSuelo } from './campo.mjs';
import { leerBmt, escribirBmt, leerPng, png } from './bmt.mjs';
import { leerDat1, escribirDat1 } from '../dat1.mjs';
import { perlin } from './pintura.mjs';
import { arcoPuntiagudo } from './moonlit.mjs';
import { alturaPiso, orilla, NIVEL, MAR } from './playa.mjs';
import { aplicarParches } from '../../recomp/parches.mjs';
import { cabecera } from './zombis.mjs';
import { rueda } from './comun.mjs';
import { COLUMNAS, R_BORDE } from './caldera.mjs';

// Triangulación: el área de los triángulos tiene que dar la del polígono (con escalones, puntos repetidos y vanos).
const areaTris = (p, t) => t.reduce((s, [i, j, k]) => s + ((p[j][0] - p[i][0]) * (p[k][1] - p[i][1]) - (p[j][1] - p[i][1]) * (p[k][0] - p[i][0])) / 2, 0);
const vano = [[0, 0], [1, 0], ...arcoPuntiagudo(2, 1, 2.7, 4.1), [3, 0], [4, 0], [4, 6], [3, 6], [3, 6.4], [2, 6.4], [2, 6.4], [1, 6.4], [1, 5.8], [0, 5.8]];
for (const p of [vano, [[-0.55, 0], [0.55, 0], [0.55, 3.6], [0, 3.6], [0, 3.6], [-0.55, 3.2]], [[0, 0], [2, 0], [2, 1], [1.5, 1], [1, 1], [1, 2], [0, 2]]]) {
  const q = limpiar(p);
  assert.ok(Math.abs(areaTris(q, triangular(q)) - areaFirmada(q)) < 1e-9, 'la triangulación tapa todo el polígono');
}
// Arco apuntado: nace y muere en los arranques y pasa por el vértice.
const arco = arcoPuntiagudo(0, 1, 2, 3.5);
assert.deepEqual(arco[0].map((v) => +v.toFixed(9)), [-1, 2]);
assert.deepEqual(arco.at(-1).map((v) => +v.toFixed(9)), [1, 2]);
assert.ok(Math.abs(Math.max(...arco.map((p) => p[1])) - 3.5) < 1e-9);
// Ruido periódico: se repite cada px.
assert.ok(Math.abs(perlin(1.3, 2.7, 5, 8, 4) - perlin(9.3, 6.7, 5, 8, 4)) < 1e-12);

// Formatos: ida y vuelta byte a byte.
const m = { nombre: 'mod_x', mats: [{ nombre: '', col: [1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 0, 0], tex: 'tex_x.bmt', ini: 0, n: 1 }],
  vs: [0, 1, 2].map((i) => ({ p: [i, 0, 1], n: [0, 1, 0], c: 0xffffffff, uv: [i / 2, 1] })), is: [0, 1, 2] };
const nmd = escribirNmd(m);
assert.deepEqual(escribirNmd(leerNmd(nmd)), nmd);
const campo = [{ borrado: [0, 0, 0, 0], escala: 0.01, modelos: [{ nombre: 'mod_x.nmd', tipo: 3, z: 1, escribeZ: 1, mezcla: 2, pruebaAlfa: 0 }],
  efecto: 3, efecto2: 0, difusa: [1, 1, 1, 1], ambiente: [0.5, 0.5, 0.5, 1], dir: [0.1, -0.2, 0], viento: [5, 0, -5] }];
const fld = escribirCampo(campo);
assert.deepEqual(escribirCampo(leerCampo(fld)), fld);
assert.equal(leerCampo(fld)[0].modelos[0].nombre, 'mod_x.nmd');
const dat = escribirDat1([{ nombre: 'dat/a.dat', datos: Buffer.from('hola') }, { nombre: 'obj/b.nmd', datos: nmd }]);
const e = leerDat1(dat);
assert.deepEqual(e.map((x) => x.nombre), ['dat/a.dat', 'obj/b.nmd']);
assert.deepEqual(dat.subarray(e[1].offset, e[1].offset + e[1].tam), nmd);
const rgba = Buffer.from([255, 0, 0, 255, 0, 255, 0, 128, 0, 0, 255, 0, 255, 255, 255, 255]);
assert.deepEqual(leerBmt(escribirBmt({ w: 2, h: 2, rgba })).rgba, rgba);
// Playa: se pelea en plano (los personajes pisan en 0) y del lado del mar el agua llega justo a la orilla.
for (let th = 0; th < 360; th += 7.5) for (let r = 0; r <= 5.5; r += 0.5) assert.ok(alturaPiso(r, th) === 0, 'zona de pelea plana');
for (let th = MAR.a0; th <= MAR.a1; th += 5) assert.ok(Math.abs(alturaPiso(orilla(th), th) - NIVEL) < 0.05, 'el agua llega a la orilla');

// Parches del exe: cambian bytes solo si los originales coinciden, y juntan el C a insertar.
const img = Buffer.from([0x83, 0xf8, 0x07, 0x90]);
const iny = aplicarParches('# comentario\n1000 83 f8 07 => 83 f8 08\n1003 { eax = 9; }\n', img, 0x1000);
assert.deepEqual([...img], [0x83, 0xf8, 0x08, 0x90]);
assert.equal(iny.get(0x1003), '{ eax = 9; }');
assert.throws(() => aplicarParches('1000 83 f8 07 => 83 f8 09', img, 0x1000), /no coinciden/);
assert.deepEqual(leerPng(png({ w: 2, h: 2, rgba })).rgba, rgba);

// Modelos animados: los datos como los lee d3d/animado.c (5 floats por vértice: posición y uv).
const fig = { x: 1, z: 2, rumbo: 0.5, escala: 1.1, tipo: 1, fase: 0.3, vel: 0.2, radio: 2.24, brazo: 2, incl: 0.1, cojera: 0.5, sentido: -1 };
const datos = cabecera([fig], 3, [[0, 0.5, 0, -1], [0.1, 0.8, 0, 0]], [0, 1, 1]);
const F = (i) => { const v = datos[Math.floor(i / 5)], k = i % 5; return k < 3 ? v.p[k] : v.uv[k - 3]; };
assert.deepEqual([0, 1, 2, 3].map(F), [7777, -3131, 2468, 1], 'firma');
assert.deepEqual([4, 5, 6, 7].map(F), [datos.length, 1, 3, 2], 'vértices de datos, figuras, vértices por figura, huesos');
assert.deepEqual([8, 9, 10, 11, 12, 13, 14, 15].map(F), [0, 0.5, 0, -1, 0.1, 0.8, 0, 0], 'pivotes');
assert.deepEqual(Array.from({ length: 12 }, (_, k) => F(16 + k)), [1, 2, 0.5, 1.1, 1, 0.3, 0.2, 2.24, 2, 0.1, 0.5, -1], 'figura');
assert.deepEqual([28, 29, 30].map(F), [0, 1, 1], 'hueso de cada vértice');
assert.ok(datos.every((v) => v.n[1] === 1), 'normales válidas en los vértices de datos');
// La rueda de selección (rueda() en comun.mjs): 16 filas; 0-6 las del juego, 10 y 15 Random (la 7 del juego), en las
// miniaturas la 11 repite la 0 y las 7-9 llevan el alfa de la 3. Cada fila de prueba tiene todos sus bytes en fila + 1.
const delJuego = (alto) => ({ w: 1, h: 8 * alto, rgba: Buffer.concat([0, 1, 2, 3, 4, 5, 6, 7].map((k) => Buffer.alloc(4 * alto, k + 1))) });
const [m42, m24] = rueda(delJuego(64), delJuego(32));
const filas = (img, alto) => Array.from({ length: 16 }, (_, k) => img.rgba[4 * k * alto]);
assert.deepEqual([m42.h, m24.h], [1024, 512]);
assert.deepEqual(filas(m42, 64), [1, 2, 3, 4, 5, 6, 7, 4, 4, 4, 8, 1, 0, 0, 0, 8]);
assert.deepEqual(filas(m24, 32), [1, 2, 3, 4, 5, 6, 7, 0, 0, 0, 8, 0, 0, 0, 0, 8]);

// dat/suelo.dat como lo lee runtime/escenarios.c: "sue1", cantidad, y por escenario número, radio, columnas, (x z r alto).
const sue = escribirSuelo([{ etapa: 11, radio: 150, columnas: [{ x: -58, z: 0, r: 13, alto: 6 }, { x: 1, z: 2, r: 3, alto: 4 }] }]);
assert.equal(sue.toString('latin1', 0, 4), 'sue1');
assert.deepEqual([sue.readUInt32LE(4), sue.readUInt32LE(8), sue.readFloatLE(12), sue.readUInt32LE(16)], [1, 11, 150, 2]);
assert.deepEqual([0, 1, 2, 3, 4, 5, 6, 7].map((i) => sue.readFloatLE(20 + 4 * i)), [-58, 0, 13, 6, 1, 2, 3, 4]);
assert.equal(sue.length, 20 + 32);
// Eclipse Caldera: las columnas entran en la arena, no se pisan, dejan libre el arranque (±20, 0) con el cuerpo (3) y
// en cada escalera hay un salto (avanza ~14, sube hasta 16.5) de una a la siguiente.
for (const c of COLUMNAS) assert.ok(Math.hypot(c.x, c.z) + c.r < R_BORDE, `columna en (${c.x}, ${c.z}) fuera de la arena`);
COLUMNAS.forEach((a, i) => COLUMNAS.slice(i + 1).forEach((b) => assert.ok(Math.hypot(a.x - b.x, a.z - b.z) > a.r + b.r, `columnas (${a.x}, ${a.z}) y (${b.x}, ${b.z}) se pisan`)));
for (const x of [-20, 20]) for (const c of COLUMNAS) assert.ok(Math.hypot(x - c.x, c.z) > c.r + 3 + 10, `el arranque en x ${x} queda contra una columna`);
const col = (x, z) => COLUMNAS.find((c) => c.x === x && c.z === z);
for (const [a, b] of [[[-58, 0], [-88, -6]], [[-88, -6], [-116, 6]], [[58, 0], [90, 6]], [[90, 6], [118, -4]], [[-20, 78], [-52, 100]], [[-52, 100], [-30, 122]]]) {
  const p = col(...a), q = col(...b);
  assert.ok(Math.hypot(p.x - q.x, p.z - q.z) - p.r - q.r < 12 && q.alto - p.alto < 16.5 - 3, `escalera (${a}) -> (${b})`);
}
// las del eje x lo cruzan: la pelea arranca sobre esa línea
for (const c of COLUMNAS.slice(0, 6)) assert.ok(Math.abs(c.z) < c.r - 4, `la columna (${c.x}, ${c.z}) no cruza el eje x`);
console.log('escenario: todo bien');
