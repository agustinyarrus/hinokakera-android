// Sunlit Shore: escenario nuevo de playa con sol (el 10.º de field.dat, el número 9; la rueda de selección lo
// muestra en el lugar 7 con los parches de analisis/parches.txt). Primero arma Outskirt Hill de noche
// (moonlit.mjs) y después le suma la playa al mismo <scratch>/mods/vsobj2.dat: field.dat con 10 escenarios, la
// rueda con su nombre y su miniatura (texturas más altas) y los modelos y texturas de la playa.
// Uso: node tools/escenario/playa.mjs [--png carpeta]   (--png guarda las texturas y una vista con la cámara del juego)
import { writeFileSync, mkdirSync, readFileSync } from 'node:fs';
import { join } from 'node:path';
import { SCRATCH } from '../datos.mjs';
import { leerDat1, escribirDat1 } from '../dat1.mjs';
import { escribirNmd } from './nmd.mjs';
import { leerBmt } from './bmt.mjs';
import { leerCampo, escribirCampo } from './campo.mjs';
import { Lienzo, azar, fbm, perlin, sat, suave, mezclar, hex, trazo } from './pintura.mjs';
import { Malla, unit, cruz, resta, suma, escalar, punto, rotY } from './geometria.mjs';
import { vista } from './vista.mjs';
import { principal as noche, sacar, elevAV, vAElev, domo } from './moonlit.mjs';

const PNG = process.argv.includes('--png') ? process.argv[process.argv.indexOf('--png') + 1] : null;
export const ESCENARIO = 9, LUGAR = 7; // número en field.dat y lugar en la rueda (ver analisis/parches.txt)
const RAD = Math.PI / 180;

// Hacia dónde queda cada cosa, en grados desde +X hacia +Z (la cámara arranca mirando a +Z, a 90°; a la derecha
// quedan los ángulos menores).
export const SOL = { ang: 72, elev: 8, dist: 38, radio: 1.7 };
// El mar ocupa el frente, de MAR.a0 a MAR.a1; la orilla es una bahía: a 9,2 de frente y más lejos a los costados.
export const MAR = { a0: -25, a1: 205, lejos: 38.5, hondo: 30 };
export const NIVEL = -0.13; // altura del agua: la arena baja apenas desde la zona de pelea (r < 5.5) hasta la orilla
export const orilla = (th) => 9.2 + 5.5 * Math.pow(1 - Math.sin(th * RAD), 1.4) + 0.35 * perlin(th / 23, 0.5, 71);
const thMar = (th) => ((((th + 90) % 360) + 360) % 360) - 90; // en [-90, 270), para compararlo con MAR
// Más bajada esconde la orilla detrás del borde de la zona plana: la cámara del juego va a ~1 de altura.
const caida = (th) => { const t = thMar(th); return suave(MAR.a0 - 25, MAR.a0, t) * (1 - suave(MAR.a1, MAR.a1 + 25, t)); };
export const alturaPiso = (r, th) => { const c = caida(th), o = orilla(th); return -c * (0.15 * suave(5.5, o + 0.3, r) + 0.6 * suave(o, o + 5, r)); };
export const PROMONTORIOS = [{ ang: -14, dist: 21, faro: true }, { ang: 194, dist: 21 }];

const base = new Map(); // lo que dejó moonlit.mjs en mods/vsobj2.dat
const leer = (nombre) => base.get(nombre) ?? sacar(nombre);
const salidas = new Map(); // nombre -> datos (reemplaza a lo de base)
const generadas = new Map(); // textura -> RGBA8, para la vista
const modelos = new Map();
function textura(nombre, L) {
  generadas.set(`${nombre}.bmt`, { w: L.w, h: L.h, rgba: L.rgba8() });
  salidas.set(`obj/${nombre}.bmt`, L.bmt());
  if (PNG) writeFileSync(join(PNG, `${nombre}.png`), L.png());
}
const modelo = (m) => { modelos.set(m.nombre, m); salidas.set(`obj/${m.nombre}.nmd`, escribirNmd(m)); };
const dirA = (ang, elev = 0) => [Math.cos(elev * RAD) * Math.cos(ang * RAD), Math.sin(elev * RAD), Math.cos(elev * RAD) * Math.sin(ang * RAD)];
const enPiso = (ang, dist, dy = 0) => { const d = dirA(ang); return [d[0] * dist, alturaPiso(dist, ang) + Math.max(0, medano(dist, ang) + 0.06) + dy, d[2] * dist]; };
const gradiente = (stops, t) => {
  if (t <= stops[0][0]) return stops[0][1];
  for (let i = 1; i < stops.length; i++) if (t <= stops[i][0]) return mezclar(stops[i - 1][1], stops[i][1], (t - stops[i - 1][0]) / (stops[i][0] - stops[i - 1][0]));
  return stops[stops.length - 1][1];
};
const difAng = (a, b) => ((((a - b) % 360) + 540) % 360) - 180;
const ruido3 = (x, y, z, s) => (perlin(x, y, s) + perlin(y + 31.7, z, s + 1) + perlin(z + 17.3, x, s + 2)) / 1.6;
function hash2(x, y) {
  let h = Math.imul(x, 0x27d4eb2d) ^ Math.imul(y, 0x165667b1);
  h = Math.imul(h ^ (h >>> 15), 0x85ebca6b); h ^= h >>> 13;
  return ((h ^ (h >>> 16)) >>> 0) / 4294967296;
}

// ---------- cielo: panorama de 4096x1024 con la elevación de moonlit (de -10° a 30° en 3/4 de la textura) ----------
const CIELO = [[-10, hex('#9cc9de')], [0, hex('#d2edf7')], [2, hex('#c0e5f8')], [6, hex('#97d1f5')], [12, hex('#70b9ef')], [22, hex('#519fe6')], [40, hex('#3685dc')], [65, hex('#2b71cf')], [90, hex('#235dbf')]];
export const HORIZONTE_MAR = hex('#8cc0da'); // el mar se funde con esto a lo lejos
const ISLAS = [{ ang: 101, ancho: 10, alto: 1.5, pico: 0.35 }, { ang: 137, ancho: 4.5, alto: 0.7, pico: 0.6 }, { ang: 168, ancho: 7, alto: 1, pico: 0.5 }, { ang: 41, ancho: 5, alto: 0.55, pico: 0.4 }];

// Cúmulos: racimos de bolas (en grados) con la base plana; los bajos son chicos y lejanos.
function nubes(rnd) {
  const lista = [];
  const agregar = (th, eb, s, n) => {
    const bolas = [];
    for (let k = 0; k < n; k++) {
      const r = s * (0.55 + rnd() * 0.6), d = (rnd() - 0.5) * s * (2.2 + n * 0.35);
      bolas.push({ th: th + d, e: eb + r * (0.55 + 0.5 * rnd()) * (1 - Math.abs(d) / (s * (1.5 + n * 0.2))), r });
    }
    lista.push({ th, eb, s, bolas });
  };
  for (let i = 0; i < 46; i++) { // lejanas, sobre el horizonte
    const th = rnd() * 360, eb = 1.2 + rnd() * 3.5;
    if (Math.abs(difAng(th, SOL.ang)) < 9) continue;
    agregar(th, eb, 0.5 + rnd() * 0.9, 3 + Math.floor(rnd() * 5));
  }
  for (let i = 0; i < 22; i++) { // medianas
    const th = rnd() * 360, eb = 5 + rnd() * 9;
    if (Math.abs(difAng(th, SOL.ang)) < 14 && eb < 16) continue;
    agregar(th, eb, 1.3 + rnd() * 1.6, 6 + Math.floor(rnd() * 6));
  }
  for (let i = 0; i < 9; i++) { // grandes y altas
    const th = rnd() * 360, eb = 14 + rnd() * 12;
    if (Math.abs(difAng(th, SOL.ang)) < 22) continue;
    agregar(th, eb, 3 + rnd() * 2.5, 8 + Math.floor(rnd() * 6));
  }
  agregar(SOL.ang + 11, 10.5, 1.4, 7); // una cerca del sol, con el borde plateado
  return lista;
}

