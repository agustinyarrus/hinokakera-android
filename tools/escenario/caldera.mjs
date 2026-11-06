// Eclipse Caldera: una isla de basalto en el medio de un lago de lava, dentro de la caldera de un volcán, bajo un
// eclipse total. Columnas de basalto de distintas alturas a las que los luchadores se suben de un salto (escaleras de
// columnas y una cumbre a la que solo se llega desde otra), un cristal de fuego gigante que flota y gira sobre la
// lava, cascadas de lava que caen de la caldera, braseros en el borde y brasas que suben.
// Es el escenario 11 de field.dat y el lugar 9 de la rueda de Stage Select (analisis/parches.txt).
//
// Lo que lo hace jugable va del lado del juego (runtime/escenarios.c, por los ganchos de analisis/ganchos.txt): las
// columnas de dat/suelo.dat son piso a su altura y pared para el que está más abajo, la arena tiene borde, y los
// modelos mod_w* quedan fijos en el mundo (los demás, como en todos los escenarios, siguen al centro de la pelea).
// Medidas: el juego cuenta en unidades de 1/20 de las de los modelos del escenario; los luchadores miden ~20 y
// saltan ~16.5 (sin doble salto), así que las columnas de 5, 9 y 13 se alcanzan desde el piso y la de 20 desde otra.
import { readFileSync } from 'node:fs';
import { leerPng } from './bmt.mjs';
import { Lienzo, azar, fbm, perlin, sat, suave, mezclar, hex, trazo, poligono, desenfocar } from './pintura.mjs';
import { Malla, unit, cruz, resta, suma, escalar, punto } from './geometria.mjs';
import { textura, modelo, miniatura } from './comun.mjs';
import { domo } from './moonlit.mjs';
import { animado, fuegoMalla } from './zombis.mjs';

export const ESCENARIO = 11, FILA = 9; // en field.dat y en la rueda (fila de nombres y miniaturas)
const E = 20; // unidades del juego por unidad de modelo
const RAD = Math.PI / 180;

// Columnas en unidades del juego: centro (x, z), radio de lo que se pisa y alto de la tapa. Los luchadores arrancan
// en (±20, 0) mirándose sobre el eje x (la pelea se mueve por toda la arena después) y la cámara mira a +z desde
// (0, -114). El centro queda libre; detrás de cada luchador hay una escalera de tres (6, 11, 16), al norte otra que
// llega a la cumbre (20, solo desde la de 10), al sur un estrado bajo que se sube caminando y pilares sueltos.
// Entre columnas de una escalera hay menos de 10 de hueco (un salto avanza ~14) y las del eje x lo cruzan: la pelea
// arranca sobre esa línea y se camina sobre ella.
export const COLUMNAS = [
  { x: -58, z: 0, r: 13, alto: 6 }, { x: -88, z: -6, r: 13, alto: 11 }, { x: -116, z: 6, r: 11, alto: 16 }, // oeste
  { x: 58, z: 0, r: 13, alto: 6 }, { x: 90, z: 6, r: 13, alto: 11 }, { x: 118, z: -4, r: 11, alto: 16 }, // este
  { x: -20, z: 78, r: 14, alto: 5 }, { x: -52, z: 100, r: 14, alto: 10 }, { x: -30, z: 122, r: 11, alto: 20 }, { x: 30, z: 96, r: 15, alto: 8 }, // norte
  { x: -48, z: -82, r: 16, alto: 3.5 }, { x: 40, z: -104, r: 14, alto: 9 }, { x: 66, z: -86, r: 12, alto: 13 }, // sur
  { x: 40, z: 52, r: 10, alto: 7 }, { x: -64, z: 44, r: 9, alto: 12 }, { x: 74, z: -42, r: 10, alto: 8 }, { x: -76, z: -40, r: 10, alto: 9 }, // sueltas
];
export const R_BORDE = 150; // los luchadores no pasan de acá (unidades del juego)
export const SUELO = { etapa: ESCENARIO, radio: R_BORDE, columnas: COLUMNAS };

const R_ISLA = 7.9, Y_LAVA = -0.3; // en unidades de modelo
// El HUD tapa lo que está más de ~8° arriba del horizonte al empezar la ronda: el eclipse y el cristal van bajos.
const ECLIPSE = { ang: 80, elev: 6.2, dist: 38, radio: 2.5 };
const CRISTAL = { x: -3.9, y: 1.45, z: 10.4 };
const BRECHA = 80; // ángulo de la brecha de la caldera, donde se ve el eclipse

// ---------- utilidades ----------
const h2 = (i, j, s) => { // hash de enteros en [0, 1)
  let h = Math.imul(i, 0x27d4eb2d) ^ Math.imul(j, 0x165667b1) ^ Math.imul(s + 7, 0x9e3779b9);
  h = Math.imul(h ^ (h >>> 15), 0x85ebca6b); h ^= h >>> 13; h = Math.imul(h, 0xc2b2ae35);
  return ((h ^ (h >>> 16)) >>> 0) / 4294967296;
};
// Celdas de Worley periódicas (n celdas): distancia al punto más cercano, al segundo y la celda.
function worley(x, y, n, s) {
  const xi = Math.floor(x), yi = Math.floor(y);
  let d1 = 9, d2 = 9, id = 0;
  for (let j = -1; j <= 1; j++) for (let i = -1; i <= 1; i++) {
    const cx = xi + i, cy = yi + j, wx = ((cx % n) + n) % n, wy = ((cy % n) + n) % n;
    const px = cx + 0.1 + 0.8 * h2(wx, wy, s), py = cy + 0.1 + 0.8 * h2(wx, wy, s + 1), d = Math.hypot(x - px, y - py);
    if (d < d1) { d2 = d1; d1 = d; id = wx + wy * n; } else if (d < d2) d2 = d;
  }
  return { d1, d2, id };
}
const gradiente = (stops, t) => {
  if (t <= stops[0][0]) return stops[0][1];
  for (let i = 1; i < stops.length; i++) if (t <= stops[i][0]) return mezclar(stops[i - 1][1], stops[i][1], (t - stops[i - 1][0]) / (stops[i][0] - stops[i - 1][0]));
  return stops[stops.length - 1][1];
};
const LAVA = [[0, hex('#3a0a04')], [0.35, hex('#a3200a')], [0.6, hex('#ff5a12')], [0.8, hex('#ffa62a')], [1, hex('#fff2a8')]];
const difAng = (a, b) => { let d = (a - b) % 360; if (d > 180) d -= 360; if (d < -180) d += 360; return d; };

// Triángulo con la normal hacia `n` (da vuelta el orden si hace falta).
function tri(M, g, a, b, c, n) {
  const p = (i) => M.vs[i].p, cara = cruz(resta(p(b), p(a)), resta(p(c), p(a)));
  if (punto(cara, n) >= 0) g.tris.push(a, b, c); else g.tris.push(a, c, b);
}
// Grilla de vértices f(i, k) (i filas, k columnas) en triángulos con la normal hacia nf(i, k).
function grilla(M, g, ni, nk, f, nf) {
  const ids = [];
  for (let i = 0; i < ni; i++) { ids.push([]); for (let k = 0; k < nk; k++) { const [p, n, uv] = f(i, k); ids[i].push(M.vertice(p, n, uv)); } }
  for (let i = 0; i + 1 < ni; i++) for (let k = 0; k + 1 < nk; k++) {
    const n = nf(i, k);
    tri(M, g, ids[i][k], ids[i][k + 1], ids[i + 1][k + 1], n);
    tri(M, g, ids[i][k], ids[i + 1][k + 1], ids[i + 1][k], n);
  }
  return ids;
}

