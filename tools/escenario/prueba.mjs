// Prueba de las herramientas de escenarios (no necesita el juego): node tools/escenario/prueba.mjs
import assert from 'node:assert/strict';
import { triangular, limpiar, areaFirmada } from './geometria.mjs';
import { leerNmd, escribirNmd } from './nmd.mjs';
import { leerCampo, escribirCampo } from './campo.mjs';
import { leerBmt, escribirBmt } from './bmt.mjs';
import { leerDat1, escribirDat1 } from '../dat1.mjs';
import { perlin } from './pintura.mjs';
import { arcoPuntiagudo } from './moonlit.mjs';
import { alturaPiso, orilla, NIVEL, MAR } from './playa.mjs';
import { aplicarParches } from '../../recomp/parches.mjs';

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
console.log('escenario: todo bien');