function cielo() {
  const W = 4096, H = 1024, L = new Lienzo(W, H), rnd = azar(17), lista = nubes(rnd);
  const sol = dirA(SOL.ang, SOL.elev);
  // Distancia con signo a una nube (en grados; negativa adentro) y su densidad.
  const dist = (n, th, e) => {
    let d = 1e9;
    for (const b of n.bolas) d = Math.min(d, Math.hypot(difAng(th, b.th) * Math.cos(e * RAD), (e - b.e) * 1.15) - b.r);
    return d + n.s * 0.32 * fbm(th / (n.s * 0.9), e / (n.s * 0.7), 4, 5 + Math.round(n.th));
  };
  const cerca = lista.map((n) => ({ n, ancho: n.s * (3 + n.bolas.length * 0.35) + 2 }));
  // de qué lado está el mar (1) y de cuál la tierra (0), con un paso suave en las puntas
  const marLado = (th) => { const t = thMar(th); return suave(MAR.a0 - 12, MAR.a0 + 6, t) * (1 - suave(MAR.a1 - 6, MAR.a1 + 12, t)); };
  L.cada((x, y) => {
    const e = vAElev((y + 0.5) / H), th = ((x + 0.5) / W) * 360, dir = dirA(th, e);
    const ang = Math.acos(Math.max(-1, Math.min(1, dir[0] * sol[0] + dir[1] * sol[1] + dir[2] * sol[2]))) / RAD;
    let c = gradiente(CIELO, e);
    // resplandor del sol y bruma tibia del horizonte de su lado
    const g = 0.95 * Math.exp(-((ang / 3.2) ** 2)) + 0.42 * Math.exp(-((ang / 11) ** 2)) + 0.2 * Math.exp(-((ang / 32) ** 2));
    c = mezclar(c, [1, 0.97, 0.86], sat(g));
    c = mezclar(c, [0.98, 0.95, 0.84], 0.28 * Math.exp(-((difAng(th, SOL.ang) / 45) ** 2)) * Math.exp(-Math.max(e, 0) / 5));
    // cirros altos
    const ci = suave(0.2, 0.7, fbm(x / 700, y / 90, 5, 23, W / 700, 0)) * suave(28, 45, e) * (1 - suave(70, 88, e));
    c = mezclar(c, [0.97, 0.98, 1], ci * 0.45);
    // cúmulos
    for (const { n, ancho } of cerca) {
      if (Math.abs(difAng(th, n.th)) > ancho || e < n.eb - 1 || e > n.eb + n.s * 4) continue;
      const d = dist(n, th, e), borde = n.s * 0.22;
      if (d > borde) continue;
      const a = sat(-d / borde + 0.5) * suave(n.eb - 0.15 * n.s, n.eb + 0.35 * n.s, e);
      if (a <= 0) continue;
      const h = 0.04 * n.s, gx = (dist(n, th + h, e) - dist(n, th - h, e)) / (2 * h), ge = (dist(n, th, e + h) - dist(n, th, e - h)) / (2 * h);
      const gl = Math.hypot(gx, ge) || 1, nx = gx / gl, ne = ge / gl; // normal hacia afuera
      const sx = difAng(SOL.ang, th), se = SOL.elev - e, sl = Math.hypot(sx, se) || 1;
      const luz = sat(0.5 + 0.5 * (nx * sx + ne * se) / sl), arriba = suave(n.eb, n.eb + n.s * 2.2, e);
      const contra = Math.exp(-((ang / 28) ** 2)), espesor = sat(-d / (n.s * 1.2));
      let col = mezclar(hex('#a9b9cf'), [1, 1, 1], sat(luz * 0.55 + arriba * 0.55));
      col = mezclar(col, mezclar(hex('#dfe6ef'), hex('#9fb1c9'), espesor), contra * 0.7);
      col = mezclar(col, [1, 0.98, 0.9], contra * sat(1 + d / (n.s * 0.18)) * 0.9); // borde plateado a contraluz
      col = mezclar(col, gradiente(CIELO, e), 0.4 * (1 - suave(2, 7, n.eb))); // las lejanas, con bruma
      c = mezclar(c, col, a);
    }
    // del lado de tierra: sierras verdes lejanas, una detrás de la otra
    const tierra = 1 - marLado(th);
    if (tierra > 0 && e < 9) {
      const lejos = 2.6 + 2.2 * fbm(th / 14, 0.3, 4, 81) + 1.6 * Math.pow(Math.abs(Math.sin((th / 360) * 9 * Math.PI)), 3);
      const cerca2 = 1.2 + 1.3 * fbm(th / 7, 0.7, 4, 83);
      if (e < lejos) c = mezclar(c, mezclar(hex('#5d8f86'), gradiente(CIELO, e), 0.55 - 0.08 * suave(lejos - 1, lejos, e)), tierra);
      if (e < cerca2) c = mezclar(c, mezclar(hex('#4b7d58'), hex('#77a68a'), 0.35 + 0.25 * fbm(th / 2, e, 3, 85)), tierra);
    }
    // islas en el horizonte del mar, con bruma
    for (const isla of ISLAS) {
      const t = 0.5 + difAng(th, isla.ang) / isla.ancho;
      if (t <= 0 || t >= 1 || e > isla.alto * 1.6) continue;
      const perfil = isla.alto * Math.pow(Math.sin(Math.PI * t), 0.7) * (1 + isla.pico * Math.exp(-(((t - 0.45) / 0.12) ** 2))) * (1 + 0.12 * fbm(t * 6, 0.5, 3, isla.ang));
      if (e > perfil) continue;
      const luz = 0.5 + 0.5 * Math.sign(difAng(SOL.ang, th)) * (t - 0.5) * 2;
      c = mezclar(c, mezclar(mezclar(hex('#4e7f86'), hex('#78a6a8'), luz), HORIZONTE_MAR, 0.45), sat((perfil - e) * 8) * marLado(th));
    }
    // abajo del horizonte: el mar a lo lejos (o la tierra), lo que se ve más allá del borde del agua
    if (e < 0) c = mezclar(mezclar(hex('#7e9c86'), mezclar(HORIZONTE_MAR, hex('#6fa9c9'), sat(-e / 6)), marLado(th)), c, suave(-0.7, 0, e) * 0.6);
    return [...c, 1];
  });
  gaviotas(L, rnd);
  textura('tex_f100', L.recorte(0, 0, 2048, 1024));
  textura('tex_f101', L.recorte(2048, 0, 2048, 1024));
}

// Gaviotas: una "m" con las alas curvas.
function gaviotas(L, rnd) {
  for (let i = 0; i < 9; i++) {
    const th = 45 + rnd() * 110, e = 6 + rnd() * 13, x = (th / 360) * L.w, y = elevAV(e) * L.h, s = 3 + rnd() * 4, b = rnd() * 0.6;
    const ala = (lado) => { let px = x, py = y; for (let k = 1; k <= 6; k++) { const t = k / 6, nx = x + lado * s * t, ny = y - s * (0.7 * Math.sin(Math.PI * t * 0.9) - b * t); trazo(L, px, py, 0.8, nx, ny, 0.55, hex('#4a5a6e'), 0.85); px = nx; py = ny; } };
    ala(-1); ala(1);
  }
}

// ---------- sol: disco con halo y rayos, para sumar (mezcla aditiva) ----------
function sol() {
  const N = 512, L = new Lienzo(N, N), c0 = N / 2, rd = N * 0.075;
  L.cada((x, y) => {
    const dx = x + 0.5 - c0, dy = y + 0.5 - c0, r = Math.hypot(dx, dy) / rd, a = Math.atan2(dy, dx);
    const disco = sat((1 - r) * rd * 0.6 + 0.5);
    const halo = 0.55 * Math.exp(-Math.max(0, r - 1) / 0.4) + 0.2 * Math.exp(-r / 2.2);
    const rayos = (0.2 * Math.pow(Math.abs(Math.cos(3 * a + 0.3)), 90) + 0.1 * Math.pow(Math.abs(Math.cos(5 * a + 1.1)), 140)) * Math.exp(-r / 3);
    const borde = sat(Math.min(c0 - Math.abs(dx), c0 - Math.abs(dy)) / (N * 0.08));
    const i = sat(disco + (halo + rayos) * (1 - disco)) * borde;
    return [...mezclar([1, 0.86, 0.6], [1, 1, 0.96], sat(disco + 0.3 * halo)).map((v) => v * i), 1];
  });
  textura('tex_f102', L);
}
function solQuad() {
  const M = new Malla(), g = M.material('tex_f102.bmt'), d = dirA(SOL.ang, SOL.elev);
  const c = escalar(d, SOL.dist), der = unit(cruz([0, 1, 0], d)), arr = cruz(d, der);
  const s = (SOL.dist * Math.tan(SOL.radio * RAD)) / 0.15; // el disco ocupa 0.15 del ancho de la textura
  const P = (u, v) => suma(c, suma(escalar(der, (u - 0.5) * 2 * s), escalar(arr, (0.5 - v) * 2 * s)));
  M.cara(g, [P(0, 0), P(1, 0), P(1, 1), P(0, 1)], [[0, 0], [1, 0], [1, 1], [0, 1]]);
  return M;
}

// ---------- mar: textura en (ángulo, distancia a la orilla s), con más detalle cerca de la orilla ----------
const S0 = -1.5; // la arena mojada empieza 1.5 antes del agua
const vMar = (s) => Math.pow((s - S0) / (MAR.hondo - S0), 0.55);
const sMar = (v) => S0 + Math.pow(v, 1 / 0.55) * (MAR.hondo - S0);
const AGUA = [[0, hex('#8fe8db')], [0.8, hex('#4fd6d2')], [2.2, hex('#22b8c9')], [4.5, hex('#139cc0')], [9, hex('#0f7fb3')], [16, hex('#0d68a6')], [30, hex('#0c5c9c')]];