// ---------- cielo: eclipse total, el anillo de atardecer de los eclipses en todo el horizonte, ceniza y un volcán ----------
const vAElev = (v) => (v >= 0.25 ? -10 + ((1 - v) / 0.75) * 40 : 30 + ((0.25 - v) / 0.25) * 60); // inversa de elevAV
function cielo() {
  const W = 4096, H = 1024, L = new Lienzo(W, H), rnd = azar(311);
  const ed = [Math.cos(ECLIPSE.elev * RAD) * Math.cos(ECLIPSE.ang * RAD), Math.sin(ECLIPSE.elev * RAD), Math.cos(ECLIPSE.elev * RAD) * Math.sin(ECLIPSE.ang * RAD)];
  const VOLCAN = BRECHA + 7; // un volcán lejano en la brecha, debajo del eclipse, con la boca encendida y su pluma
  const cono = (az) => { const d = Math.abs(difAng(az, VOLCAN)); return d < 24 ? 4.3 - Math.max(0, d - 1.6) * 0.19 + 0.12 * perlin(az / 1.5, 0.5, 85, 240) : -99; };
  L.cada((x, y) => {
    const e = vAElev((y + 0.5) / H), az = ((x + 0.5) / W) * 360, th = az * RAD;
    const dir = [Math.cos(e * RAD) * Math.cos(th), Math.sin(e * RAD), Math.cos(e * RAD) * Math.sin(th)];
    const ang = Math.acos(sat(dir[0] * ed[0] + dir[1] * ed[1] + dir[2] * ed[2])) / RAD;
    // en un eclipse total todo el horizonte queda como un atardecer y arriba es de noche
    let c = gradiente([[-10, hex('#140402')], [0, hex('#3d0e05')], [3, hex('#6a2208')], [7, hex('#3a0f0c')], [14, hex('#1f0814')], [30, hex('#0c0512')], [60, hex('#06030a')], [90, hex('#030206')]], e);
    // halo del eclipse: luz fría cerca del disco y rojiza más afuera
    const halo = 0.42 * Math.exp(-((ang / 4.2) ** 2)) + 0.16 * Math.exp(-((ang / 15) ** 2));
    c = c.map((v, k) => v + halo * [0.95, 0.5, 0.3][k] + 0.22 * Math.exp(-((ang / 2.2) ** 2)) * [0.6, 0.7, 0.8][k]);
    // ceniza: bandas oscuras, con el borde de abajo encendido por la lava
    const n = fbm(x / 512, y / 150, 5, 77, W / 512, 0, 0.55) * 0.75 + 0.35 * perlin(x / 1024, y / 400, 79, W / 1024);
    const d = sat(suave(-0.05, 0.45, n) * suave(2, 9, e) * (1 - 0.7 * suave(25, 55, e)));
    if (d > 0.001) {
      const brillo = Math.exp(-Math.max(e - 1, 0) / 6) * 0.75 + halo * 0.8;
      c = mezclar(c, mezclar(hex('#140806'), hex('#3a120a'), sat(1 - d)).map((v, k) => v + brillo * [0.55, 0.16, 0.05][k]), sat(d * 1.1));
    }
    // volcán lejano: silueta oscura, la boca encendida y ríos de lava que bajan
    const alto = cono(az), cima = 4.3;
    if (e < alto) {
      const da = difAng(az, VOLCAN), boca = Math.exp(-((da / 1.4) ** 2)) * suave(alto - 0.6, alto, e);
      c = mezclar(hex('#0b0404'), hex('#1c0907'), sat(0.5 + da / 30) * 0.7).map((v, k) => v + boca * [1.3, 0.6, 0.15][k] + 0.1 * suave(0, cima, e) * [0.5, 0.12, 0.03][k]);
      for (let r = 0; r < 4; r++) { // ríos de lava por la ladera
        const dr = difAng(az, VOLCAN + (r - 1.5) * 2.6 + 0.9 * perlin(e * 1.5, r, 83) + (r - 1.5) * (cima - e) * 0.9);
        c = c.map((v, k) => v + 0.85 * Math.exp(-((dr / 0.22) ** 2)) * suave(0.2, 1.4, alto - e) * [1, 0.42, 0.08][k]);
      }
    }
    // la pluma sube desde la boca y se tuerce a la izquierda, lejos del eclipse
    const sube = e - cima, pluma = sube > -0.5 ? Math.exp(-((difAng(az, VOLCAN + 1.2 * Math.max(sube, 0) + 0.02 * sube * sube) / (1 + 0.45 * Math.max(sube, 0))) ** 2)) * suave(-0.5, 0.8, sube) * (1 - suave(14, 30, sube)) * (0.55 + 0.45 * fbm(x / 24, y / 24, 4, 91)) : 0;
    if (pluma > 0.01) c = mezclar(c, hex('#150908').map((v, k) => v + Math.exp(-Math.max(sube, 0) / 2.5) * [0.8, 0.28, 0.06][k]), sat(pluma * 1.2));
    return [...c, 1];
  });
  for (let i = 0; i < 1400; i++) { // estrellas: el eclipse oscurece el cielo
    const x = rnd() * W, y = rnd() * H * 0.42, e = vAElev(y / H);
    if (e < 28) continue;
    const b = suave(28, 50, e) * (0.15 + 0.85 * rnd() ** 4);
    trazo(L, x, y, 0.45 + rnd() * 0.5, x, y, 0.45, mezclar([1, 0.8, 0.7], [0.8, 0.85, 1], rnd()), b);
  }
  textura('tex_f126', L.recorte(0, 0, 2048, 1024));
  textura('tex_f127', L.recorte(2048, 0, 2048, 1024));
}

// El disco negro con la corona: rayos, un anillo blanco, protuberancias rojas y el "anillo de diamante".
function eclipse() {
  const N = 512, L = new Lienzo(N, N), c0 = N / 2, rd = N * 0.13;
  L.cada((x, y) => {
    const dx = (x + 0.5 - c0) / rd, dy = (y + 0.5 - c0) / rd, r = Math.hypot(dx, dy), a = Math.atan2(dy, dx);
    if (r < 1) return [0.012, 0.006, 0.01, 1];
    const f = r - 1, au = a / (2 * Math.PI) + 0.5; // ángulo de 0 a 1, para ruido que empalma
    const rayos = 0.55 + 0.45 * fbm(au * 20, f * 1.3, 4, 501, 20, 0) + 0.25 * Math.max(0, Math.sin(a * 9 + 2 * perlin(au * 12, 0, 503, 12)));
    const lobos = Math.max(Math.abs(Math.cos(a - 0.35)), 0.55 * Math.abs(Math.sin(a * 2 + 0.6))) ** 3; // serpentinas largas
    let k = 1.9 * Math.exp(-f / 0.05) + 1.0 * Math.exp(-f / (0.32 + 0.35 * rayos)) + 0.75 * (0.25 + 0.75 * lobos) * rayos * Math.exp(-f / 0.85);
    k *= sat(Math.min(c0 - Math.abs(x + 0.5 - c0), c0 - Math.abs(y + 0.5 - c0)) / (N * 0.08)); // a cero antes del borde del quad
    const diamante = 3 * Math.exp(-((Math.hypot(dx - Math.cos(-0.75), dy - Math.sin(-0.75)) / 0.14) ** 2));
    let col = mezclar(mezclar(hex('#ff5a2a'), hex('#ffc890'), sat(Math.exp(-f / 0.6))), hex('#fffaf0'), sat(Math.exp(-f / 0.12) + diamante));
    const prot = Math.max(0, Math.sin(a * 5 + 1.3)) ** 6 * Math.exp(-(((f - 0.05) / 0.04) ** 2)) * 1.5;
    col = mezclar(col, hex('#ff2a3a'), sat(prot));
    return [...col, sat(k + diamante + prot * 0.6)];
  });
  textura('tex_f131', L);
}
function eclipseQuad() {
  const M = new Malla(), g = M.material('tex_f131.bmt'), l = ECLIPSE;
  const d = [Math.cos(l.elev * RAD) * Math.cos(l.ang * RAD), Math.sin(l.elev * RAD), Math.cos(l.elev * RAD) * Math.sin(l.ang * RAD)];
  const c = escalar(d, l.dist), der = unit(cruz([0, 1, 0], d)), arr = cruz(d, der), s = (l.dist * Math.tan(l.radio * RAD)) / 0.26;
  const P = (u, v) => suma(c, suma(escalar(der, (u - 0.5) * 2 * s), escalar(arr, (0.5 - v) * 2 * s)));
  M.cara(g, [P(0, 0), P(1, 0), P(1, 1), P(0, 1)], [[0, 0], [1, 0], [1, 1], [0, 1]]);
  return M;
}