function mar() {
  const W = 2048, H = 1024, L = new Lienzo(W, H), rnd = azar(23), span = MAR.a1 - MAR.a0;
  L.cada((x, y) => {
    const th = MAR.a0 + ((x + 0.5) / W) * span, s = sMar((y + 0.5) / H), px = W / 64;
    const n1 = perlin(x / 32, s * 3, 31, px * 2), n2 = fbm(x / 64, s * 1.4, 4, 37, px);
    if (s < 0) { // arena mojada: más oscura y brillante cerca del agua
      const a = 0.62 * suave(S0, -0.15, s) + 0.12 * Math.exp(-(((s + 0.75 + 0.15 * n1) / 0.06) ** 2));
      return [...mezclar(hex('#9c8158'), hex('#b9c7cf'), 0.35 * suave(-0.6, 0, s)), a];
    }
    let c = gradiente(AGUA, s), a = 0.38 + 0.62 * suave(0, 2.4, s);
    // olas paralelas a la orilla (λ 1.3) y destellos de cielo en las crestas
    const fase = s / 1.3 + 0.8 * n2 + 0.15 * perlin(x / 12, s * 6, 41, px * 4);
    const cresta = Math.pow(0.5 + 0.5 * Math.sin(2 * Math.PI * fase), 6);
    c = c.map((v, k) => v * (0.94 + 0.09 * Math.sin(2 * Math.PI * fase)) + cresta * 0.06 * [0.7, 0.85, 1][k] * suave(2, 6, s));
    // cáusticas en lo bajo
    const ca = Math.pow(1 - Math.abs(perlin(x / 9, s * 7, 43, px * 7) + 0.6 * perlin(x / 5, s * 11, 47, px * 12)), 8);
    c = c.map((v, k) => v + ca * 0.16 * [0.85, 1, 0.9][k] * (1 - suave(0.4, 2.6, s)));
    // espuma: la que lame la arena y dos rompientes
    const lame = suave(-0.05, 0.04, s - 0.05 * n1) * (1 - suave(0.1, 0.38, s - 0.12 * n2));
    const rompe = (s0, ancho) => Math.exp(-(((s - s0 - 0.25 * n2) / ancho) ** 2)) * suave(-0.1, 0.35, perlin(x / 40, s0 * 3, 53 + s0 * 10, px));
    const espuma = sat(Math.max(lame, rompe(1.7, 0.16) * 0.85, rompe(3.6, 0.22) * 0.7) * (0.75 + 0.35 * perlin(x / 6, s * 22, 59, px * 10)));
    c = mezclar(c, [0.97, 0.99, 1], espuma); a = Math.max(a, espuma * 0.95);
    // a lo lejos se funde con el horizonte
    c = mezclar(c, HORIZONTE_MAR, 0.8 * suave(9, MAR.hondo, s));
    // brillo del sol: un camino de destellos debajo del sol que se abre hacia el horizonte
    const camino = Math.exp(-((difAng(th, SOL.ang) / (3.5 + s * 0.16)) ** 2));
    c = mezclar(c, [1, 0.98, 0.88], camino * 0.35 * suave(3, 26, s));
    return [...c, a];
  });
  // destellos sueltos sobre el camino del sol
  for (let i = 0; i < 9000; i++) {
    const v = Math.pow(rnd(), 0.6), s = sMar(v), th = SOL.ang + (rnd() - 0.5) * 2 * (6 + s * 0.3), dc = Math.exp(-((difAng(th, SOL.ang) / (3.5 + s * 0.16)) ** 2));
    if (s < 1.5 || rnd() > dc) continue;
    const x = ((th - MAR.a0) / span) * W, y = v * H, l = 0.6 + rnd() * 2.2;
    trazo(L, x - l, y, 0.6, x + l, y, 0.5, [1, 0.99, 0.93], 0.5 + 0.5 * rnd());
  }
  textura('tex_f103', L);
}
// Malla del mar: ángulos de MAR.a0 a MAR.a1 y distancias a la orilla de S0 a MAR.lejos. Plana al nivel del agua y,
// en la arena mojada, apoyada sobre la arena que baja.
function marMalla() {
  const M = new Malla(), g = M.material('tex_f103.bmt'), S = [S0, -1.1, -0.7, -0.4, -0.15, 0.05, 0.25, 0.5, 0.85, 1.3, 1.9, 2.7, 3.7, 5, 7, 9.5, 13, 18, 24, 30];
  const filas = [], paso = 2.5, n = Math.round((MAR.a1 - MAR.a0) / paso);
  for (const s of S) {
    filas.push(Array.from({ length: n + 1 }, (_, k) => {
      const th = MAR.a0 + k * paso, r = Math.min(orilla(th) + s, MAR.lejos), d = dirA(th);
      return M.vertice([d[0] * r, Math.max(alturaPiso(r, th) + 0.025, NIVEL), d[2] * r], [0, 1, 0], [k / n, vMar(r - orilla(th))]);
    }));
  }
  for (let i = 0; i + 1 < filas.length; i++) for (let k = 0; k < n; k++) {
    const a = filas[i][k], b = filas[i][k + 1], c = filas[i + 1][k + 1], d = filas[i + 1][k];
    g.tris.push(a, b, c, a, c, d); // mirando para arriba
  }
  return M;
}

// ---------- arena: se repite cada 5 unidades (el piso corre la textura con la pelea) ----------
function arena() {
  const N = 1024, L = new Lienzo(N, N), rnd = azar(29);
  L.cada((x, y) => {
    const ondas = Math.sin(2 * Math.PI * ((30 * x + 12 * y) / N + 0.9 * fbm(x / 128, y / 128, 3, 61, N / 128, N / 128)));
    const tono = 0.06 * fbm(x / 256, y / 256, 4, 67, N / 256, N / 256) + 0.025 * ondas + 0.045 * (hash2(x, y) - 0.5);
    return [...mezclar(hex('#d8bd8c'), hex('#f0dcb3'), sat(0.55 + tono * 4)), 1];
  });
  // conchitas, alguna piedrita y algún palito, con su sombra (se repiten en los bordes)
  for (let i = 0; i < 34; i++) {
    const x = rnd() * N, y = rnd() * N, tipo = rnd(), r = 3 + rnd() * 4;
    for (const ox of [-N, 0, N]) for (const oy of [-N, 0, N]) {
      const X = x + ox, Y = y + oy;
      if (X < -20 || Y < -20 || X > N + 20 || Y > N + 20) continue;
      trazo(L, X + 1.2, Y + 1.2, r, X + 1.2, Y + 1.2, r, hex('#b39a6c'), 0.3);
      if (tipo < 0.6) { // concha clara con estrías
        trazo(L, X, Y, r, X, Y, r, mezclar(hex('#f8f2ea'), hex('#f4cdbd'), rnd()), 1);
        for (let k = -2; k <= 2; k++) trazo(L, X, Y + r * 0.7, 0.5, X + k * r * 0.35, Y - r * 0.6, 0.4, hex('#dcc1ad'), 0.6);
      } else if (tipo < 0.82) trazo(L, X, Y, r * 0.6, X, Y, r * 0.6, mezclar(hex('#b3ada3'), hex('#d6d0c6'), rnd()), 1); // piedrita
      else trazo(L, X - r * 1.6, Y, 1.2, X + r * 1.6, Y + r * 0.4, 0.9, hex('#9a7d58'), 0.85); // palito
    }
  }
  textura('tex_f104', L);
}
// Piso: disco de radio 26 con la textura cada 5 unidades como el del juego (u = x/5 - 0.5, v = 0.5 - z/5); plano
// donde se pelea y bajando hacia el agua del lado del mar.
function piso() {
  const M = new Malla(), g = M.material('tex_f104.bmt'), radios = [0, 2, 4, 5.5, 6.2, 7, 7.8, 8.6, 9.4, 10.2, 11, 12, 13.5, 15.5, 18, 21, 26], lados = 96, ids = [];
  for (const r of radios) ids.push(Array.from({ length: lados + 1 }, (_, k) => {
    const th = (k / lados) * 360, d = dirA(th), p = [d[0] * r, alturaPiso(r, th), d[2] * r];
    return M.vertice(p, [0, 1, 0], [p[0] / 5 - 0.5, 0.5 - p[2] / 5]);
  }));
  for (let i = 0; i + 1 < ids.length; i++) for (let k = 0; k < lados; k++) {
    const a = ids[i][k], b = ids[i][k + 1], c = ids[i + 1][k + 1], d = ids[i + 1][k];
    g.tris.push(a, b, c, a, c, d);
  }
  return M;
}

// ---------- texturas de las cosas ----------
// Hojas (1024x1024): arriba, cuatro pencas de palmera de 256x512 (tres verdes y una seca) con el raquis al medio
// (la punta arriba); abajo a la izquierda dos matas de pasto de 256x256 y abajo a la derecha una mata de uva de mar.
function hojas() {
  const L = new Lienzo(1024, 1024);
  for (let k = 0; k < 4; k++) {
    const rnd = azar(300 + k), seca = k === 3, x0 = 256 * k + 128;
    const verde = seca ? [hex('#6d5634'), hex('#a3804c'), hex('#c7a46a')] : [hex('#2f6a26'), mezclar(hex('#4f9a35'), hex('#5aa23a'), rnd()), hex('#a5c95a')];
    for (let y = 500; y > 14; y -= 4.2) {
      const t = (500 - y) / 486, largo = (seca ? 95 : 118) * Math.pow(Math.sin(Math.PI * Math.min(1, 0.06 + t * 0.98)), 0.8);
      for (const lado of [-1, 1]) {
        if (seca && rnd() < 0.18) continue;
        const l = largo * (0.85 + 0.3 * rnd()), ang = (seca ? 70 : 52 + 10 * rnd()) * RAD, caer = (seca ? 0.5 : 0.18) * l;
        let px = x0 + lado * 2, py = y;
        for (let s = 1; s <= 7; s++) {
          const u = s / 7, nx = x0 + lado * (2 + Math.sin(ang) * l * u), ny = y - Math.cos(ang) * l * u + caer * u * u;
          trazo(L, px, py, 2.6 * (1 - u * 0.75), nx, ny, 2.6 * (1 - (u + 0.14) * 0.75), mezclar(mezclar(verde[0], verde[1], suave(0, 0.5, u)), verde[2], suave(0.55, 1, u) * (0.5 + 0.5 * t)));
          px = nx; py = ny;
        }
      }
    }
    trazo(L, x0, 506, 4, x0, 10, 1, seca ? hex('#8a6a3c') : hex('#9fb54e')); // raquis
  }
  // pasto de médano: hojas finas que salen de un pie
  for (let k = 0; k < 2; k++) {
    const rnd = azar(400 + k), x0 = 128 + 256 * k, base = 1012;
    for (let i = 0; i < 46; i++) {
      const alto = 110 + rnd() * 120, curva = (rnd() - 0.5) * 150, r0 = 1.4 + rnd() * 1.2, col = mezclar(hex('#6f8a45'), hex('#cdbf7e'), 0.3 + rnd() * 0.7);
      let px = x0 + (rnd() - 0.5) * 30, py = base;
      const x1 = px;
      for (let s = 1; s <= 8; s++) { const t = s / 8, nx = x1 + curva * t * t, ny = base - alto * t + Math.abs(curva) * 0.3 * t * t; trazo(L, px, py, r0 * (1 - t * 0.7), nx, ny, r0 * (1 - (t + 0.12) * 0.7), mezclar(col, hex('#e3d9a0'), t * 0.5)); px = nx; py = ny; }
    }
  }
  // uva de mar: hojas redondas con nervio claro, más oscuras abajo, algunas flores
  const rnd = azar(450);
  for (let i = 0; i < 260; i++) {
    const a = rnd() * Math.PI, d = Math.sqrt(rnd()) * 230, x = 768 + Math.cos(a) * d * 1.05, y = 1010 - Math.sin(a) * d, r = 13 + rnd() * 9;
    const luz = sat(0.3 + (1010 - y) / 360 + (rnd() - 0.5) * 0.3), col = mezclar(hex('#1d4419'), hex('#5f9a3c'), luz);
    trazo(L, x, y, r, x, y, r, col);
    trazo(L, x, y + r * 0.8, 0.9, x + (rnd() - 0.5) * 4, y - r * 0.7, 0.6, mezclar(col, hex('#d6e6a0'), 0.45));
  }
  for (let i = 0; i < 26; i++) { const a = rnd() * Math.PI, d = Math.sqrt(rnd()) * 200, x = 768 + Math.cos(a) * d, y = 1000 - Math.sin(a) * d; trazo(L, x, y, 5, x, y, 5, mezclar(hex('#e8457a'), hex('#ff9ac0'), rnd())); }
  textura('tex_f105', L);
}
function corteza() { // anillos de palmera, 256x512 (se repite)
  const N = 256, H = 512, L = new Lienzo(N, H);
  L.cada((x, y) => {
    const anillo = (y % 32) / 32, marca = Math.exp(-(((anillo - 0.1) / 0.05) ** 2)), fibra = fbm(x / 6, y / 40, 3, 91, N / 6, H / 40);
    const v = 0.78 + 0.16 * fibra - 0.32 * marca + 0.08 * suave(0.2, 0.9, anillo);
    return [...hex('#8f7c63').map((c) => c * v), 1];
  });
  textura('tex_f106', L);
}
function roca() { // piedra clara con grietas y líquenes, 512x512 (se repite)
  const N = 512, L = new Lienzo(N, N);
  L.cada((x, y) => {
    const n = fbm(x / 64, y / 64, 5, 101, N / 64, N / 64), q = fbm(x / 20, y / 20, 3, 105, N / 20, N / 20) * 0.35;
    const grieta = Math.pow(1 - Math.abs(perlin(x / 90 + q, y / 70 + q, 103, N / 90, N / 70)), 40) * suave(-0.1, 0.25, perlin(x / 128, y / 128, 109, N / 128, N / 128));
    const liquen = suave(0.35, 0.6, fbm(x / 48, y / 48, 4, 107, N / 48, N / 48)) * 0.5;
    let c = mezclar(hex('#6a625a'), hex('#bdb2a1'), sat(0.5 + n * 1.5));
    c = mezclar(c, hex('#a8a46a'), liquen * 0.45);
    return [...c.map((v) => v * (1 - grieta * 0.45) * (0.94 + 0.12 * hash2(x, y))), 1];
  });
  textura('tex_f107', L);
}
function madera() { // cuatro tablas gastadas por textura (a lo largo de u), 256x256
  const N = 256, L = new Lienzo(N, N);
  L.cada((x, y) => {
    const tabla = Math.floor(y / 64), yy = y % 64, junta = yy < 3 || yy > 61, veta = fbm(x / 60, y / 3 + tabla * 13, 4, 111, N / 60, N / 3);
    const c = mezclar(hex('#7d6a53'), hex('#b49f80'), sat(0.55 + veta * 1.2 + (tabla % 2) * 0.06));
    return [...(junta ? c.map((v) => v * 0.45) : c), 1];
  });
  textura('tex_f108', L);
}
function paja() { // techo de paja en hileras, 256x256
  const N = 256, L = new Lienzo(N, N, [...hex('#8a6d3c'), 1]), rnd = azar(121);
  for (let fila = 0; fila < 4; fila++) for (let i = 0; i < 260; i++) {
    const x = rnd() * N, y0 = fila * 64 - 6 + rnd() * 8, l = 50 + rnd() * 26, col = mezclar(hex('#b8924f'), hex('#e3c787'), rnd());
    for (const o of [-N, 0, N]) trazo(L, x + o, y0, 1.3, x + o + (rnd() - 0.5) * 8, y0 + l, 0.8, col, 0.9);
  }
  textura('tex_f109', L);
}
function pintura() { // blanco gastado (el color va en el material)
  const L = new Lienzo(64, 64);
  L.cada((x, y) => [...[1, 1, 1].map((v) => v * (0.9 + 0.1 * fbm(x / 8, y / 8, 3, 131, 8, 8))), 1]);
  textura('tex_f110', L);
}
function verde() { // pasto corto para lo alto de los promontorios, 256x256
  const N = 256, L = new Lienzo(N, N);
  L.cada((x, y) => [...mezclar(hex('#365f26'), hex('#6f9645'), sat(0.5 + 1.2 * fbm(x / 24, y / 24, 4, 141, N / 24, N / 24) + 0.15 * (hash2(x, y) - 0.5))), 1]);
  textura('tex_f111', L);
}

// ---------- geometría ----------
// Icosfera (caras hacia afuera según la regla de geometria.mjs).
function icosfera(div) {
  const t = (1 + Math.sqrt(5)) / 2;
  const vs = [[-1, t, 0], [1, t, 0], [-1, -t, 0], [1, -t, 0], [0, -1, t], [0, 1, t], [0, -1, -t], [0, 1, -t], [t, 0, -1], [t, 0, 1], [-t, 0, -1], [-t, 0, 1]].map(unit);
  let fs = [[0, 11, 5], [0, 5, 1], [0, 1, 7], [0, 7, 10], [0, 10, 11], [1, 5, 9], [5, 11, 4], [11, 10, 2], [10, 7, 6], [7, 1, 8], [3, 9, 4], [3, 4, 2], [3, 2, 6], [3, 6, 8], [3, 8, 9], [4, 9, 5], [2, 4, 11], [6, 2, 10], [8, 6, 7], [9, 8, 1]];
  for (let d = 0; d < div; d++) {
    const cache = new Map(), medio = (a, b) => { const k = a < b ? `${a},${b}` : `${b},${a}`; if (!cache.has(k)) { cache.set(k, vs.length); vs.push(unit(suma(vs[a], vs[b]))); } return cache.get(k); };
    fs = fs.flatMap(([a, b, c]) => { const ab = medio(a, b), bc = medio(b, c), ca = medio(c, a); return [[a, ab, ca], [b, bc, ab], [c, ca, bc], [ab, bc, ca]]; });
  }
  return { vs, fs };
}
const ESFERA = [icosfera(1), icosfera(2), icosfera(3)];

// Piedra: icosfera deformada con ruido. Cada cara elige material (mojada cerca del agua, pasto arriba si es plana)
// y su uv por el eje en el que más mira, así la textura no se estira.
function piedra(M, mats, centro, esc, giro, semilla, { div = 1, rugo = 0.28, pasto = Infinity } = {}) {
  const { vs, fs } = ESFERA[div];
  const P = vs.map((v) => {
    const k = 1 + rugo * ruido3(v[0] * 1.6 + semilla, v[1] * 1.6, v[2] * 1.6, semilla) + rugo * 0.3 * ruido3(v[0] * 4.5, v[1] * 4.5, v[2] * 4.5, semilla + 5);
    return suma(rotY([v[0] * esc[0] * k, v[1] * esc[1] * k, v[2] * esc[2] * k], giro), centro);
  });
  const N = vs.map(() => [0, 0, 0]);
  for (const [a, b, c] of fs) { const n = cruz(resta(P[b], P[a]), resta(P[c], P[a])); for (const i of [a, b, c]) N[i] = suma(N[i], n); }
  for (const [a, b, c] of fs) {
    const n = unit(cruz(resta(P[b], P[a]), resta(P[c], P[a]))), y = (P[a][1] + P[b][1] + P[c][1]) / 3, ax = n.map(Math.abs);
    const uv = (p) => (ax[1] >= ax[0] && ax[1] >= ax[2] ? [p[0] * 0.3, p[2] * 0.3] : ax[0] >= ax[2] ? [p[2] * 0.3, -p[1] * 0.3] : [p[0] * 0.3, -p[1] * 0.3]);
    const g = y > pasto && n[1] > 0.55 ? mats.pasto : y < NIVEL + 0.45 ? mats.mojada : mats.seca;
    g.tris.push(...[a, b, c].map((i) => M.vertice(P[i], unit(suma(escalar(n, 0.6), escalar(unit(N[i]), 0.4))), uv(P[i])))); // facetada
  }
  return P;
}