// ---------- piedra ----------
// Lajas de basalto con vetas de lava (piso de la isla, se repite cada 1.6).
function lajas() {
  const N = 512, L = new Lienzo(N, N), C = 7;
  L.cada((x, y) => {
    const u = (x + 0.5) / N * C, v = (y + 0.5) / N * C, w = worley(u, v, C, 11), b = w.d2 - w.d1, t = h2(w.id, 0, 13);
    const det = fbm(x / 16, y / 16, 4, 15, N / 16, N / 16) * 0.5 + 0.5, gr = fbm(x / 4, y / 4, 2, 17, N / 4, N / 4) * 0.5 + 0.5;
    const tono = h2(w.id, 3, 14) < 0.22 ? hex('#4a2a22') : mezclar(hex('#221d1e'), hex('#433834'), t);
    let c = tono.map((k) => k * (0.75 + 0.25 * det) * (0.78 + 0.3 * gr));
    c = mezclar(c, hex('#0b0909'), (1 - suave(0.02, 0.07, b)) * 0.9); // juntas
    const vena = t > 0.45 ? Math.exp(-b / 0.022) * suave(0.35, 0.65, fbm(u * 2, v * 2, 3, 19, C * 2, C * 2) * 0.5 + 0.5) : 0;
    c = c.map((k, i) => k + vena * [1.0, 0.36, 0.08][i] * 1.2);
    const ceniza = suave(0.55, 0.85, fbm(x / 64, y / 64, 3, 21, N / 64, N / 64) * 0.5 + 0.5) * (1 - suave(0.04, 0.2, b) * 0.6);
    return [...mezclar(c, hex('#5a514c'), ceniza * 0.35 * (1 - vena)), 1];
  });
  textura('tex_f122', L);
}
// Costados de las columnas: basalto columnar con juntas, más caliente abajo (v = 1 es el piso, 1 de alto).
function basalto() {
  const N = 512, L = new Lienzo(N, N);
  L.cada((x, y) => {
    const u = (x + 0.5) / N, v = (y + 0.5) / N, n = fbm(x / 8, y / 32, 4, 31, N / 8, N / 32) * 0.5 + 0.5, gr = fbm(x / 4, y / 4, 2, 33, N / 4, N / 4) * 0.5 + 0.5;
    const cara = Math.floor(u * 3), alt = v * 5 + 0.7 * h2(cara, 0, 36) + 0.06 * perlin(u * 8, 0, 35, 8); // juntas casi rectas, otra altura en cada cara
    const fila = Math.floor(alt), junta = Math.abs((alt % 1) - 0.5), vert = Math.abs(((u * 3) % 1) - 0.5);
    let c = mezclar(hex('#2f2827'), hex('#5c4b46'), n * 0.65 + 0.35 * h2(fila, cara, 37)).map((k) => k * (0.8 + 0.3 * gr) * (0.85 + 0.15 * suave(0.5, 0.3, vert)));
    c = mezclar(c, hex('#0a0808'), suave(0.42, 0.49, junta) * 0.85);
    const calor = suave(0.55, 1, v), grieta = suave(0.47, 0.5, junta) * calor + Math.exp(-Math.abs(perlin(x / 32, y / 16, 39, N / 32, N / 16)) / 0.05) * calor * 0.8;
    c = c.map((k, i) => k * (1 - 0.3 * calor) + grieta * [1, 0.35, 0.07][i] * 0.9 + calor * calor * [0.22, 0.05, 0.01][i]);
    return [...c, 1];
  });
  textura('tex_f120', L);
}
// Tapa de las columnas (radial): basalto partido y un anillo de runas encendidas cerca del borde.
function tapa() {
  const N = 512, L = new Lienzo(N, N), c0 = N / 2;
  L.cada((x, y) => {
    const dx = (x + 0.5 - c0) / c0, dy = (y + 0.5 - c0) / c0, r = Math.hypot(dx, dy), a = Math.atan2(dy, dx);
    const w = worley(dx * 3 + 3, dy * 3 + 3, 64, 41), b = w.d2 - w.d1, gr = fbm(x / 3, y / 3, 2, 43) * 0.5 + 0.5;
    let c = mezclar(hex('#30292a'), hex('#4f4340'), h2(w.id, 1, 45)).map((k) => k * (0.8 + 0.3 * gr) * (0.85 + 0.15 * r));
    c = mezclar(c, hex('#0c0909'), (1 - suave(0.02, 0.06, b)) * 0.8);
    const anillo = Math.exp(-(((r - 0.8) / 0.012) ** 2)) + Math.exp(-(((r - 0.9) / 0.01) ** 2));
    const runa = r > 0.82 && r < 0.88 && Math.sin(a * 24) > -0.2 && h2(Math.floor((a + Math.PI) * 24 / (2 * Math.PI)), Math.floor(r * 60), 47) > 0.45 ? 0.9 : 0;
    const k = sat(anillo * 0.8 + runa * 0.7) * (0.6 + 0.4 * gr);
    c = c.map((v, i) => v + k * [1, 0.48, 0.12][i]);
    return [...c, 1];
  });
  textura('tex_f121', L);
}
// Roca de la caldera (u alrededor cada 6, v de arriba (0) al lago (1) en 10 de alto), con la luz de la lava abajo.
function rocaTex() {
  const W = 512, H = 1024, L = new Lienzo(W, H);
  L.cada((x, y) => {
    const v = (y + 0.5) / H, n = fbm(x / 32, y / 20, 5, 51, W / 32, 0) * 0.5 + 0.5, ar = Math.abs(fbm(x / 16, y / 48, 4, 53, W / 16, 0));
    const estrato = 0.5 + 0.5 * Math.sin(y / 7 + 4 * fbm(x / 64, y / 64, 3, 52, W / 64, 0));
    let c = mezclar(hex('#120c0b'), hex('#43322b'), n * 0.75 + estrato * 0.25).map((k) => k * (0.6 + 0.6 * suave(0.02, 0.3, ar)));
    const luz = suave(0.5, 1, v) ** 1.5; // la lava de abajo
    c = c.map((k, i) => k * (1 - 0.15 * luz) + luz * [0.62, 0.18, 0.05][i] * (0.5 + 0.5 * n));
    const w = worley(x / 64, y / 64, 8, 54), grieta = Math.exp(-(w.d2 - w.d1) / 0.025) * suave(0.62, 0.78, fbm(x / 64, y / 64, 3, 56, W / 64, 0) * 0.5 + 0.5) * suave(0.35, 0.9, v);
    return [...c.map((k, i) => k + grieta * [1, 0.34, 0.06][i]), 1];
  });
  textura('tex_f125', L);
}

// ---------- lava ----------
// Placas de costra que flotan sobre lava encendida (se repite; la uv corre con d3d/animado.c).
function lavaTex() {
  const N = 512, L = new Lienzo(N, N), C = 6;
  L.cada((x, y) => {
    const u = (x + 0.5) / N * C, v = (y + 0.5) / N * C;
    const w = worley(u + 0.25 * fbm(u, v, 3, 61, C, C), v + 0.25 * fbm(u + 5, v, 3, 63, C, C), C, 65), b = w.d2 - w.d1;
    const det = fbm(x / 16, y / 16, 4, 67, N / 16, N / 16) * 0.5 + 0.5, t = h2(w.id, 2, 69);
    const canal = Math.exp(-b / (0.07 + 0.05 * t)), placa = 1 - canal;
    let calor = sat(canal * 1.1 + 0.25 * det * placa * (1 - suave(0.1, 0.4, b)) + 0.1 * t * placa);
    const c = gradiente(LAVA, calor).map((k, i) => k * (placa > 0.85 ? 0.5 + 0.5 * det : 1));
    return [...c, 1];
  });
  textura('tex_f124', L);
}
// Cascada: chorros que bajan (v repetida; la uv corre rápido), con alfa para la mezcla aditiva.
function cascadaTex() {
  const W = 128, H = 512, L = new Lienzo(W, H);
  L.cada((x, y) => {
    const u = (x + 0.5) / W, chorro = Math.exp(-(((u - 0.5) / 0.32) ** 4)), n = fbm(x / 8, y / 64, 4, 71, W / 8, H / 64) * 0.5 + 0.5;
    const costra = suave(0.58, 0.72, fbm(x / 4, y / 32, 3, 73, W / 4, H / 32) * 0.5 + 0.5), k = sat(chorro * (0.35 + 0.95 * n) * (1 - 0.65 * costra));
    return [...gradiente(LAVA, 0.2 + 0.8 * k), sat(chorro * (0.6 + 0.4 * n))];
  });
  textura('tex_f132', L);
}
// Resplandor redondo (aditivo): halo del cristal, del lago al pie de la isla y de las cascadas.
function resplandorTex() {
  const N = 128, L = new Lienzo(N, N);
  L.cada((x, y) => { const r = Math.hypot(x + 0.5 - N / 2, y + 0.5 - N / 2) / (N / 2); const k = Math.exp(-r * r * 3.5) * (1 - suave(0.8, 1, r)); return [...mezclar(hex('#ff3a0a'), hex('#ffb04a'), k), k]; });
  textura('tex_f129', L);
  const B = new Lienzo(64, 64); // brasa: punto amarillo con halo
  B.cada((x, y) => { const r = Math.hypot(x + 0.5 - 32, y + 0.5 - 32) / 32; const k = Math.exp(-r * r * 9) + 0.35 * Math.exp(-r * r * 2.5) * (1 - suave(0.8, 1, r)); return [...mezclar(hex('#ff6a1a'), hex('#fff0a0'), sat(k)), sat(k)]; });
  textura('tex_f130', B);
}
// Cristal: bandas de las caras (u de borde a borde de cada cara, v a lo largo), núcleo blanco y bordes rojos.
function cristalTex() {
  const N = 256, L = new Lienzo(N, N);
  L.cada((x, y) => {
    const u = (x + 0.5) / N, v = (y + 0.5) / N, borde = Math.abs(u - 0.5) * 2, n = fbm(x / 16, y / 30, 3, 81) * 0.5 + 0.5;
    const nucleo = Math.exp(-(((v - 0.5) / 0.32) ** 2)) * (1 - 0.6 * borde), filo = suave(0.85, 1, borde);
    const k = sat(0.2 + 0.7 * nucleo + 0.45 * filo + 0.15 * n);
    return [...gradiente([[0, hex('#3a030a')], [0.35, hex('#a8141a')], [0.62, hex('#ff5a1a')], [0.85, hex('#ffc070')], [1, hex('#fff6d8')]], k), 1];
  });
  textura('tex_f128', L);
}
// Sello del eclipse en el centro de la isla (calcomanía aditiva de 1024: anillos, runas, rayos y el eclipse).
function selloTex() {
  const N = 1024, L = new Lienzo(N, N, [0, 0, 0, 0]), c0 = N / 2, R = N / 2 - 4, rnd = azar(97);
  const COL = hex('#ff9a3a'), anillo = (r, w, a = 1) => { for (let i = 0; i < 720; i++) { const t0 = (i / 720) * 2 * Math.PI, t1 = ((i + 1) / 720) * 2 * Math.PI; trazo(L, c0 + Math.cos(t0) * r, c0 + Math.sin(t0) * r, w, c0 + Math.cos(t1) * r, c0 + Math.sin(t1) * r, w, COL, a); } };
  anillo(R * 0.98, 2.5); anillo(R * 0.93, 5); anillo(R * 0.79, 3); anillo(R * 0.7, 2); anillo(R * 0.38, 3);
  for (let i = 0; i < 48; i++) { // runas entre 0.8 y 0.92
    const a = (i / 48) * 2 * Math.PI, r0 = R * 0.81, r1 = R * 0.91, P = (rr, da) => [c0 + Math.cos(a + da) * rr, c0 + Math.sin(a + da) * rr];
    for (let s = 0; s < 3; s++) {
      const [x0, y0] = P(r0 + rnd() * (r1 - r0), (rnd() - 0.5) * 0.08), [x1, y1] = P(r0 + rnd() * (r1 - r0), (rnd() - 0.5) * 0.08);
      trazo(L, x0, y0, 2.2, x1, y1, 2.2, COL, 0.9);
    }
  }
  for (let i = 0; i < 12; i++) { // rayos con rombos
    const a = (i / 12) * 2 * Math.PI + Math.PI / 12, P = (rr) => [c0 + Math.cos(a) * rr, c0 + Math.sin(a) * rr];
    const [x0, y0] = P(R * 0.4), [x1, y1] = P(R * 0.68);
    trazo(L, x0, y0, 3.5, x1, y1, 1, COL, 0.9);
    const [mx, my] = P(R * 0.55), q = 14, ca = Math.cos(a), sa = Math.sin(a);
    poligono(L, [[mx + ca * q * 1.6, my + sa * q * 1.6], [mx - sa * q, my + ca * q], [mx - ca * q * 1.6, my - sa * q * 1.6], [mx + sa * q, my - ca * q]], COL, 0.8);
  }
  for (let i = 0; i < 12; i++) { // estrella de 12 puntas
    const a0 = (i / 12) * 2 * Math.PI, a1 = ((i + 5) / 12) * 2 * Math.PI;
    trazo(L, c0 + Math.cos(a0) * R * 0.68, c0 + Math.sin(a0) * R * 0.68, 1.8, c0 + Math.cos(a1) * R * 0.68, c0 + Math.sin(a1) * R * 0.68, 1.8, COL, 0.55);
  }
  // el eclipse: disco oscuro (sin luz), corona y una media luna corrida
  for (let y = 0; y < N; y++) for (let x = 0; x < N; x++) {
    const r = Math.hypot(x + 0.5 - c0, y + 0.5 - c0) / R;
    if (r < 0.33) { const k = Math.exp(-(((r - 0.3) / 0.025) ** 2)) * 1.2 + 0.25 * Math.exp(-(((r - 0.33) / 0.06) ** 2)); L.sobre(x, y, hex('#ffd27a'), sat(k)); }
  }
  desenfocar(L, 1.5);
  // gastado: se apaga por partes
  for (let y = 0; y < N; y++) for (let x = 0; x < N; x++) { const i = 4 * (y * N + x); L.d[i + 3] *= 0.45 + 0.55 * suave(0.2, 0.6, fbm(x / 40, y / 40, 4, 99) * 0.5 + 0.5); }
  textura('tex_f123', L);
}