// Promontorio: un montón de piedras grandes metidas en el agua, con pasto arriba. Devuelve la cima.
function promontorio(M, mats, { ang, dist }, semilla) {
  const rnd = azar(semilla), c = enPiso(ang, dist), dir = dirA(ang), lado = [-dir[2], 0, dir[0]];
  let cima = [c[0], -1e9, c[2]];
  for (let i = 0; i < 7; i++) {
    const t = (i / 6 - 0.5) * 2, alto = 6.5 * (1 - 0.55 * t * t) * (0.8 + 0.35 * rnd()), ancho = 3.2 + rnd() * 1.6;
    const p = suma(suma(c, escalar(lado, t * 7 + (rnd() - 0.5) * 1.5)), escalar(dir, (rnd() - 0.3) * 3));
    const P = piedra(M, mats, [p[0], NIVEL - alto * 0.3, p[2]], [ancho, alto, ancho * 0.9], rnd() * 6, semilla + i * 7, { div: 2, rugo: 0.22, pasto: NIVEL + alto * 0.35 });
    for (const q of P) if (q[1] > cima[1] && Math.abs(t) < 0.4) cima = q;
  }
  return cima;
}

// Faro blanco y rojo, con baranda y linterna.
function faro(M, mats, base) {
  const lados = 14, tramos = [[0, 0.8, 'blanco'], [0.8, 1.6, 'rojo'], [1.6, 2.4, 'blanco'], [2.4, 3.2, 'rojo'], [3.2, 4.4, 'blanco']];
  const radio = (y) => 0.72 - 0.065 * y;
  for (const [y0, y1, color] of tramos) M.tubo(mats[color], [suma(base, [0, y0, 0]), suma(base, [0, y1, 0])], [radio(y0), radio(y1)], lados, (y1 - y0) / 1.5);
  M.tubo(mats.negro, [suma(base, [0, 4.4, 0]), suma(base, [0, 4.55, 0])], [0.62, 0.62], lados);
  tapa(M, mats.negro, suma(base, [0, 4.55, 0]), 0.62, lados);
  M.tubo(mats.vidrio, [suma(base, [0, 4.55, 0]), suma(base, [0, 5.2, 0])], [0.36, 0.36], lados);
  M.tubo(mats.rojo, [suma(base, [0, 5.2, 0]), suma(base, [0, 5.75, 0])], [0.46, 0.02], lados);
  tapa(M, mats.rojo, suma(base, [0, 5.2, 0]), 0.46, lados, -1);
}
// Disco horizontal (tapa de un cilindro); abajo = -1 lo hace mirar para abajo.
function tapa(M, g, c, r, lados, abajo = 1) {
  const ids = Array.from({ length: lados }, (_, k) => { const a = (k / lados) * 2 * Math.PI; return M.vertice(suma(c, [Math.cos(a) * r, 0, Math.sin(a) * r]), [0, abajo, 0], [Math.cos(a) * 0.5 + 0.5, Math.sin(a) * 0.5 + 0.5]); });
  const m = M.vertice(c, [0, abajo, 0], [0.5, 0.5]);
  for (let k = 0; k < lados; k++) abajo > 0 ? g.tris.push(m, ids[(k + 1) % lados], ids[k]) : g.tris.push(m, ids[k], ids[(k + 1) % lados]);
}
// Caja orientada: centro, ejes (unitarios, con ex x ey = ez) y medias medidas; uv en unidades del mundo por cara.
// Cada cara se recorre con dos ejes B, C tales que B x C es su normal hacia afuera.
function bloque(M, g, c, [ex, ey, ez], [hx, hy, hz], esc = 0.5) {
  const menos = (a) => escalar(a, -1);
  for (const [n, h, B, hb, C, hc] of [[ex, hx, ey, hy, ez, hz], [menos(ex), hx, ez, hz, ey, hy], [ey, hy, ez, hz, ex, hx], [menos(ey), hy, ex, hx, ez, hz], [ez, hz, ex, hx, ey, hy], [menos(ez), hz, ey, hy, ex, hx]]) {
    const cc = suma(c, escalar(n, h)), q = (a, b) => suma(cc, suma(escalar(B, a * hb), escalar(C, b * hc)));
    M.cara(g, [q(-1, -1), q(1, -1), q(1, 1), q(-1, 1)], [[0, 0], [2 * hb * esc, 0], [2 * hb * esc, 2 * hc * esc], [0, 2 * hc * esc]]);
  }
}

// Palmera: tronco curvo que se inclina hacia `hacia` y una corona de pencas (dos medias tiras plegadas en V, de las
// dos caras) que salen hacia arriba y caen con el peso; abajo, unos cocos.
function palmera(M, F, mats, pie, { alto, hacia, inclina, pencas = 13, semilla }) {
  const rnd = azar(semilla), lean = unit([Math.cos(hacia * RAD), 0, Math.sin(hacia * RAD)]), puntos = [], radios = [];
  for (let i = 0; i <= 10; i++) {
    const t = i / 10, curva = inclina * alto * Math.pow(t, 1.7) + 0.15 * Math.sin(t * 5 + semilla);
    puntos.push(suma(pie, suma(escalar(lean, curva), [0.08 * Math.sin(t * 3 + semilla), alto * t * (1 - 0.12 * inclina * t), 0])));
    radios.push(0.2 * (1 - 0.42 * t) + 0.09 * Math.exp(-t * 14));
  }
  M.tubo(mats.corteza, puntos, radios, 8, alto / 1.1);
  const copa = puntos[10], eje = unit(resta(puntos[10], puntos[8]));
  for (let k = 0; k < 4; k++) { const a = rnd() * 2 * Math.PI, p = suma(copa, [Math.cos(a) * 0.22, -0.2 - rnd() * 0.15, Math.sin(a) * 0.22]); bolita(M, mats.coco, p, 0.13 + rnd() * 0.03); }
  for (let k = 0; k < pencas; k++) {
    const az = (k / pencas) * 2 * Math.PI + rnd() * 0.35, seca = k % 5 === 4;
    const elev = seca ? -60 : k % 3 === 0 ? 38 : k % 3 === 1 ? 12 : -12, largo = (seca ? 1.8 : 2.4 + rnd() * 0.9) * (alto / 7 + 0.35);
    const d0 = unit(suma(escalar([Math.cos(az), 0, Math.sin(az)], Math.cos((elev + rnd() * 10) * RAD)), escalar(eje, Math.sin((elev + rnd() * 10) * RAD))));
    penca(F, seca ? mats.pencaSeca : mats.penca[k % 3], suma(copa, escalar(d0, 0.12)), d0, largo, (seca ? 0.5 : 0.62) * (0.9 + 0.2 * rnd()), seca ? 0.15 : 0.55 + rnd() * 0.25);
  }
}
function bolita(M, g, c, r) {
  const { vs, fs } = ESFERA[0], ids = vs.map((v) => M.vertice(suma(c, escalar(v, r)), v, [v[0] * 0.5 + 0.5, v[1] * 0.5 + 0.5]));
  for (const [a, b, d] of fs) g.tris.push(ids[a], ids[b], ids[d]);
}
// Una penca: 8 tramos a lo largo; u de 0 a 1 de un borde al otro (el raquis en 0.5), v de 1 (pie) a 0 (punta),
// dentro de su columna de tex_f105.
function penca(F, { g, col }, p0, d0, largo, ancho, caida) {
  const n = 8, puntos = [];
  for (let i = 0; i <= n; i++) { const s = i / n; puntos.push(suma(p0, suma(escalar(d0, largo * s), [0, -caida * largo * s * s, 0]))); }
  for (const cara of [1, -1]) {
    const filas = puntos.map((p, i) => {
      const s = i / n, tg = unit(resta(puntos[Math.min(i + 1, n)], puntos[Math.max(i - 1, 0)]));
      const lado = unit(cruz(tg, [0, 1, 0])), arriba = unit(cruz(lado, tg)), w = ancho * Math.sin(Math.PI * Math.min(1, 0.08 + s * 0.95)) ** 0.8, pliegue = 0.45;
      const nrm = escalar(arriba, cara), v = (1 - s) * 0.5;
      const izq = suma(p, suma(escalar(lado, -w * Math.cos(pliegue)), escalar(arriba, -w * Math.sin(pliegue))));
      const der = suma(p, suma(escalar(lado, w * Math.cos(pliegue)), escalar(arriba, -w * Math.sin(pliegue))));
      return [F.vertice(izq, nrm, [col, v]), F.vertice(p, nrm, [col + 0.125, v]), F.vertice(der, nrm, [col + 0.25, v])];
    });
    for (let i = 0; i < n; i++) for (let j = 0; j < 2; j++) {
      const a = filas[i][j], b = filas[i][j + 1], c = filas[i + 1][j + 1], d = filas[i + 1][j];
      cara > 0 ? g.tris.push(a, b, c, a, c, d) : g.tris.push(a, c, b, a, d, c); // (a, b, c) mira hacia `arriba`
    }
  }
}

// Mata: dos o tres carteles cruzados con la textura de pasto o de arbusto (de las dos caras).
function mata(F, g, pie, alto, ancho, uv, giro, n = 2) {
  for (let k = 0; k < n; k++) {
    const a = giro + (k * Math.PI) / n, d = [Math.cos(a) * ancho / 2, 0, Math.sin(a) * ancho / 2], arriba = [0, alto, 0];
    const A = resta(pie, d), B = suma(pie, d), q = [suma(A, arriba), A, B, suma(B, arriba)], t = [[uv[0], uv[1]], [uv[0], uv[3]], [uv[2], uv[3]], [uv[2], uv[1]]];
    const ids = q.map((p, i) => F.vertice(p, [0, 1, 0], t[i]));
    g.tris.push(ids[0], ids[1], ids[2], ids[0], ids[2], ids[3], ids[0], ids[2], ids[1], ids[0], ids[3], ids[2]);
  }
}