// ---------- modelos ----------
const radioIsla = (th) => R_ISLA + 0.18 * perlin(th * 6 / (2 * Math.PI), 0.5, 101, 6) + 0.08 * perlin(th * 23 / (2 * Math.PI), 0.5, 103, 23);
// Isla: piso plano hasta el borde y una barranca irregular hasta la lava.
function piso() {
  const M = new Malla(), g = M.material('tex_f122.bmt', [1, 1, 1, 1], [0, 0, 0, 0, 0, 0, 0, 0, 0]), gr = M.material('tex_f125.bmt', [1, 1, 1, 1], [0, 0, 0, 0, 0, 0, 0, 0, 0]);
  const LADOS = 128, anillos = [0, 0.6, 1.3, 2.1, 3, 4, 5, 6, 6.8, 7.4, 1], T = 1.6; // el último: el borde (relativo)
  grilla(M, g, anillos.length, LADOS + 1, (i, k) => {
    const th = (k / LADOS) * 2 * Math.PI, r = i === anillos.length - 1 ? radioIsla(th) : Math.min(anillos[i], radioIsla(th) - 0.15);
    const p = [r * Math.cos(th), 0, r * Math.sin(th)];
    return [p, [0, 1, 0], [p[0] / T, p[2] / T]];
  }, () => [0, 1, 0]);
  // barranca: del borde a la lava, con salientes
  const perfil = [[0, 0], [0.06, -0.05], [0.1, -0.16], [0.2, -0.22], [0.32, Y_LAVA - 0.08]];
  grilla(M, gr, perfil.length, LADOS + 1, (i, k) => {
    const th = (k / LADOS) * 2 * Math.PI, q = k / LADOS, r = radioIsla(th) + perfil[i][0] * (1 + 0.5 * perlin(q * 48, i, 107, 48)), y = perfil[i][1] * (1 + 0.3 * perlin(q * 64, i, 109, 64));
    const p = [r * Math.cos(th), y, r * Math.sin(th)];
    return [p, unit([Math.cos(th), 0.4, Math.sin(th)]), [(th * R_ISLA) / 6, 0.9 - y * 0.2]];
  }, (i, k) => { const th = ((k + 0.5) / LADOS) * 2 * Math.PI; return [Math.cos(th), 0.3, Math.sin(th)]; });
  return M;
}

// Columnas: prismas de basalto de seis caras con la arista de arriba biselada.
function columnas() {
  const M = new Malla(), SIN = [0, 0, 0, 0, 0, 0, 0, 0, 0], gs = M.material('tex_f120.bmt', [1, 1, 1, 1], SIN), gt = M.material('tex_f121.bmt', [1, 1, 1, 1], SIN);
  const rnd = azar(131);
  for (const c of COLUMNAS) {
    const cx = c.x / E, cz = c.z / E, R = (1.05 * c.r) / E, top = c.alto / E, giro = rnd() * Math.PI, bis = 0.025;
    const P = (k, rr, y) => { const a = giro + (k / 6) * 2 * Math.PI; return [cx + rr * Math.cos(a), y, cz + rr * Math.sin(a)]; };
    let u = 0;
    for (let k = 0; k < 6; k++) {
      const a0 = P(k, R, -0.05), a1 = P(k + 1, R, -0.05), b0 = P(k, R, top - bis), b1 = P(k + 1, R, top - bis), c0 = P(k, R - bis, top), c1 = P(k + 1, R - bis, top);
      const mid = giro + ((k + 0.5) / 6) * 2 * Math.PI, n = [Math.cos(mid), 0, Math.sin(mid)], lado = Math.hypot(a1[0] - a0[0], a1[2] - a0[2]);
      const ids = [a0, a1, b1, b0].map((p, j) => M.vertice(p, n, [u + (j === 1 || j === 2 ? lado : 0), 1 - p[1]]));
      tri(M, gs, ids[0], ids[1], ids[2], n); tri(M, gs, ids[0], ids[2], ids[3], n);
      const nb = unit([n[0], 1, n[2]]), jb = [b0, b1, c1, c0].map((p, j) => M.vertice(p, nb, [u + (j === 1 || j === 2 ? lado : 0), 1 - p[1] - (j >= 2 ? 0.02 : 0)]));
      tri(M, gs, jb[0], jb[1], jb[2], nb); tri(M, gs, jb[0], jb[2], jb[3], nb);
      u += lado;
    }
    const centro = M.vertice([cx, top, cz], [0, 1, 0], [0.5, 0.5]), borde = [];
    for (let k = 0; k <= 6; k++) { const p = P(k, R - bis, top), a = giro + (k / 6) * 2 * Math.PI; borde.push(M.vertice(p, [0, 1, 0], [0.5 + 0.5 * Math.cos(a), 0.5 + 0.5 * Math.sin(a)])); }
    for (let k = 0; k < 6; k++) tri(M, gt, centro, borde[k], borde[k + 1], [0, 1, 0]);
    for (let i = 0; i < 5; i++) { // cascotes al pie (bajos: se pisan)
      const a = rnd() * 2 * Math.PI, d = R + 0.04 + rnd() * 0.15, s = 0.025 + rnd() * 0.035, x = cx + d * Math.cos(a), z = cz + d * Math.sin(a);
      roca(M, gs, x, 0, z, s, rnd);
    }
  }
  return M;
}
// Piedra suelta: octaedro deformado.
function roca(M, g, x, y, z, s, rnd, alto = 0.7) {
  const P = [[1, 0, 0], [-1, 0, 0], [0, 1, 0], [0, -0.3, 0], [0, 0, 1], [0, 0, -1]].map(([a, b, c]) => [x + a * s * (0.7 + 0.6 * rnd()), y + b * s * alto * (0.7 + 0.6 * rnd()) + s * 0.2, z + c * s * (0.7 + 0.6 * rnd())]);
  for (const [a, b, c] of [[0, 4, 2], [4, 1, 2], [1, 5, 2], [5, 0, 2], [4, 0, 3], [1, 4, 3], [5, 1, 3], [0, 5, 3]]) {
    const n = unit(cruz(resta(P[b], P[a]), resta(P[c], P[a]))), o = unit(resta(suma(suma(P[a], P[b]), P[c]).map((v) => v / 3), [x, y, z]));
    const ids = [P[a], P[b], P[c]].map((p, j) => M.vertice(p, punto(n, o) >= 0 ? n : escalar(n, -1), [j * 0.3 + rnd() * 0.1, 0.55 + j * 0.1]));
    tri(M, g, ids[0], ids[1], ids[2], o);
  }
}

// Lago de lava: anillo desde abajo de la isla hasta las paredes de la caldera (corre despacio).
function lago() {
  const M = new Malla(), g = M.material('tex_f124.bmt'), radios = [7.4, 8.2, 9.5, 11.5, 14, 17.5, 21.5, 25, 28], LADOS = 96, T = 2.5;
  grilla(M, g, radios.length, LADOS + 1, (i, k) => {
    const th = (k / LADOS) * 2 * Math.PI, p = [radios[i] * Math.cos(th), Y_LAVA, radios[i] * Math.sin(th)];
    return [p, [0, 1, 0], [p[0] / T, p[2] / T]];
  }, () => [0, 1, 0]);
  const n = M.aNmd('');
  const malla = { vs: n.vs, is: n.is, huesos: n.vs.map(() => 0) };
  return animado('mod_w122', [{ x: 0, z: 0, rumbo: 0, escala: 1, tipo: 3, fase: 0, vel: 0.018, radio: 0.007 }], malla, [[0, 0, 0, -1]],
    (cant) => [{ nombre: '', col: [1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0], tex: 'tex_f124.bmt', ini: 0, n: cant }]);
}