// Médanos del lado de tierra: anillo de r 12.5 a 36 que sube de abajo del piso a lomas de 1 a 3 y vuelve a bajar.
const medano = (r, th) => {
  const t = thMar(th), lado = suave(MAR.a1 - 5, MAR.a1 + 25, t) * (1 - suave(MAR.a0 + 335, MAR.a0 + 365, t));
  const alto = 1.4 + 1.2 * fbm(th / 25, 0.5, 3, 151) + 0.5 * Math.sin(th * RAD * 7);
  return -0.06 + lado * (alto * suave(12.5, 19, r) * (1 - 0.6 * suave(24, 36, r)) + 0.25 * fbm(r / 3, th / 10, 3, 153) * suave(13, 16, r));
};
function medanos(M, g) {
  const radios = [12.5, 13.5, 14.5, 15.5, 17, 18.5, 20, 22, 24, 27, 31, 36], a0 = MAR.a1 - 8, a1 = MAR.a0 + 368, paso = 2.5, n = Math.round((a1 - a0) / paso), filas = [];
  const P = (r, th) => { const d = dirA(th); return [d[0] * r, medano(r, th), d[2] * r]; };
  for (const r of radios) filas.push(Array.from({ length: n + 1 }, (_, k) => {
    const th = a0 + k * paso, p = P(r, th), e = 0.2;
    const nrm = unit(cruz(resta(P(r, th + e * 5), P(r, th - e * 5)), resta(P(r + e, th), P(r - e, th))));
    return M.vertice(p, nrm[1] < 0 ? escalar(nrm, -1) : nrm, [p[0] / 5 - 0.5, 0.5 - p[2] / 5]);
  }));
  for (let i = 0; i + 1 < filas.length; i++) for (let k = 0; k < n; k++) {
    const a = filas[i][k], b = filas[i][k + 1], c = filas[i + 1][k + 1], d = filas[i + 1][k];
    const cr = cruz(resta(M.vs[b].p, M.vs[a].p), resta(M.vs[c].p, M.vs[a].p));
    cr[1] >= 0 ? g.tris.push(a, b, c, a, c, d) : g.tris.push(a, c, b, a, d, c);
  }
}

// Muelle de madera que entra al mar: tablas sobre pilotes.
function muelle(M, mats) {
  const g = mats.madera;
  const ang = 148, r0 = 10.4, r1 = 23.5, d = dirA(ang), lado = [-d[2], 0, d[0]], y = NIVEL + 0.55;
  const largo = r1 - r0, centro = suma(escalar(d, (r0 + r1) / 2), [0, y, 0]);
  bloque(M, g, centro, [d, [0, 1, 0], lado], [largo / 2, 0.06, 0.95], 0.5);
  for (let r = r0 + 0.4; r <= r1; r += 2.2) for (const s of [-0.85, 0.85]) {
    const p = suma(escalar(d, r), escalar(lado, s)), pie = Math.min(alturaPiso(r, ang), NIVEL) - 0.6;
    M.tubo(g, [[p[0], pie, p[2]], [p[0], y + 0.75, p[2]]], [0.09, 0.08], 6, 0.5);
  }
  for (const s of [-0.88, 0.88]) bloque(M, g, suma(suma(centro, escalar(lado, s)), [0, 0.7, 0]), [d, [0, 1, 0], lado], [largo / 2, 0.04, 0.04], 0.5); // barandas
  const p = suma(suma(escalar(d, r0 + 4.8), escalar(lado, 0.98)), [0, y + 0.45, 0]); // salvavidas colgado de la baranda
  rosca(M, [mats.rojo, mats.blanco], p, lado, 0.3, 0.09);
}
// Rosca (salvavidas): anillo de radio R y grosor r alrededor del eje `eje`, en gajos de dos colores.
function rosca(M, mats, c, eje, R, r) {
  const n = 16, m = 8, e1 = unit(cruz(eje, Math.abs(eje[1]) < 0.9 ? [0, 1, 0] : [1, 0, 0])), e2 = cruz(eje, e1);
  const P = (i, j) => { const a = (i / n) * 2 * Math.PI, b = (j / m) * 2 * Math.PI, rad = suma(escalar(e1, Math.cos(a)), escalar(e2, Math.sin(a))), nrm = suma(escalar(rad, Math.cos(b)), escalar(eje, Math.sin(b))); return [suma(c, suma(escalar(rad, R), escalar(nrm, r))), nrm]; };
  for (let i = 0; i < n; i++) for (let j = 0; j < m; j++) {
    const q = [P(i, j), P(i + 1, j), P(i + 1, j + 1), P(i, j + 1)], g = mats[Math.floor((i * 4) / n) % 2];
    const ids = q.map(([p, nn]) => M.vertice(p, nn, [0, 0])), cr = cruz(resta(q[1][0], q[0][0]), resta(q[2][0], q[0][0]));
    punto(cr, q[0][1]) > 0 ? g.tris.push(ids[0], ids[1], ids[2], ids[0], ids[2], ids[3]) : g.tris.push(ids[0], ids[2], ids[1], ids[0], ids[3], ids[2]);
  }
}
// Parador de paja: cuatro postes, techo cónico (de las dos caras) y un mostrador.
function parador(M, mats, ang, dist) {
  const c = enPiso(ang, dist), alto = 2.5;
  for (const [dx, dz] of [[-1, -1], [1, -1], [1, 1], [-1, 1]]) M.tubo(mats.madera, [suma(c, [dx * 1.3, -0.3, dz * 1.3]), suma(c, [dx * 1.3, alto + 0.2, dz * 1.3])], [0.09, 0.08], 6, 1.2);
  const lados = 12, R = 2.5, punta = suma(c, [0, alto + 1.4, 0]);
  const borde = Array.from({ length: lados + 1 }, (_, k) => { const a = (k / lados) * 2 * Math.PI; return suma(c, [Math.cos(a) * R, alto - 0.05 * Math.sin(3 * a), Math.sin(a) * R]); });
  for (let k = 0; k < lados; k++) {
    const A = borde[k], B = borde[k + 1], n = unit(cruz(resta(B, A), resta(punta, A))), arriba = n[1] > 0 ? n : escalar(n, -1);
    const uv = [[0, 1], [1.4, 1], [0.7, 0]], fuera = [A, B, punta].map((p, i) => M.vertice(p, arriba, uv[i])), dentro = [A, B, punta].map((p, i) => M.vertice(p, escalar(arriba, -1), uv[i]));
    if (n[1] > 0) mats.paja.tris.push(fuera[0], fuera[1], fuera[2], dentro[0], dentro[2], dentro[1]);
    else mats.paja.tris.push(fuera[0], fuera[2], fuera[1], dentro[0], dentro[1], dentro[2]);
  }
  bloque(M, mats.madera, suma(c, [0, 0.55, 1.05]), [[1, 0, 0], [0, 1, 0], [0, 0, 1]], [1.15, 0.55, 0.12], 0.8); // mostrador
  bloque(M, mats.madera, suma(c, [0, 1.12, 1.05]), [[1, 0, 0], [0, 1, 0], [0, 0, 1]], [1.25, 0.04, 0.22], 0.8);
}
// Sombrilla a gajos rojos y blancos, dos reposeras y un toallón.
function sombrilla(M, mats, ang, dist) {
  const c = enPiso(ang, dist), d = dirA(ang), lado = [-d[2], 0, d[0]], incl = unit([d[0] * 0.18, 1, d[2] * 0.18]), punta = suma(c, escalar(incl, 2.35));
  M.tubo(mats.blanco, [suma(c, [0, -0.3, 0]), punta], [0.035, 0.03], 6);
  const lados = 10, R = 1.5, centro = suma(c, escalar(incl, 1.95)), e1 = unit(cruz(incl, [1, 0, 0])), e2 = cruz(e1, incl);
  const borde = Array.from({ length: lados + 1 }, (_, k) => { const a = (k / lados) * 2 * Math.PI; return suma(centro, suma(escalar(e1, Math.cos(a) * R), escalar(e2, Math.sin(a) * R))); });
  for (let k = 0; k < lados; k++) {
    const g = k % 2 ? mats.blanco : mats.rojo, A = borde[k], B = borde[k + 1], n = unit(cruz(resta(B, A), resta(punta, A))), arriba = n[1] > 0 ? n : escalar(n, -1);
    const f = [A, B, punta].map((p) => M.vertice(p, arriba, [0, 0])), t = [A, B, punta].map((p) => M.vertice(p, escalar(arriba, -1), [0, 0]));
    if (n[1] > 0) g.tris.push(f[0], f[1], f[2], t[0], t[2], t[1]); else g.tris.push(f[0], f[2], f[1], t[0], t[1], t[2]);
  }
  for (const s of [-0.95, 0.95]) { // reposeras: base y respaldo
    const p = suma(c, suma(escalar(lado, s), escalar(d, -0.2))), ej = [d, [0, 1, 0], lado];
    bloque(M, mats.azul, suma(p, [0, 0.22, 0]), ej, [0.75, 0.04, 0.3], 1);
    const r = unit(suma(escalar(d, -0.55), [0, 0.83, 0])), respaldo = suma(p, suma(escalar(d, -0.75), escalar(r, 0.36)));
    bloque(M, mats.azul, suma(respaldo, [0, 0.22, 0]), [r, escalar(cruz(r, lado), -1), lado], [0.38, 0.04, 0.3], 1);
    for (const [u, v] of [[-0.7, -0.26], [0.7, -0.26], [-0.7, 0.26], [0.7, 0.26]]) { const q = suma(p, suma(escalar(d, u), escalar(lado, v))); M.tubo(mats.blanco, [suma(q, [0, -0.1, 0]), suma(q, [0, 0.2, 0])], [0.02, 0.02], 4); }
  }
  bloque(M, mats.turquesa, suma(c, suma(escalar(d, 1.2), [0, 0.03, 0])), [lado, [0, 1, 0], escalar(d, -1)], [0.45, 0.012, 0.9], 1); // toallón
  bolita(M, mats.amarillo, suma(c, suma(suma(escalar(d, 1.9), escalar(lado, -1.1)), [0, 0.18, 0])), 0.18); // pelota
}
// Bote de remos varado en la orilla: casco (media esfera estirada), borde de madera y dos bancos.
function bote(M, mats, ang, dist) {
  const c = enPiso(ang, dist, 0.42), largo = [Math.cos((ang + 70) * RAD), 0, Math.sin((ang + 70) * RAD)], ancho = [-largo[2], 0, largo[0]];
  const { vs, fs } = ESFERA[1], P = vs.map((v) => [v[0], Math.min(v[1], 0.15), v[2]]), giro = (p) => suma(c, suma(escalar(largo, p[0] * 1.7 * (1 - 0.25 * Math.max(0, p[0]))), suma(escalar(ancho, p[2] * 0.62), [0, p[1] * 0.5 + 0.06 * p[0], 0])));
  for (const [a, b, d] of fs) {
    const q = [a, b, d].map((i) => giro(P[i])), n = unit(cruz(resta(q[1], q[0]), resta(q[2], q[0]))), g = Math.max(vs[a][1], vs[b][1], vs[d][1]) > 0.14 ? mats.madera : mats.bote;
    g.tris.push(...q.map((p) => M.vertice(p, n, [p[0] * 0.8, p[2] * 0.8])));
  }
  for (const u of [-0.5, 0.45]) bloque(M, mats.madera, suma(giro([u, 0.15, 0]), [0, 0.02, 0]), [ancho, [0, 1, 0], escalar(largo, -1)], [0.55, 0.03, 0.12], 1);
}