// Paredes de la caldera: alto por ángulo, con una brecha baja hacia el eclipse y dos picos que la enmarcan.
const altoCaldera = (az) => {
  const b = Math.exp(-((difAng(az, BRECHA) / 16) ** 2)), picos = Math.exp(-((difAng(az, BRECHA - 30) / 9) ** 2)) * 3.5 + Math.exp(-((difAng(az, BRECHA + 26) / 10) ** 2)) * 4.2;
  return (4.2 + 2.2 * (fbm(az / 40, 0.5, 4, 111, 9) * 0.5 + 0.5) + picos) * (1 - 0.78 * b) + 0.35 * perlin(az / 3, 0.5, 113, 120);
};
const CASCADAS = [[BRECHA - 52, 0.9], [BRECHA - 22, 1.2], [BRECHA + 44, 1], [BRECHA + 120, 0.8], [BRECHA + 175, 1.1], [BRECHA + 236, 0.9]]; // [ángulo, ancho]
function caldera() {
  const M = new Malla(), g = M.material('tex_f125.bmt', [1, 1, 1, 1], [0, 0, 0, 0, 0, 0, 0, 0, 0]), LADOS = 180, R0 = 26;
  // de la lava a la cresta (tirando hacia afuera) y de ahí hacia atrás; v sigue la altura
  const filas = [0, 0.15, 0.35, 0.55, 0.72, 0.86, 0.95, 1];
  grilla(M, g, filas.length, LADOS + 1, (i, k) => {
    const az = (k / LADOS) * 360, th = az * RAD, h = altoCaldera(az), t = filas[i];
    const r = R0 + 0.5 * perlin(az / 7.2, t * 3, 115, 50) + t * 2.2 + 0.6 * perlin(az / 4, t * 5, 117, 90) * t;
    const y = Y_LAVA - 0.2 + (h - Y_LAVA + 0.2) * t ** 0.85;
    return [[r * Math.cos(th), y, r * Math.sin(th)], unit([-Math.cos(th), 0.25, -Math.sin(th)]), [(th * R0) / 6, 1 - (y + 0.5) / 10]];
  }, (i, k) => { const th = ((k + 0.5) / LADOS) * 360 * RAD; return [-Math.cos(th), 0.2, -Math.sin(th)]; });
  return M;
}
// Cascadas de lava por la pared (aditivas, la uv corre hacia abajo) y su resplandor en el lago.
function cascadas() {
  const M = new Malla(), g = M.material('tex_f132.bmt'), R0 = 26;
  for (const [az, ancho] of CASCADAS) {
    const h = altoCaldera(az), filas = 10, th = az * RAD;
    for (let lado = -1; lado <= 1; lado += 2) { // dos tiras un poco cruzadas para que tenga cuerpo
      const ids = [];
      for (let i = 0; i <= filas; i++) {
        const t = i / filas, y = h - 0.1 - (h - Y_LAVA) * t, r = R0 - 0.35 + t * 2.0 * 0.95 - 0.4 * t * t, w = 2.2 * ancho * (0.55 + 0.45 * t) / 2;
        const ct = [r * Math.cos(th), y, r * Math.sin(th)], tg = [-Math.sin(th + lado * 0.25), 0, Math.cos(th + lado * 0.25)];
        ids.push([M.vertice(suma(ct, escalar(tg, -w)), [0, 1, 0], [0, t * 3]), M.vertice(suma(ct, escalar(tg, w)), [0, 1, 0], [1, t * 3])]);
      }
      const n = [-Math.cos(th), 0, -Math.sin(th)];
      for (let i = 0; i < filas; i++) { tri(M, g, ids[i][0], ids[i][1], ids[i + 1][1], n); tri(M, g, ids[i][0], ids[i + 1][1], ids[i + 1][0], n); }
    }
  }
  const n = M.aNmd('');
  return animado('mod_w124', [{ x: 0, z: 0, rumbo: 0, escala: 1, tipo: 3, fase: 0, vel: -0.9, radio: 0 }], { vs: n.vs, is: n.is, huesos: n.vs.map(() => 0) }, [[0, 0, 0, -1]],
    (cant) => [{ nombre: '', col: [1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0], tex: 'tex_f132.bmt', ini: 0, n: cant }]);
}
// Resplandores aditivos quietos: el lago al pie de la isla, al pie de cada cascada y la luz del sello.
function resplandores() {
  const M = new Malla(), g = M.material('tex_f129.bmt'), LADOS = 96;
  grilla(M, g, 2, LADOS + 1, (i, k) => { // anillo: u de 0.5 (centro del halo) a 1 (borde)
    const th = (k / LADOS) * 2 * Math.PI, r = i ? 10.2 : radioIsla(th) + 0.25;
    return [[r * Math.cos(th), Y_LAVA + 0.02, r * Math.sin(th)], [0, 1, 0], [0.5 + 0.5 * i, 0.5]];
  }, () => [0, 1, 0]);
  for (const [az, ancho] of CASCADAS) {
    const th = az * RAD, r = 27.4, c = [r * Math.cos(th), Y_LAVA + 0.03, r * Math.sin(th)], s = 3.2 * ancho;
    M.cara(g, [[c[0] - s, c[1], c[2] - s], [c[0] - s, c[1], c[2] + s], [c[0] + s, c[1], c[2] + s], [c[0] + s, c[1], c[2] - s]], [[0, 0], [0, 1], [1, 1], [1, 0]]);
  }
  return M;
}
// Sello en el centro, apenas arriba del piso.
function sello() {
  const M = new Malla(), g = M.material('tex_f123.bmt', [1, 1, 1, 0.8]), s = 2.3, y = 0.004;
  M.cara(g, [[-s, y, -s], [-s, y, s], [s, y, s], [s, y, -s]], [[0, 1], [0, 0], [1, 0], [1, 1]]);
  return M;
}

// Borde de la isla: pilares rotos con braseros entre las rocas de la orilla (afuera de donde llegan los luchadores).
const BRASEROS = [20, 65, 110, 155, 200, 245, 290, 335];
function borde(fuegos) {
  const M = new Malla(), SIN = [0, 0, 0, 0, 0, 0, 0, 0, 0], gs = M.material('tex_f120.bmt', [1, 1, 1, 1], SIN), gr = M.material('tex_f125.bmt', [1, 1, 1, 1], SIN), rnd = azar(151);
  const prisma = (x, z, rr, y0, y1, lados, giro) => {
    let u = 0;
    for (let k = 0; k < lados; k++) {
      const a0 = giro + (k / lados) * 2 * Math.PI, a1 = giro + ((k + 1) / lados) * 2 * Math.PI, n = [Math.cos((a0 + a1) / 2), 0, Math.sin((a0 + a1) / 2)];
      const P = (a, y) => [x + rr * Math.cos(a), y, z + rr * Math.sin(a)], lado = 2 * rr * Math.sin(Math.PI / lados);
      const ids = [P(a0, y0), P(a1, y0), P(a1, y1), P(a0, y1)].map((p, j) => M.vertice(p, n, [u + (j === 1 || j === 2 ? lado : 0), 1 - p[1] * 0.6]));
      tri(M, gs, ids[0], ids[1], ids[2], n); tri(M, gs, ids[0], ids[2], ids[3], n);
      u += lado;
    }
    const c = M.vertice([x, y1, z], [0, 1, 0], [0.5, 0.05]), ids = [];
    for (let k = 0; k <= lados; k++) { const a = giro + (k / lados) * 2 * Math.PI; ids.push(M.vertice([x + rr * Math.cos(a), y1, z + rr * Math.sin(a)], [0, 1, 0], [0.5 + 0.3 * Math.cos(a), 0.05])); }
    for (let k = 0; k < lados; k++) tri(M, gs, c, ids[k], ids[k + 1], [0, 1, 0]);
  };
  for (const az of BRASEROS) { // brasero: pilar bajo con un cuenco ancho; el fuego arriba
    const th = az * RAD, r = radioIsla(th) + 0.12, x = r * Math.cos(th), z = r * Math.sin(th), g0 = rnd();
    prisma(x, z, 0.16, Y_LAVA, 0.62, 8, g0);
    prisma(x, z, 0.3, 0.62, 0.7, 8, g0);
    fuegos.push({ x, z, alto: 0.72 });
  }
  // obelisco: cuatro caras que se angostan y una punta
  const obelisco = (x, z, b, alto, giro) => {
    const P = (k, f, y) => { const a = giro + (k / 4) * 2 * Math.PI; return [x + b * f * Math.cos(a), y, z + b * f * Math.sin(a)]; }, punta = [x, alto + b * 0.9, z];
    for (let k = 0; k < 4; k++) {
      const a = P(k, 1, Y_LAVA), bb = P(k + 1, 1, Y_LAVA), c = P(k + 1, 0.6, alto), d = P(k, 0.6, alto), mid = giro + ((k + 0.5) / 4) * 2 * Math.PI;
      const n = unit([Math.cos(mid), 0.1, Math.sin(mid)]), ids = [a, bb, c, d].map((p, j) => M.vertice(p, n, [j === 1 || j === 2 ? 0.55 : 0, 1 - (p[1] - Y_LAVA) * 0.3]));
      tri(M, gs, ids[0], ids[1], ids[2], n); tri(M, gs, ids[0], ids[2], ids[3], n);
      const np = unit([Math.cos(mid), 0.7, Math.sin(mid)]), t = [d, c, punta].map((p, j) => M.vertice(p, np, [j === 2 ? 0.27 : j * 0.55, 0.12 + (j === 2 ? -0.1 : 0)]));
      tri(M, gs, t[0], t[1], t[2], np);
    }
  };
  // pilar partido: la tapa cortada en diagonal y mellada
  const partido = (x, z, rr, alto, giro) => {
    const lados = 8, inc = rr * 1.4, top = (a) => alto + inc * Math.cos(a - giro) + 0.04 * Math.sin(a * 5 + giro * 3);
    let u = 0;
    const arriba = [];
    for (let k = 0; k < lados; k++) {
      const a0 = (k / lados) * 2 * Math.PI, a1 = ((k + 1) / lados) * 2 * Math.PI, n = [Math.cos((a0 + a1) / 2), 0, Math.sin((a0 + a1) / 2)], lado = 2 * rr * Math.sin(Math.PI / lados);
      const P = (a, y) => [x + rr * Math.cos(a), y, z + rr * Math.sin(a)], ids = [P(a0, Y_LAVA), P(a1, Y_LAVA), P(a1, top(a1)), P(a0, top(a0))].map((p, j) => M.vertice(p, n, [u + (j === 1 || j === 2 ? lado : 0), 1 - (p[1] - Y_LAVA) * 0.35]));
      tri(M, gs, ids[0], ids[1], ids[2], n); tri(M, gs, ids[0], ids[2], ids[3], n);
      arriba.push(P(a0, top(a0)));
      u += lado;
    }
    const cy = arriba.reduce((t, p) => t + p[1], 0) / lados, c = M.vertice([x, cy, z], [0, 1, 0], [0.5, 0.1]);
    for (let k = 0; k < lados; k++) {
      const p0 = arriba[k], p1 = arriba[(k + 1) % lados], nn = unit(cruz(resta(p1, [x, cy, z]), resta(p0, [x, cy, z])));
      const ids = [p0, p1].map((p, j) => M.vertice(p, nn[1] > 0 ? nn : escalar(nn, -1), [0.5 + 0.4 * j, 0.15]));
      tri(M, gs, c, ids[0], ids[1], [0, 1, 0]);
    }
  };
  for (let i = 0; i < BRASEROS.length; i++) { // entre braseros: obeliscos y pilares partidos, más bajos frente a la brecha
    const az = BRASEROS[i] + 22 + (rnd() - 0.5) * 8, th = az * RAD, r = radioIsla(th) + 0.3 + rnd() * 0.2, x = r * Math.cos(th), z = r * Math.sin(th);
    const bajo = Math.abs(difAng(az, BRECHA)) < 30, alto = bajo ? 0.5 + rnd() * 0.3 : 1.1 + rnd() * 1.6;
    if (i % 2 && !bajo) obelisco(x, z, 0.24 + rnd() * 0.06, alto, rnd() * Math.PI);
    else partido(x, z, 0.2 + rnd() * 0.08, alto * 0.6, rnd() * 2 * Math.PI);
    for (let k = 0; k < 4; k++) roca(M, gr, x + (rnd() - 0.5) * 0.7, Y_LAVA, z + (rnd() - 0.5) * 0.7, 0.12 + rnd() * 0.14, rnd, 0.9);
  }
  for (let i = 0; i < 70; i++) { // rocas de la orilla
    const th = rnd() * 2 * Math.PI, r = radioIsla(th) + 0.15 + rnd() * 0.6;
    roca(M, gr, r * Math.cos(th), Y_LAVA, r * Math.sin(th), 0.08 + rnd() * 0.16, rnd, 0.8);
  }
  return M;
}
function fuegos(lista) {
  const malla = { ...fuegoMalla(0.72, 0.55), variante: (f) => fuegoMalla(f.radio, 0.55) };
  const figuras = lista.map((f, k) => ({ x: f.x, z: f.z, rumbo: Math.atan2(-f.x, -f.z), escala: 1, tipo: 2, fase: k * 0.29, vel: 1.4 + 0.15 * (k % 3), radio: f.alto }));
  return animado('mod_w130', figuras, malla, [[0, 0, 0, -1], [0, 0, 0, 0]], (cant) => [{ nombre: '', col: [1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 0, 0], tex: 'tex_f155.bmt', ini: 0, n: cant }]);
}

// Cristal de fuego: un racimo de prismas con punta en los dos extremos (sin luz: brilla por su textura), que gira y
// flota, con un halo aditivo.
function cristal() {
  const M = new Malla(), g = M.material('tex_f128.bmt'), rnd = azar(171);
  const prisma = (base, dir, largo, rr) => {
    const d = unit(dir), e1 = unit(cruz(d, Math.abs(d[1]) < 0.9 ? [0, 1, 0] : [1, 0, 0])), e2 = cruz(d, e1), L6 = 6;
    const P = (k, t, k2) => { const a = (k / L6) * 2 * Math.PI; return suma(suma(base, escalar(d, t * largo)), suma(escalar(e1, Math.cos(a) * rr * k2), escalar(e2, Math.sin(a) * rr * k2))); };
    const punta0 = suma(base, escalar(d, -0.18 * largo)), punta1 = suma(base, escalar(d, 1.22 * largo));
    for (let k = 0; k < L6; k++) {
      const a = P(k, 0, 1), b = P(k + 1, 0, 1), c = P(k + 1, 1, 1), dd = P(k, 1, 1), n = unit(resta(suma(a, b), escalar(base, 2)));
      const ids = [a, b, c, dd].map((p, j) => M.vertice(p, n, [j === 1 || j === 2 ? 1 : 0, j >= 2 ? 0.85 : 0.15]));
      tri(M, g, ids[0], ids[1], ids[2], n); tri(M, g, ids[0], ids[2], ids[3], n);
      for (const [q0, q1, pt, v0, v1] of [[c, dd, punta1, 0.85, 1], [b, a, punta0, 0.15, 0]]) {
        const m = unit(cruz(resta(q1, q0), resta(pt, q0))), o = unit(resta(pt, base)), t = [q0, q1, pt].map((p, j) => M.vertice(p, m, [j === 2 ? 0.5 : j, j === 2 ? v1 : v0]));
        tri(M, g, t[0], t[1], t[2], punto(m, resta(suma(q0, q1), escalar(base, 2))) >= 0 ? m : escalar(m, -1));
      }
    }
  };
  prisma([0, -0.8, 0], [0, 1, 0], 1.6, 0.32); // el central
  for (let i = 0; i < 7; i++) { // los que salen de la base
    const a = (i / 7) * 2 * Math.PI + rnd() * 0.5, incl = 0.35 + rnd() * 0.45, dir = [Math.cos(a) * Math.sin(incl), Math.cos(incl) * (i % 3 === 0 ? -1 : 1), Math.sin(a) * Math.sin(incl)];
    prisma(escalar(dir, 0.18), dir, 0.45 + rnd() * 0.55, 0.09 + rnd() * 0.08);
  }
  const n = M.aNmd('');
  const [m, v] = animado('mod_w127', [{ x: CRISTAL.x, z: CRISTAL.z, rumbo: 0, escala: 1, tipo: 4, fase: 0, vel: 0.035, radio: 0.12, incl: CRISTAL.y }], { vs: n.vs, is: n.is, huesos: n.vs.map(() => 0) }, [[0, 0, 0, -1]],
    (cant) => [{ nombre: '', col: [1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0], tex: 'tex_f128.bmt', ini: 0, n: cant }]);
  v.vs = v.vs.map((q) => ({ ...q, p: [q.p[0], q.p[1] + CRISTAL.y, q.p[2]] })); // la vista: a su altura
  // halo: tres cuadrados cruzados
  const H = new Malla(), gh = H.material('tex_f129.bmt', [1, 1, 1, 0.55]), c = [CRISTAL.x, CRISTAL.y, CRISTAL.z], s = 1.5;
  for (const [a, b] of [[[1, 0, 0], [0, 1, 0]], [[0, 0, 1], [0, 1, 0]], [[0.7, 0, 0.7], [0, 1, 0]]]) {
    const q = [[-1, -1], [1, -1], [1, 1], [-1, 1]].map(([i, j]) => suma(c, suma(escalar(a, i * s), escalar(b, j * s * 1.15))));
    H.cara(gh, q, [[0, 1], [1, 1], [1, 0], [0, 0]]);
    H.cara(gh, [...q].reverse(), [[0, 0], [1, 0], [1, 1], [0, 1]]);
  }
  return { m, v, halo: H };
}