// Velero lejano: casco y dos velas.
function velero(M, mats, ang, dist) {
  const d = dirA(ang + 90), c = suma(escalar(dirA(ang), dist), [0, NIVEL + 0.15, 0]);
  bloque(M, mats.blanco, c, [d, [0, 1, 0], [-d[2], 0, d[0]]], [0.9, 0.18, 0.28], 1);
  M.tubo(mats.negro, [suma(c, [0, 0.1, 0]), suma(c, [0, 2.2, 0])], [0.03, 0.025], 4);
  const vela = (a, b, p) => { for (const s of [1, -1]) { const q = s > 0 ? [a, b, p] : [a, p, b]; M.cara(mats.blanco, q, [[0, 0], [1, 0], [0.5, 1]]); } };
  vela(suma(c, [0, 0.3, 0]), suma(c, suma(escalar(d, 0.75), [0, 0.3, 0])), suma(c, [0, 2.15, 0]));
  vela(suma(c, [0, 0.4, 0]), suma(c, suma(escalar(d, -0.6), [0, 0.35, 0])), suma(c, [0, 1.7, 0]));
}

// Todo lo sólido (iluminado) en un modelo, los promontorios en otro (no entran: máx. 65535 vértices) y el follaje
// (prueba de alfa, de las dos caras) en un tercero.
function cosas() {
  const M = new Malla(), P = new Malla(), F = new Malla(), SIN = [0, 0, 0, 0, 0, 0, 0, 0, 0];
  const m = (tex, col = [1, 1, 1, 1], malla = M) => malla.material(tex, col, SIN);
  const mats = {
    seca: m('tex_f107.bmt'), mojada: m('tex_f107.bmt', [0.62, 0.67, 0.58, 1]), pasto: m('tex_f111.bmt'),
    blanco: m('tex_f110.bmt', [0.97, 0.97, 0.95, 1]), rojo: m('tex_f110.bmt', [0.82, 0.16, 0.13, 1]), negro: m('tex_f110.bmt', [0.16, 0.17, 0.19, 1]),
    vidrio: m('tex_f110.bmt', [0.95, 0.93, 0.7, 1]), corteza: m('tex_f106.bmt'), coco: m('tex_f110.bmt', [0.35, 0.42, 0.2, 1]),
    madera: m('tex_f108.bmt'), paja: m('tex_f109.bmt'), arena: m('tex_f104.bmt'),
    azul: m('tex_f110.bmt', [0.12, 0.45, 0.78, 1]), turquesa: m('tex_f110.bmt', [0.1, 0.72, 0.72, 1]), amarillo: m('tex_f110.bmt', [0.98, 0.78, 0.2, 1]),
    bote: m('tex_f108.bmt', [0.55, 0.75, 0.95, 1]),
    penca: [0, 1, 2].map((k) => ({ g: F.material('tex_f105.bmt', [1, 1, 1, 1], SIN), col: k * 0.25 })),
  };
  mats.pencaSeca = { g: mats.penca[0].g, col: 0.75 }; // la columna 3 es la penca seca
  medanos(M, mats.arena);
  const piedrasP = { seca: m('tex_f107.bmt', [1, 1, 1, 1], P), mojada: m('tex_f107.bmt', [0.62, 0.67, 0.58, 1], P), pasto: m('tex_f111.bmt', [1, 1, 1, 1], P) };
  for (const [i, p] of PROMONTORIOS.entries()) {
    const cima = promontorio(P, piedrasP, p, 500 + i * 50);
    if (p.faro) faro(M, mats, [cima[0], cima[1] - 0.25, cima[2]]);
  }
  const rnd = azar(611);
  for (const [ang, dist, n] of [[61, 12.3, 4], [127, 12.6, 3], [30, 14.5, 2], [168, 13.5, 2], [-8, 17, 3]]) { // piedras de la orilla
    for (let k = 0; k < n; k++) {
      const s = 0.45 + rnd() * 1.1, p = enPiso(ang + (rnd() - 0.5) * 6, dist + (rnd() - 0.5) * 1.5);
      piedra(M, mats, [p[0], Math.max(p[1], NIVEL - 0.2) - s * 0.3, p[2]], [s * 1.2, s * 0.8, s], rnd() * 6, 700 + k * 13 + dist, { rugo: 0.3 });
    }
  }
  muelle(M, mats);
  sombrilla(M, mats, 236, 10.8); bote(M, mats, 171, 10.9);
  parador(M, mats, 252, 15.5);
  velero(M, mats, 97, 31);
  for (const [i, [ang, dist, alto, hacia, inclina]] of [[118, 8.7, 4.6, 100, 0.5], [53, 9.3, 4.2, 40, 0.45], [157, 10.4, 7.2, 170, 0.3], [-7, 13.5, 6.2, -20, 0.3], [214, 13, 7.6, 200, 0.18], [231, 16.5, 6, 250, 0.22], [266, 13.2, 8, 280, 0.14], [291, 15, 6.6, 300, 0.25], [318, 12.8, 7.1, 330, 0.2], [337, 16.5, 5.6, 350, 0.3]].entries()) {
    palmera(M, F, mats, enPiso(ang, dist, -0.15), { alto, hacia, inclina, semilla: 900 + i * 31 });
  }
  const pasto = F.material('tex_f105.bmt', [1, 1, 1, 1], SIN), r2 = azar(733);
  for (let i = 0; i < 170; i++) { // pasto sobre los médanos
    const th = MAR.a1 + 4 + r2() * (360 - (MAR.a1 - MAR.a0) - 8), r = 13.2 + Math.pow(r2(), 0.7) * 11, d = dirA(th), y = medano(r, th);
    const k = r2() < 0.5 ? 0 : 0.25;
    mata(F, pasto, [d[0] * r, y - 0.05, d[2] * r], 0.7 + r2() * 0.6, 1 + r2() * 0.6, [k, 0.505, k + 0.25, 0.75], r2() * Math.PI);
  }
  for (let i = 0; i < 26; i++) { // arbustos al pie de las palmeras de tierra y en los promontorios
    const th = MAR.a1 + 6 + r2() * (360 - (MAR.a1 - MAR.a0) - 12), r = 13 + r2() * 6, d = dirA(th), y = medano(r, th);
    mata(F, pasto, [d[0] * r, y - 0.1, d[2] * r], 1.1 + r2() * 0.8, 1.6 + r2() * 0.8, [0.5, 0.5, 1, 1], r2() * Math.PI, 3);
  }
  return { M, P, F };
}

// ---------- la rueda de selección ----------
// Las letras se recortan de los otros nombres de tex_sys24 ([fila, x0, x1] de su trazo blanco): salen con la letra
// del juego. A cada letra le toca la tinta (y la sombra) más cercana a su trazo.
const LETRAS = { S: [1, 158, 170], u: [0, 200, 211], n: [4, 160, 173], l: [0, 189, 197], i: [2, 200, 208], t: [0, 69, 75], h: [2, 185, 198], o: [0, 41, 53], r: [0, 104, 114], e: [0, 141, 152] };
export const NOMBRE = 'Sunlit Shore';

export function letra(t, [fila, x0, x1]) {
  const W = t.w, y0 = fila * 32, al = (x, y) => t.rgba[4 * ((y0 + y) * W + x) + 3];
  const blanco = (x, y) => al(x, y) > 128 && t.rgba[4 * ((y0 + y) * W + x)] > 140;
  const comp = new Int32Array(W * 32).fill(-1), cajas = [];
  for (let y = 0; y < 32; y++) for (let x = 0; x < W; x++) { // trazos blancos conexos
    if (comp[y * W + x] >= 0 || !blanco(x, y)) continue;
    const k = cajas.length, caja = [x, x], pila = [[x, y]]; comp[y * W + x] = k; cajas.push(caja);
    while (pila.length) {
      const [a, b] = pila.pop(); caja[0] = Math.min(caja[0], a); caja[1] = Math.max(caja[1], a);
      for (let dy = -1; dy <= 1; dy++) for (let dx = -1; dx <= 1; dx++) {
        const u = a + dx, v = b + dy;
        if (u >= 0 && v >= 0 && u < W && v < 32 && comp[v * W + u] < 0 && blanco(u, v)) { comp[v * W + u] = k; pila.push([u, v]); }
      }
    }
  }
  const mias = new Set(cajas.map((c, k) => (c[0] >= x0 - 1 && c[1] <= x1 + 1 ? k : -1)).filter((k) => k >= 0));
  const asignado = Int32Array.from(comp), cola = []; // la tinta va al trazo más cercano (búsqueda en anchura)
  for (let i = 0; i < W * 32; i++) if (asignado[i] >= 0) cola.push(i);
  for (let q = 0; q < cola.length; q++) {
    const i = cola[q], x = i % W, y = (i / W) | 0;
    for (const [dx, dy] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) {
      const u = x + dx, v = y + dy, j = v * W + u;
      if (u >= 0 && v >= 0 && u < W && v < 32 && asignado[j] < 0 && al(u, v) > 0) { asignado[j] = asignado[i]; cola.push(j); }
    }
  }
  const px = [];
  for (let i = 0; i < W * 32; i++) if (mias.has(asignado[i])) px.push({ x: i % W, y: (i / W) | 0, c: [0, 1, 2, 3].map((k) => t.rgba[4 * (y0 * W + i) + k] / 255) });
  const xs = [...mias].flatMap((k) => cajas[k]);
  return { px, x0: Math.min(...xs), x1: Math.max(...xs) };
}
// Una fila de 256x32 con el texto centrado como los demás nombres (centro en x 129.5).
export function fila(t, texto, separacion = 2, espacio = 9) {
  const L = new Lienzo(256, 32), puestas = [];
  let x = 0;
  for (const ch of texto) {
    if (ch === ' ') { x += espacio; continue; }
    const l = letra(t, LETRAS[ch]);
    puestas.push([l, x - l.x0]); x += l.x1 - l.x0 + 1 + separacion;
  }
  const corrida = Math.round(129.5 - (x - separacion) / 2);
  for (const [l, dx] of puestas) for (const p of l.px) L.sobre(p.x + dx + corrida, p.y, p.c.slice(0, 3), p.c[3]);
  return L;
}

// Nombres (256x512, filas de 32): 0-6 los del juego, 7 la playa, 8 Random y la 15 otra vez Random.
function nombres() {
  const t = leerBmt(leer('obj/tex_sys24.bmt')), L = new Lienzo(256, 512);
  const copiar = (yo, yd, h) => { for (let y = 0; y < h; y++) for (let x = 0; x < 256; x++) L.d.set([0, 1, 2, 3].map((k) => t.rgba[4 * ((yo + y) * 256 + x) + k] / 255), 4 * ((yd + y) * 256 + x)); };
  copiar(0, 0, 7 * 32); copiar(7 * 32, 8 * 32, 32); copiar(7 * 32, 15 * 32, 32);
  L.d.set(fila(t, NOMBRE).d, 4 * LUGAR * 32 * 256);
  textura('tex_sys24', L);
}

// Miniaturas (512x1024, franjas de 64): 0-6 las del juego (con la de Outskirt Hill de noche), 7 la playa con el
// alfa de las otras, 8 Random, la 9 repite la 0 y la 15 Random (el anterior de la 0 y el siguiente de Random).
function miniaturas(e) {
  const t = leerBmt(leer('obj/tex_sys42.bmt')), L = new Lienzo(512, 1024);
  const copiar = (yo, yd, h) => { for (let y = 0; y < h; y++) for (let x = 0; x < 512; x++) L.d.set([0, 1, 2, 3].map((k) => t.rgba[4 * ((yo + y) * 512 + x) + k] / 255), 4 * ((yd + y) * 512 + x)); };
  copiar(0, 0, 448); copiar(192, 448, 64); copiar(448, 512, 64); copiar(0, 576, 64); copiar(448, 960, 64);
  const img = render(e, 1024, 256, { pos: [0, 1.1, -5.7], rumbo: 82, cabeceo: 1, fovV: 17 });
  for (let y = 0; y < 64; y++) for (let x = 0; x < 256; x++) { // promedio de 4x4
    const c = [0, 0, 0];
    for (let dy = 0; dy < 4; dy++) for (let dx = 0; dx < 4; dx++) for (let k = 0; k < 3; k++) c[k] += Math.min(1, img[3 * ((4 * y + dy) * 1024 + 4 * x + dx) + k]) / 16;
    L.d.set(c, 4 * ((448 + y) * 512 + x));
  }
  textura('tex_sys42', L);
}

// Vista del escenario sin HUD ni personajes (el rasterizador de vista.mjs).
function render(e, w, h, cam) {
  const tex = (n) => generadas.get(n) || leerBmt(leer(`obj/${n}`));
  const mods = e.modelos.map((m) => ({ ...m, nmd: modelos.get(m.nombre.replace('.nmd', '')) }));
  const cache = new Map(), texturas = (n) => (cache.has(n) ? cache.get(n) : cache.set(n, tex(n)).get(n));
  return vista({ w, h, modelos: mods, texturas, luz: { difusa: e.difusa, ambiente: e.ambiente, dir: e.dir }, cam });
}

export function principal() {
  noche();
  const v2 = readFileSync(join(SCRATCH, 'mods', 'vsobj2.dat'));
  for (const x of leerDat1(v2)) base.set(x.nombre, v2.subarray(x.offset, x.offset + x.tam));
  if (PNG) mkdirSync(PNG, { recursive: true });

  cielo(); sol(); mar(); arena(); hojas(); corteza(); roca(); madera(); paja(); pintura(); verde();
  const { M, P, F } = cosas();
  modelo(piso().aNmd('mod_f100'));
  modelo(domo('tex_f100.bmt', 'tex_f101.bmt').aNmd('mod_f101'));
  modelo(solQuad().aNmd('mod_f102'));
  modelo(marMalla().aNmd('mod_f103'));
  modelo(M.aNmd('mod_f104'));
  modelo(F.aNmd('mod_f105'));
  modelo(P.aNmd('mod_f106'));

  const campo = leerCampo(leer('dat/field.dat'));
  if (campo.length !== ESCENARIO) throw new Error(`field.dat tiene ${campo.length} escenarios (se esperaban ${ESCENARIO})`);
  const C = (nombre, tipo, z, escribeZ, mezcla, pruebaAlfa = 0) => ({ nombre: `${nombre}.nmd`, tipo, z, escribeZ, mezcla, pruebaAlfa });
  const e = {
    borrado: [0, 0, 0, 0], escala: 0.01,
    modelos: [
      C('mod_f100', 3, 1, 1, 0), // piso: el primero, su textura se corre con la pelea
      C('mod_f101', 3, 1, 0, 0), // cielo
      C('mod_f102', 3, 1, 0, 3), // sol (se suma)
      C('mod_f104', 1, 1, 1, 0), // médanos, piedras, faro, muelle, parador, velero y troncos (iluminados)
      C('mod_f106', 1, 1, 1, 0), // promontorios
      C('mod_f103', 3, 1, 0, 2), // mar, después de lo sólido: lo que queda bajo el agua se ve a través
      C('mod_f105', 1, 1, 1, 2, 1), // pencas, pasto y arbustos: prueba de alfa y escriben Z
    ],
    efecto: 0, efecto2: 0, difusa: [1.18, 1.1, 0.96, 1], ambiente: [0.6, 0.64, 0.72, 1], dir: [-0.2, -0.55, 0.35], viento: [3, 0, -6],
  };
  campo.push(e);
  salidas.set('dat/field.dat', escribirCampo(campo));
  nombres(); miniaturas(e);
  if (PNG) for (const [nombre, cam] of [['vista-playa', [0, 1, -5.7, 90, 0]], ['vista-izq', [0, 1, -5.7, 140, 1]], ['vista-der', [0, 1, -5.7, 25, 1]], ['vista-atras', [0, 1, 5.7, 270, 1]]]) {
    const ang = cam[3], pos = [-Math.cos(ang * RAD) * 5.7, cam[1], -Math.sin(ang * RAD) * 5.7];
    const G = render(e, 1332, 600, { pos, rumbo: ang, cabeceo: cam[4], fovV: 30.6 }), V = new Lienzo(1332, 600);
    for (let i = 0; i < 1332 * 600; i++) V.d.set([Math.min(1, G[3 * i]), Math.min(1, G[3 * i + 1]), Math.min(1, G[3 * i + 2]), 1], 4 * i);
    writeFileSync(join(PNG, `${nombre}.png`), V.png());
  }

  const todo = [...[...salidas].map(([nombre, datos]) => ({ nombre, datos })), ...[...base].filter(([n]) => !salidas.has(n)).map(([nombre, datos]) => ({ nombre, datos }))];
  const destino = join(SCRATCH, 'mods', 'vsobj2.dat');
  writeFileSync(destino, escribirDat1(todo));
  console.log(`playa: ${salidas.size} archivos (${[...modelos].map(([n, m]) => `${n} ${m.vs.length}v`).join(', ')}) -> ${destino}`);
}

if (import.meta.url === `file:///${process.argv[1]?.replaceAll('\\', '/')}`) principal();