// Brasas que suben del lago y de las vetas de la isla (tres cuadrados cruzados cada una, aditivas).
function brasas() {
  const B = new Malla(), g = B.material('tex_f130.bmt'), s = 1;
  for (const [a, b] of [[[1, 0, 0], [0, 1, 0]], [[0, 0, 1], [0, 1, 0]], [[1, 0, 0], [0, 0, 1]]]) {
    const q = [[-1, -1], [1, -1], [1, 1], [-1, 1]].map(([i, j]) => suma(escalar(a, i * s), escalar(b, j * s)));
    B.cara(g, q, [[0, 1], [1, 1], [1, 0], [0, 0]]);
    B.cara(g, [...q].reverse(), [[0, 0], [1, 0], [1, 1], [0, 1]]);
  }
  const n = B.aNmd(''), malla = { vs: n.vs, is: n.is, huesos: n.vs.map(() => 0) }, rnd = azar(191), figuras = [];
  for (let i = 0; i < 300; i++) {
    const enIsla = i < 100, th = rnd() * 2 * Math.PI, r = enIsla ? 0.8 + rnd() * 6.6 : 8 + rnd() * rnd() * 14, x = r * Math.cos(th), z = r * Math.sin(th);
    figuras.push({ x, z, rumbo: rnd() * 6.28, escala: 0.03 + rnd() * 0.045, tipo: 5, fase: rnd(), vel: 0.07 + rnd() * 0.12, radio: 1.5 + rnd() * 3.5, incl: 0.1 + rnd() * 0.3, cojera: enIsla ? 0.02 : Y_LAVA });
  }
  const [m, v] = animado('mod_w129', figuras, malla, [[0, 0, 0, -1]], (cant) => [{ nombre: '', col: [1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0], tex: 'tex_f130.bmt', ini: 0, n: cant }]);
  const nv = malla.vs.length; // la vista (miniatura): cada brasa a la altura de su fase
  v.vs = v.vs.map((q, i) => { const z = figuras[Math.floor(i / nv)]; return { ...q, p: [q.p[0], q.p[1] + z.cojera + z.radio * z.fase, q.p[2]] }; });
  return [m, v];
}

// Agrega el escenario 11 a `campo` y pone su miniatura y su nombre en la fila 9 de tex_sys42 y tex_sys24 (las dos ya
// con las filas de la rueda, ver rueda() en comun.mjs). Devuelve el terreno para dat/suelo.dat.
export function eclipseCaldera(campo, s42, s24) {
  cielo(); eclipse(); lajas(); basalto(); tapa(); rocaTex(); lavaTex(); cascadaTex(); resplandorTex(); cristalTex(); selloTex();

  modelo(piso().aNmd('mod_w120'));
  modelo(columnas().aNmd('mod_w121'));
  const [lm, lv] = lago(); modelo(lm, lv);
  modelo(caldera().aNmd('mod_w123'));
  const [cm, cv] = cascadas(); modelo(cm, cv);
  const listaFuegos = [];
  modelo(borde(listaFuegos).aNmd('mod_w125'));
  modelo(sello().aNmd('mod_w126'));
  modelo(resplandores().aNmd('mod_w131'));
  const cr = cristal(); modelo(cr.m, cr.v); modelo(cr.halo.aNmd('mod_w128'));
  const [bm, bv] = brasas(); modelo(bm, bv);
  const [fm, fv] = fuegos(listaFuegos); modelo(fm, fv);
  modelo(domo('tex_f126.bmt', 'tex_f127.bmt').aNmd('mod_f126'));
  modelo(eclipseQuad().aNmd('mod_f127'));

  const M = (nombre, tipo, z, escribeZ, mezcla) => ({ nombre: `${nombre}.nmd`, tipo, z, escribeZ, mezcla, pruebaAlfa: 0 });
  const e = {
    borrado: [0, 0, 0, 0], escala: 0, // el piso no se corre: queda fijo en el mundo (mod_w)
    modelos: [
      M('mod_w120', 1, 1, 1, 0), // isla
      M('mod_w121', 1, 1, 1, 0), // columnas
      M('mod_w125', 1, 1, 1, 0), // borde: braseros, pilares y rocas
      M('mod_w123', 1, 1, 1, 0), // paredes de la caldera
      M('mod_w122', 3, 1, 1, 0), // lago de lava (animado)
      M('mod_w127', 3, 1, 1, 0), // cristal (animado)
      M('mod_f126', 3, 1, 0, 0), // cielo
      M('mod_f127', 3, 1, 0, 2), // eclipse
      M('mod_w126', 3, 1, 0, 3), // sello
      M('mod_w131', 3, 1, 0, 3), // resplandores
      M('mod_w124', 3, 1, 0, 3), // cascadas (animadas)
      M('mod_w128', 3, 1, 0, 3), // halo del cristal
      M('mod_w130', 3, 1, 0, 3), // fuegos de los braseros (animados)
      M('mod_w129', 3, 1, 0, 3), // brasas (animadas)
    ],
    efecto: 0, efecto2: 0, difusa: [1, 0.64, 0.46, 1], ambiente: [0.48, 0.31, 0.33, 1], dir: [0.3, -0.55, 0.45], viento: [6, 0, 0],
  };
  if (campo.length !== ESCENARIO) throw new Error(`field.dat tiene ${campo.length} escenarios (se esperaban ${ESCENARIO})`);
  campo.push(e);

  miniatura(e, FILA, s42, 'vista-caldera.png');
  const nombre = leerPng(readFileSync(new URL('nombre-caldera.png', import.meta.url)));
  for (let y = 0; y < 32; y++) nombre.rgba.copy(s24.rgba, 4 * (FILA * 32 + y) * s24.w, 4 * y * nombre.w, 4 * (y + 1) * nombre.w);
  return SUELO;
}
