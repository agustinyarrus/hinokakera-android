// Outskirt Hill de noche, al estilo de Moonlit Wilderness (Tekken 7): luna llena, ruinas góticas en una loma,
// árbol seco, monolito caído y campo de cortaderas. Lee los originales de tu copia del juego y deja
// <scratch>/mods/vsobj2.dat: el parche del juego más lo nuevo (el juego busca primero en vsobj2.dat).
// Uso: node tools/escenario/moonlit.mjs [--png carpeta]   (--png guarda las texturas para mirarlas)
import { openSync, readSync, closeSync, writeFileSync, mkdirSync, readFileSync } from 'node:fs';
import { join } from 'node:path';
import { JUEGO, SCRATCH } from '../datos.mjs';
import { escribirDat1 } from '../dat1.mjs';
import { leerNmd, escribirNmd } from './nmd.mjs';
import { leerBmt } from './bmt.mjs';
import { leerCampo, escribirCampo } from './campo.mjs';
import { Lienzo, azar, fbm, perlin, sat, suave, mezclar, hex, trazo, poligono, desenfocar } from './pintura.mjs';
import { Malla, rotY, rotZ, rotX, unit, cruz, resta, suma, escalar } from './geometria.mjs';
import { vista } from './vista.mjs';

const ARCHIVOS = ['vsobj2.dat', 'vsobj.dat', 'vsdat.dat']; // orden en que el juego los busca
const PNG = process.argv.includes('--png') ? process.argv[process.argv.indexOf('--png') + 1] : null;
const ESCENARIO = 3; // Outskirt Hill
const RAD = Math.PI / 180;

// Hacia dónde queda cada cosa, en grados desde +X hacia +Z (la cámara arranca mirando a +Z, a 90°).
export const LUNA = { ang: 100, elev: 8.5, dist: 38, radio: 4.3 };
export const CASTILLO = { ang: 64, dist: 23.5 };
export const MONOLITO = { ang: 82, dist: 15.5 };
export const ARBOL = { ang: 117, dist: 14.8 };

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
// Como leerDat1 pero solo con la tabla (los dat grandes no se leen enteros).
function leerDat1Tabla(t) {
  const n = t.readUInt32LE(4), r = [];
  for (let i = 0, p = 8; i < n && p + 4 <= t.length; i++) {
    const l = t.readUInt32LE(p); r.push({ nombre: t.toString('latin1', p + 4, p + 4 + l).replace(/\0.*$/s, ''), offset: t.readUInt32LE(p + 4 + l), tam: t.readUInt32LE(p + 8 + l) });
    p += 12 + l;
  }
  return r;
}

const salidas = []; // [{nombre, datos}]
const generadas = new Map(); // textura -> RGBA8, para la vista
function textura(nombre, L) {
  generadas.set(`${nombre}.bmt`, { w: L.w, h: L.h, rgba: L.rgba8() });
  salidas.push({ nombre: `obj/${nombre}.bmt`, datos: L.bmt() });
  if (PNG) writeFileSync(join(PNG, `${nombre}.png`), L.png());
}

// ---------- cielo: panorama de 4096x1024 (x = ángulo, y = elevación) en dos mitades de 2048 ----------
// La elevación no es lineal: de -10° a 30° ocupa 3/4 de la textura, que es lo que ve la cámara.
export const elevAV = (e) => (e <= 30 ? 1 - ((e + 10) / 40) * 0.75 : 0.25 - ((e - 30) / 60) * 0.25);
export const vAElev = (v) => (v >= 0.25 ? -10 + ((1 - v) / 0.75) * 40 : 30 + ((0.25 - v) / 0.25) * 60);

function cielo() {
  const W = 4096, H = 1024, L = new Lienzo(W, H), rnd = azar(7);
  const cenit = hex('#050a1f'), alto = hex('#0b1638'), medio = hex('#16285a'), bajo = hex('#243c6e'), horizonte = hex('#2f4878');
  const lunaDir = [Math.cos(LUNA.elev * RAD) * Math.cos(LUNA.ang * RAD), Math.sin(LUNA.elev * RAD), Math.cos(LUNA.elev * RAD) * Math.sin(LUNA.ang * RAD)];
  const xm = (LUNA.ang / 360) * W, ym = elevAV(LUNA.elev) * H;
  const nube = (x, y) => { // densidad 0..1, periódica en x (los períodos del ruido tienen que ser enteros)
    const e = vAElev(y / H), aplana = 1 + 0.5 * sat((12 - e) / 15); // algo más chatas cerca del horizonte
    const n = fbm(x / 512, (y / 288) * aplana, 5, 3, 8, 0, 0.5) * 0.9 + 0.4 * perlin(x / 1024, y / 700, 9, 4);
    return suave(0.0, 0.4, n) * suave(2, 8, e) * (1 - 0.7 * suave(25, 55, e));
  };
  L.cada((x, y) => {
    const e = vAElev((y + 0.5) / H), th = ((x + 0.5) / W) * 360 * RAD;
    const dir = [Math.cos(e * RAD) * Math.cos(th), Math.sin(e * RAD), Math.cos(e * RAD) * Math.sin(th)];
    const ang = Math.acos(sat(dir[0] * lunaDir[0] + dir[1] * lunaDir[1] + dir[2] * lunaDir[2])) / RAD;
    let c = e < 0 ? mezclar(horizonte, hex('#22355f'), sat(-e / 10)) : e < 6 ? mezclar(horizonte, bajo, e / 6) : e < 20 ? mezclar(bajo, medio, (e - 6) / 14) : e < 45 ? mezclar(medio, alto, (e - 20) / 25) : mezclar(alto, cenit, (e - 45) / 45);
    const halo = 0.3 * Math.exp(-((ang / 14) ** 2)) + 0.18 * Math.exp(-((ang / 40) ** 2));
    c = c.map((v, k) => v + halo * [0.42, 0.55, 0.8][k]);
    // nubes: núcleo oscuro y borde plateado del lado de la luna
    const d = nube(x, y);
    if (d > 0.001) {
      let mx = xm - x; if (mx > W / 2) mx -= W; if (mx < -W / 2) mx += W; // hacia la luna en la imagen
      const my = ym - y, l = Math.hypot(mx, my) || 1, dl = nube(x + (mx / l) * 6, y + (my / l) * 6);
      const borde = sat((d - dl) * 4 + 0.15) * (0.45 + 1.4 * Math.exp(-((ang / 30) ** 2)));
      const nucleo = mezclar(hex('#141d38'), hex('#2b3b66'), sat(1 - d) * 0.6 + halo);
      const lit = mezclar(nucleo, hex('#c9d6f0'), sat(borde));
      c = mezclar(c, lit, sat(d * 1.15));
    }
    return [...c, 1];
  });
  // estrellas donde no hay nubes ni resplandor de la luna
  for (let i = 0; i < 2600; i++) {
    const x = rnd() * W, y = rnd() * H * 0.86, e = vAElev(y / H);
    if (e < 3) continue;
    const libre = 1 - sat(nube(Math.floor(x), Math.floor(y)) * 3);
    const b = libre * suave(3, 14, e) * (0.25 + 0.75 * rnd() ** 3);
    if (b < 0.05) continue;
    const r = 0.45 + rnd() * 0.5 * (b > 0.6 ? 1.6 : 1);
    trazo(L, x, y, r, x, y, r, mezclar([1, 1, 1], hex('#bcd2ff'), rnd()), b);
  }
  textura('tex_f090', L.recorte(0, 0, 2048, 1024));
  textura('tex_f091', L.recorte(2048, 0, 2048, 1024));
}

// Domo de radio 40: 64 lados, la mitad de 0° a 180° con tex_f090 y la otra con tex_f091.
export function domo(texA = 'tex_f090.bmt', texB = 'tex_f091.bmt') {
  const M = new Malla(), elev = [-10, -4, 0, 2, 4, 6, 8, 10, 13, 16, 20, 25, 30, 40, 52, 66, 80, 90], R = 40;
  for (const [tex, a0] of [[texA, 0], [texB, 180]]) {
    const g = M.material(tex), lados = 32, ids = [];
    for (const e of elev) {
      ids.push(Array.from({ length: lados + 1 }, (_, k) => {
        const th = (a0 + (k / lados) * 180) * RAD, p = [R * Math.cos(e * RAD) * Math.cos(th), R * Math.sin(e * RAD), R * Math.cos(e * RAD) * Math.sin(th)];
        return M.vertice(p, unit(escalar(p, -1)), [k / lados, elevAV(e)]);
      }));
    }
    for (let i = 0; i + 1 < ids.length; i++) for (let k = 0; k < lados; k++) {
      const a = ids[i][k], b = ids[i][k + 1], c = ids[i + 1][k + 1], d = ids[i + 1][k];
      g.tris.push(a, b, c, a, c, d); // normal hacia adentro: se ve desde el centro
    }
  }
  return M;
}

// ---------- luna: disco opaco con mares y halo semitransparente (mezcla alfa) ----------
function luna() {
  const N = 512, L = new Lienzo(N, N), rd = N * 0.2, c0 = N / 2;
  L.cada((x, y) => {
    const dx = (x + 0.5 - c0) / rd, dy = (y + 0.5 - c0) / rd, r = Math.hypot(dx, dy);
    const fuera = Math.max(0, r - 1), halo = r > 1 ? 0.32 * Math.exp(-fuera / 0.16) + 0.22 * Math.exp(-fuera / 0.85) : 0;
    const borde = sat(Math.min(c0 - Math.abs(x + 0.5 - c0), c0 - Math.abs(y + 0.5 - c0)) / (N * 0.06)); // a negro antes del borde del quad
    let col = [0.78, 0.86, 1], alfa = sat(halo * 1.6) * borde;
    const cob = sat((1 - r) * rd + 0.5);
    if (cob > 0) {
      const z = Math.sqrt(Math.max(0, 1 - r * r)), mar = suave(0.02, 0.3, fbm(dx * 1.4 + 3, dy * 1.4 + 7, 5, 21)) * 0.42;
      const crater = 0.1 * fbm(dx * 11, dy * 11, 3, 33), luz = 0.78 + 0.22 * z;
      const disco = mezclar([0.97, 0.98, 1], [0.56, 0.59, 0.66], sat(mar + crater)).map((v) => v * luz);
      col = mezclar(col, disco, cob); alfa = alfa + (1 - alfa) * cob;
    }
    return [...col, alfa];
  });
  textura('tex_f092', L);
}

function lunaQuad() {
  const M = new Malla(), g = M.material('tex_f092.bmt');
  const d = [Math.cos(LUNA.elev * RAD) * Math.cos(LUNA.ang * RAD), Math.sin(LUNA.elev * RAD), Math.cos(LUNA.elev * RAD) * Math.sin(LUNA.ang * RAD)];
  const c = escalar(d, LUNA.dist), der = unit(cruz([0, 1, 0], d)), arr = cruz(d, der);
  const s = (LUNA.dist * Math.tan(LUNA.radio * RAD)) / 0.4; // el disco ocupa 0.4 del ancho de la textura
  const P = (u, v) => suma(c, suma(escalar(der, (u - 0.5) * 2 * s), escalar(arr, (0.5 - v) * 2 * s)));
  M.cara(g, [P(0, 0), P(1, 0), P(1, 1), P(0, 1)], [[0, 0], [1, 0], [1, 1], [0, 1]]); // normal hacia el centro
  return M;
}

// ---------- texturas que salen de las originales ----------
const gradiente = (stops, t) => {
  for (let i = 1; i < stops.length; i++) if (t <= stops[i][0]) return mezclar(stops[i - 1][1], stops[i][1], sat((t - stops[i - 1][0]) / (stops[i][0] - stops[i - 1][0])));
  return stops[stops.length - 1][1];
};
const luminancia = (r, i) => (0.3 * r[i] + 0.59 * r[i + 1] + 0.11 * r[i + 2]) / 255;

// Pasto de noche: el mismo dibujo que el original, en grises azulados de luna.
function deNoche({ w, h, rgba }, stops, nombre) {
  const L = new Lienzo(w, h);
  L.cada((x, y) => { const i = 4 * (y * w + x); return [...gradiente(stops, luminancia(rgba, i)), rgba[i + 3] / 255]; });
  textura(nombre, L);
}
const PASTO = [[0.2, hex('#090e17')], [0.38, hex('#18222f')], [0.5, hex('#2b3746')], [0.62, hex('#465564')], [0.8, hex('#7a8a98')]];

// Montes (arriba) de noche con el filo iluminado, y abajo lomas lejanas con ruinas en vez de la ciudad.
function montes(bma) {
  const w = bma.readUInt16LE(4), h = bma.readUInt16LE(6), L = new Lienzo(w, h), rnd = azar(5);
  const px = (x, y) => bma.readUInt32LE(8 + 4 * (y * w + x));
  for (let y = 0; y < h / 2; y++) for (let x = 0; x < w; x++) {
    const c = px(x, y), l = (0.3 * ((c >>> 16) & 255) + 0.59 * ((c >>> 8) & 255) + 0.11 * (c & 255)) / 255;
    L.d.set([...gradiente([[0.35, hex('#0a1126')], [0.6, hex('#16244a')], [0.85, hex('#2e4373')], [1, hex('#425a8c')]], l), suave(0.12, 0.45, (c >>> 24) / 255)], 4 * (y * w + x)); // sin los píxeles sueltos casi transparentes
  }
  for (let x = 0; x < w; x++) { // filo de luna: los primeros píxeles opacos de cada columna
    let y = 0; while (y < h / 2 && L.d[4 * (y * w + x) + 3] < 0.5) y++;
    for (let k = 0; k < 4 && y + k < h / 2; k++) L.sumar(x, y + k, [0.16, 0.2, 0.28], 1 - k / 4);
  }
  // abajo: lomas y ruinas recortadas contra el cielo (la mitad de abajo se usa entera en cada cartel)
  const y0 = h / 2 + 6, alto = h / 2 - 6, oscuro = hex('#0c1329');
  const piso = (x) => y0 + alto * (0.58 - 0.16 * fbm(x / 300, 0.5, 4, 8) - 0.1 * Math.sin((x / w) * Math.PI));
  const perfil = [];
  for (let x = 0; x <= w; x += 8) perfil.push([x, piso(x)]);
  poligono(L, [[w, h], [0, h], ...perfil], hex('#111b36'));
  const ruina = (x, ancho, alt, arcos) => {
    const b = piso(x + ancho / 2) + 4, pts = [[x, b]];
    for (let k = 0; k < arcos; k++) { // los vanos se dibujan con y hacia arriba y se dan vuelta
      const xc = x + ((k + 0.5) / arcos) * ancho, a = (ancho / arcos) * 0.28;
      pts.push([xc - a, b], ...arcoPuntiagudo(xc, a, -(b - alt * 0.38), -(b - alt * 0.55), 5).map(([u, v]) => [u, -v]), [xc + a, b]);
    }
    pts.push([x + ancho, b]);
    for (let k = 10; k >= 0; k--) pts.push([x + (ancho * k) / 10, b - alt * (0.75 + 0.25 * rnd()) * (k % 3 === 0 ? 0.85 : 1)]);
    poligono(L, pts, oscuro);
  };
  ruina(140, 150, 70, 3); ruina(430, 60, 120, 1); ruina(560, 210, 60, 4); ruina(860, 90, 85, 2);
  textura('tex_f095', L);
}

// ---------- árboles secos (mismo reparto que tex_f052: uno solo arriba a la izquierda, hilera abajo) ----------
function rama(L, x, y, ang, largo, grosor, nivel, rnd, col) {
  let px = x, py = y, a = ang;
  for (let s = 0; s < 3; s++) {
    a += (rnd() - 0.5) * 0.4;
    const nx = px + (Math.cos(a) * largo) / 3, ny = py - (Math.sin(a) * largo) / 3;
    trazo(L, px, py, grosor * (1 - s * 0.09), nx, ny, grosor * (1 - (s + 1) * 0.09), col);
    px = nx; py = ny;
  }
  if (nivel === 0 || largo < 5) return;
  const hijos = 2 + (rnd() < 0.35 ? 1 : 0);
  for (let k = 0; k < hijos; k++) {
    const da = (k - (hijos - 1) / 2) * (0.5 + rnd() * 0.35) + (rnd() - 0.5) * 0.3;
    rama(L, px, py, a + da + (Math.PI / 2 - a) * 0.12, largo * (0.6 + rnd() * 0.2), grosor * 0.64, nivel - 1, rnd, col);
  }
}
// Dos pasadas con la misma semilla: un filo claro corrido hacia la luna y encima la silueta oscura.
function arbolSeco(L, x, y, largo, grosor, nivel, semilla, wrap = 0) {
  for (const [dx, dy, col] of [[-1.3, -0.9, hex('#3d4c66')], [0, 0, hex('#0f141d')]])
    for (const o of wrap ? [-wrap, 0, wrap] : [0]) rama(L, x + dx + o, y + dy, Math.PI / 2 + (azar(semilla)() - 0.5) * 0.2, largo, grosor, nivel, azar(semilla), col);
}
function arboles() {
  const L = new Lienzo(1024, 1024), rnd = azar(11);
  arbolSeco(L, 256, 508, 150, 13, 6, 3);
  for (let i = 0; i < 26; i++) arbolSeco(L, (i / 26) * 1024 + rnd() * 30, 1020, 40 + rnd() * 60, 3 + rnd() * 3, 4, 100 + i, 1024); // matorrales
  for (const [x, s] of [[120, 1], [470, 0.8], [760, 1.15]]) arbolSeco(L, x, 1020, 120 * s, 9 * s, 6, 200 + x, 1024);
  textura('tex_f096', L);
}

// ---------- cortaderas: dos tiras que se repiten a lo ancho (las de tex_f056, con el doble de alto) ----------
function cortaderas() {
  const L = new Lienzo(1024, 1024); // cada tira: 1024x512 para 3.6 x 1.8 unidades (sin estirar)
  // El pie va 16 filas más arriba: si no, el filtrado (y los mipmaps) pegan las hojas oscuras arriba de la otra tira.
  for (const base of [495, 1007]) {
    const hoja = (x, alto, curva, r0) => { // hoja arqueada que sale del pie de la mata
      let px = x, py = base;
      for (let s = 1; s <= 8; s++) {
        const t = s / 8, nx = x + curva * t * t, ny = base - alto * t + Math.abs(curva) * 0.35 * t * t;
        trazo(L, px, py, r0 * (1.12 - t * 0.85), nx, ny, r0 * (1 - t * 0.85), mezclar(hex('#111a22'), hex('#40505a'), t));
        px = nx; py = ny;
      }
    };
    for (const o of [-1024, 0, 1024]) {
      const r2 = azar(base), pies = Array.from({ length: 9 }, (_, k) => ((k + 0.5 + (r2() - 0.5) * 0.7) * 1024) / 9);
      for (const p of pies) for (let i = 0; i < 24; i++) hoja(p + o + (r2() - 0.5) * 60, 60 + r2() * 150, (r2() - 0.5) * 230, 2 + r2() * 1.8);
      const r3 = azar(base + 7);
      for (let i = 0; i < 16; i++) { // tallos con su penacho, vencidos por el viento
        const x = pies[i % pies.length] + (r3() - 0.5) * 50 + o, alto = 290 + r3() * 180, lean = 10 + r3() * 40;
        const tallo = (t) => [x + lean * t * t, base - alto * t], largo = 0.3 + r3() * 0.12, t0 = 1 - largo;
        for (let k = 0; k < 10; k++) { const [ax, ay] = tallo((k / 10) * t0), [bx, by] = tallo(((k + 1) / 10) * t0); trazo(L, ax, ay, 1.4, bx, by, 1.2, hex('#8a96a3')); }
        const ancho = 20 + r3() * 14, w = (s) => ancho * Math.sin(Math.PI * Math.pow(s, 0.65));
        for (let k = 0; k < 24; k++) { // cuerpo suave del penacho
          const s = 0.1 + (k / 24) * 0.8, [ax, ay] = tallo(t0 + largo * s);
          trazo(L, ax, ay, w(s) * 0.5, ax, ay, w(s) * 0.5, hex('#dfe6f1'), 0.12);
        }
        for (let k = 0; k < 230; k++) { // filamentos que se abren hacia arriba y afuera
          const s = r3(), t = t0 + largo * s, [ax, ay] = tallo(t), lado = r3() < 0.5 ? -1 : 1;
          const dl = Math.hypot(2 * lean * t, alto), tx = (2 * lean * t) / dl, ty = -alto / dl, nx = -ty * lado, ny = tx * lado;
          const f = (20 + r3() * 25) * (Math.PI / 180), l = w(s) * (1.1 + 0.6 * r3());
          const dx = tx * Math.cos(f) + nx * Math.sin(f), dy = ty * Math.cos(f) + ny * Math.sin(f);
          const mx = ax + dx * l * 0.55, my = ay + dy * l * 0.55, ex = ax + dx * l + tx * l * 0.12, ey = ay + dy * l + ty * l * 0.12;
          const col = mezclar(hex('#b4c0d2'), hex('#f5f8fd'), r3()), alfa = 0.25 + r3() * 0.4;
          trazo(L, ax, ay, 0.85, mx, my, 0.6, col, alfa); trazo(L, mx, my, 0.6, ex, ey, 0.35, col, alfa);
        }
      }
    }
  }
  textura('tex_f097', L);
}

// ---------- piedra y corteza (se repiten en los dos sentidos) ----------
function piedra() {
  const N = 512, L = new Lienzo(N, N), rnd = azar(31), fila = 64, filas = N / fila, cortes = [];
  for (let f = 0; f < filas; f++) {
    const c = [], x0 = rnd() * 120;
    for (let x = x0; x < x0 + N - 60; x += 70 + rnd() * 90) c.push(x % N);
    cortes.push(c.sort((a, b) => a - b));
  }
  const tono = Array.from({ length: filas }, () => Array.from({ length: 16 }, () => 0.86 + rnd() * 0.24));
  L.cada((x, y) => {
    const f = Math.floor(y / fila), yy = y - f * fila, c = cortes[f];
    let k = c.findIndex((v) => v > x); if (k < 0) k = 0;
    const izq = c[(k + c.length - 1) % c.length], dist = Math.min((((x - izq) % N) + N) % N, (((c[k] - x) % N) + N) % N, yy, fila - 1 - yy);
    const bisel = yy < 5 ? 0.08 : fila - yy < 6 ? -0.1 : 0;
    const desgaste = 0.16 * fbm(x / 32, y / 32, 5, 41, N / 32, N / 32) + 0.08 * perlin(x / 8, y / 64, 43, N / 8, N / 64);
    const v = dist < 2.5 ? 0.38 : tono[f][k % 16] * (0.92 + desgaste) + bisel;
    return [...hex('#7a7e86').map((c0) => c0 * v), 1];
  });
  textura('tex_f098', L);
}
function corteza() {
  const N = 256, L = new Lienzo(N, N);
  L.cada((x, y) => {
    const n = fbm(x / 8, y / 64, 5, 51, N / 8, N / 64) * 0.5 + 0.5 * perlin(x / 32, y / 128, 53, N / 32, N / 128);
    return [...mezclar(hex('#24211e'), hex('#5c5750'), suave(-0.35, 0.45, n)), 1];
  });
  textura('tex_f099', L);
}

// ---------- geometría ----------
// Arco apuntado de (xc - a, hs) a (xc + a, hs) pasando por el vértice (xc, ha).
export function arcoPuntiagudo(xc, a, hs, ha, n = 8) {
  const R = ((ha - hs) ** 2 + a * a) / (2 * a), cx = xc - a + R, fa = Math.acos((a - R) / R), izq = [];
  for (let i = 0; i <= n; i++) { const f = Math.PI + (fa - Math.PI) * (i / n); izq.push([cx + R * Math.cos(f), hs + R * Math.sin(f)]); }
  return [...izq, ...izq.slice(0, n).reverse().map(([x, y]) => [2 * xc - x, y])];
}
// Borde superior roto, de x1 a x0, en escalones de sillar; nunca más bajo que `piso(x)`.
function techoRoto(x0, x1, h0, h1, rnd, piso = () => -1e9, rugo = 0.45) {
  const pts = [], n = Math.max(2, Math.round((x1 - x0) / 0.5));
  let yAnt = null;
  for (let i = 0; i <= n; i++) {
    const t = i / n, x = x1 + (x0 - x1) * t;
    let y = h1 + (h0 - h1) * t + (rnd() - 0.5) * 2 * rugo - (rnd() < 0.2 ? rnd() * rugo * 2 : 0);
    y = Math.max(piso(x), Math.round(y / 0.4) * 0.4);
    if (yAnt !== null && i < n && y !== yAnt) pts.push([x, yAnt]);
    pts.push([x, y]); yAnt = y;
  }
  return pts;
}
// Un paño de muro de x0 a x1 desde y0, con un vano en arco que nace en y0 y el borde de arriba dado (de x1 a x0).
function pano(x0, x1, y0, vano, techo) {
  const pts = [[x0, y0]];
  if (vano) pts.push([vano.xc - vano.a, y0], ...arcoPuntiagudo(vano.xc, vano.a, vano.hs, vano.ha), [vano.xc + vano.a, y0]);
  return [...pts, [x1, y0], ...techo];
}
const sobreVano = (v, margen) => (x) => (v && Math.abs(x - v.xc) < v.a + 0.3 ? v.ha + margen : -1e9);

// Los muros arrancan bajo tierra (cimientos): así no flotan donde la loma baja.
const PIE = -5;

// Muro con arcada abajo y ventana alta donde el muro todavía llega; bahias: [{x0, x1, alto: [h0, h1]}].
function muro(M, g, bahias, grosor, xf, rnd, arcada = { a: 1.05, hs: 2.7, ha: 4.1 }, alta = { a: 0.45, hs: 6.1, ha: 6.9, desde: 5 }) {
  for (const b of bahias) {
    const xc = (b.x0 + b.x1) / 2, v1 = b.sinArco ? null : { xc, ...arcada }, maxAlto = Math.max(...b.alto);
    if (maxAlto <= alta.desde + 0.6) {
      M.extruir(g, pano(b.x0, b.x1, PIE, v1, techoRoto(b.x0, b.x1, b.alto[0], b.alto[1], rnd, sobreVano(v1, 0.5))), grosor, xf);
      continue;
    }
    M.extruir(g, pano(b.x0, b.x1, PIE, v1, [[b.x1, alta.desde], [b.x0, alta.desde]]), grosor, xf);
    const v2 = maxAlto > alta.ha + 0.6 ? { xc, ...alta } : null;
    M.extruir(g, pano(b.x0, b.x1, alta.desde, v2, techoRoto(b.x0, b.x1, b.alto[0], b.alto[1], rnd, sobreVano(v2, 0.4))), grosor, xf);
  }
}

// Loma bajo el castillo (unidades del escenario): elipse de radios rr (hacia el centro) y rt (de costado), con la
// cima plana donde apoya el castillo. Devuelve la altura de la cima y la del suelo en cada punto (x, z).
function loma(M, g, centro, ang, rr, rt, alto, base) {
  const n = 22, lados = 48, ids = [], dirR = [Math.cos(ang), 0, Math.sin(ang)], dirT = [-Math.sin(ang), 0, Math.cos(ang)];
  const altura = (u, v) => base + (alto - base) * (1 - suave(0.42, 1, Math.hypot(u / rr, v / rt))) * (1 + 0.05 * fbm(u * 0.4, v * 0.4, 3, 61));
  const pos = (u, v) => [centro[0] + dirR[0] * u + dirT[0] * v, altura(u, v), centro[2] + dirR[2] * u + dirT[2] * v];
  for (let i = 0; i <= n; i++) {
    const fila = [];
    for (let k = 0; k <= lados; k++) {
      const a = (k / lados) * 2 * Math.PI, d = (i / n) * 1.05, u = Math.cos(a) * rr * d, v = Math.sin(a) * rt * d, e = 0.05;
      const p = pos(u, v);
      let nrm = unit(cruz(resta(pos(u, v + e), pos(u, v - e)), resta(pos(u + e, v), pos(u - e, v))));
      if (nrm[1] < 0) nrm = escalar(nrm, -1);
      fila.push(M.vertice(p, nrm, [p[0] * 0.35, p[2] * 0.35]));
    }
    ids.push(fila);
  }
  for (let i = 0; i < n; i++) for (let k = 0; k < lados; k++) {
    const a = ids[i][k], b = ids[i][k + 1], c = ids[i + 1][k + 1], d = ids[i + 1][k];
    const cr = cruz(resta(M.vs[b].p, M.vs[a].p), resta(M.vs[c].p, M.vs[a].p)); // que la cara mire para arriba
    if (cr[1] >= 0) g.tris.push(a, b, c, a, c, d); else g.tris.push(a, c, b, a, d, c);
  }
  return { cima: altura(0, 0), suelo: (x, z) => altura((x - centro[0]) * dirR[0] + (z - centro[2]) * dirR[2], (x - centro[0]) * dirT[0] + (z - centro[2]) * dirT[2]) };
}

const SIN_BRILLO = [0, 0, 0, 0, 0, 0, 0, 0, 0]; // como los modelos iluminados del juego (sin pasada especular)
function castillo() {
  const M = new Malla(), piedraG = M.material('tex_f098.bmt', [1, 1, 1, 1], SIN_BRILLO), pastoG = M.material('tex_f094.bmt', [1, 1, 1, 1], SIN_BRILLO);
  const rnd = azar(77), th = CASTILLO.ang * RAD, centro = [CASTILLO.dist * Math.cos(th), 0, CASTILLO.dist * Math.sin(th)];
  const { cima, suelo } = loma(M, pastoG, centro, th, 9, 14, 2.3, -0.9);
  // marco del castillo: x a lo largo del frente, z hacia la pelea; escala 0.55 (medidas en "metros" de diseño)
  const giro = Math.atan2(-Math.cos(th), -Math.sin(th)) + 22 * RAD, s = 0.55, base = [centro[0], cima - 0.25, centro[2]];
  const enCastillo = (rot, off) => (x, y, z) => {
    const q = rotY([x, y, z], rot), p = rotY([(q[0] + off[0]) * s, (q[1] + off[1]) * s, (q[2] + off[2]) * s], giro);
    return [p[0] + base[0], p[1] + base[1], p[2] + base[2]];
  };
  muro(M, piedraG, [{ x0: -7, x1: -3.5, alto: [3.4, 4.8] }, { x0: -3.5, x1: 0, alto: [7.8, 8.4] }, { x0: 0, x1: 3.5, alto: [8.6, 7.4] }, { x0: 3.5, x1: 7, alto: [6.6, 4.6] }], 1.0, enCastillo(0, [0, 0, 0]), rnd);
  // torre cuadrada en la punta derecha, con la esquina de atrás caída
  for (const [rot, off, alto] of [[0, [7, 0, -0.6], [10.6, 9.4]], [-Math.PI / 2, [10, 0, -0.6], [8.8, 5.6]], [Math.PI, [10, 0, -3.6], [10.2, 10.8]], [Math.PI / 2, [7, 0, -3.6], [11, 10.4]]])
    muro(M, piedraG, [{ x0: 0, x1: 3, alto, sinArco: true }], 0.8, enCastillo(rot, off), rnd, undefined, { a: 0.3, hs: 7.6, ha: 8.3, desde: 6.6 });
  // ala izquierda que se va hacia atrás, más derrumbada
  muro(M, piedraG, [{ x0: 0, x1: 3.6, alto: [4.2, 3.4] }, { x0: 3.6, x1: 7.2, alto: [3.0, 1.6] }], 0.9, enCastillo(Math.PI - 62 * RAD, [-7, 0, 0]), rnd, { a: 0.9, hs: 2.2, ha: 3.2 });
  // arco suelto adelante
  M.extruir(piedraG, pano(-2, 2, PIE, { xc: 0, a: 1.15, hs: 2.6, ha: 3.9 }, arcoPuntiagudo(0, 2, 2.6, 4.8, 10).reverse()), 0.9, enCastillo(-12 * RAD, [-1.5, 0, 5.5]));
  for (let i = 0; i < 22; i++) { // escombros
    const a = rnd() * 2 * Math.PI, r = 2 + rnd() * 7, lado = 0.3 + rnd() * 0.8, rot = rnd() * Math.PI;
    const off = rotY([Math.cos(a) * r, 0, Math.sin(a) * r + 1.5], -rot), [wx, , wz] = enCastillo(rot, off)(0, 0, 0);
    off[1] = (suelo(wx, wz) - base[1]) / s; // donde la loma baja, el escombro baja con ella
    M.caja(piedraG, -lado / 2, -lado * 0.3, lado / 2, lado * (0.4 + rnd() * 0.5), lado * (0.6 + rnd() * 0.6), enCastillo(rot, off));
  }
  monolito(M, piedraG);
  return M;
}

// Monolito partido y vencido, clavado entre las cortaderas.
function monolito(M, g) {
  const th = MONOLITO.ang * RAD, c = [MONOLITO.dist * Math.cos(th), -0.45, MONOLITO.dist * Math.sin(th)], giro = Math.atan2(-Math.cos(th), -Math.sin(th)) + 30 * RAD;
  const techo = techoRoto(-0.55, 0.55, 3.3, 4.1, azar(91), () => 2.6, 0.35);
  M.extruir(g, [[-0.55, 0], [0.55, 0], ...techo], 0.75, (x, y, z) => { const p = rotY(rotX(rotZ([x, y, z], -15 * RAD), 7 * RAD), giro); return [p[0] + c[0], p[1] + c[1], p[2] + c[2]]; });
}

// Árboles secos en 3D: tronco retorcido que se abre en ramas.
function ramas3d(M, g, p0, dir, largo, radio, nivel, rnd) {
  const pts = [p0], rad = [radio];
  let p = p0, d = dir;
  for (let s = 1; s <= 4; s++) {
    d = unit(suma(d, [(rnd() - 0.5) * 0.4, (rnd() - 0.3) * 0.15, (rnd() - 0.5) * 0.4]));
    p = suma(p, escalar(d, largo / 4)); pts.push(p); rad.push(radio * (1 - (0.3 * s) / 4));
  }
  M.tubo(g, pts, rad, nivel > 3 ? 7 : nivel > 1 ? 5 : 4, largo * 0.8);
  if (nivel === 0) return;
  const hijos = 2 + (rnd() < 0.4 ? 1 : 0);
  for (let k = 0; k < hijos; k++) {
    const eje = unit(cruz(d, [rnd() - 0.5, rnd() - 0.5, rnd() - 0.5])), a = (28 + rnd() * 26) * RAD;
    const nd = unit(suma(suma(escalar(d, Math.cos(a)), escalar(eje, Math.sin(a))), [0, 0.22, 0]));
    ramas3d(M, g, p, nd, largo * (0.62 + rnd() * 0.18), rad[4] * 0.92, nivel - 1, rnd);
  }
}
function arboles3d() {
  const M = new Malla(), g = M.material('tex_f099.bmt', [1, 1, 1, 1], SIN_BRILLO);
  for (const [ang, dist, esc, sem] of [[ARBOL.ang, ARBOL.dist, 1, 5], [205, 16.5, 0.75, 6], [300, 15.5, 0.85, 7], [20, 17, 0.7, 8]]) {
    const th = ang * RAD;
    ramas3d(M, g, [dist * Math.cos(th), -0.4, dist * Math.sin(th)], unit([0.12, 1, -0.05]), 2.4 * esc, 0.3 * esc, 5, azar(sem));
  }
  return M;
}

// Miniatura de la selección de escenario (cuarta franja de 256x64 de tex_sys42, con su alfa) renderizada sin
// HUD ni personajes. Con --png deja además una vista grande con la cámara del juego.
function miniatura(e) {
  const tex = (n) => generadas.get(n) || (() => { const b = sacar(`obj/${n}`); return b.toString('latin1', 0, 4) === 'BMT_' ? leerBmt(b) : null; })();
  const mods = e.modelos.map((m) => ({ ...m, nmd: modelos.get(m.nombre.replace('.nmd', '')) }));
  const luz = { difusa: e.difusa, ambiente: e.ambiente, dir: e.dir }, cache = new Map(), texturas = (n) => (cache.has(n) ? cache.get(n) : cache.set(n, tex(n)).get(n));
  const W = 1024, H = 256, img = vista({ w: W, h: H, modelos: mods, texturas, luz, cam: { pos: [0, 0.55, -5.7], rumbo: 86, cabeceo: 3, fovV: 17 } });
  const sys = leerBmt(sacar('obj/tex_sys42.bmt')), L = new Lienzo(sys.w, sys.h);
  for (let i = 0; i < sys.w * sys.h; i++) L.d.set([0, 1, 2, 3].map((k) => sys.rgba[4 * i + k] / 255), 4 * i);
  for (let y = 0; y < 64; y++) for (let x = 0; x < 256; x++) { // promedio de 4x4
    const c = [0, 0, 0];
    for (let dy = 0; dy < 4; dy++) for (let dx = 0; dx < 4; dx++) for (let k = 0; k < 3; k++) c[k] += Math.min(1, img[3 * ((4 * y + dy) * W + 4 * x + dx) + k]) / 16;
    L.d.set(c, 4 * ((192 + y) * sys.w + x));
  }
  textura('tex_sys42', L);
  if (PNG) {
    const G = vista({ w: 1332, h: 600, modelos: mods, texturas, luz, cam: { pos: [0, 0.5, -5.7], rumbo: 90, cabeceo: 0, fovV: 30.6 } }), V = new Lienzo(1332, 600);
    for (let i = 0; i < 1332 * 600; i++) V.d.set([Math.min(1, G[3 * i]), Math.min(1, G[3 * i + 1]), Math.min(1, G[3 * i + 2]), 1], 4 * i);
    writeFileSync(join(PNG, 'vista.png'), V.png());
  }
}

// Copia de un modelo del juego con otro nombre (y lo que haga falta cambiarle).
function copia(original, nombre, cambio) {
  const m = leerNmd(sacar(`obj/${original}.nmd`)); m.nombre = nombre; cambio(m); return m;
}

// Cortaderas: las tiras de pasto de tex_f056, 2.6 veces más altas y con la v de la textura nueva (tiras del doble).
function estirarPasto(m, mat) {
  const usados = new Set();
  for (let t = mat.ini; t < mat.ini + mat.n; t++) for (let k = 0; k < 3; k++) usados.add(m.is[3 * t + k]);
  const vs = [...usados].map((i) => m.vs[i]);
  const pies = vs.map((v) => Math.min(...vs.filter((o) => Math.hypot(o.p[0] - v.p[0], o.p[2] - v.p[2]) < 0.05).map((o) => o.p[1])));
  vs.forEach((v, i) => { v.p = [v.p[0], pies[i] + (v.p[1] - pies[i]) * 3, v.p[2]]; v.uv = [v.uv[0] * 0.45, v.uv[1] * 2]; });
  mat.tex = 'tex_f097.bmt';
}

// Matas de cortadera más cerca (desde r 13.2, donde el juego ya ponía pasto y árboles), de afuera hacia adentro
// porque se dibujan sin escribir Z.
function cortaderasCerca() {
  const M = new Malla(), rnd = azar(131), matas = [];
  for (let i = 0; i < 200; i++) matas.push({ th: rnd() * 2 * Math.PI, r: 13.2 + rnd() * 4.3, ancho: 1.6 + rnd() * 1.8, alto: 1.2 + rnd() * 1.4, v0: rnd() < 0.5 ? 0 : 0.5, u0: rnd() });
  matas.sort((a, b) => b.r - a.r);
  for (const m of matas) {
    const c = [m.r * Math.cos(m.th), -0.15, m.r * Math.sin(m.th)], tg = [-Math.sin(m.th), 0, Math.cos(m.th)];
    const A = suma(c, escalar(tg, -m.ancho / 2)), B = suma(c, escalar(tg, m.ancho / 2)), arriba = [0, m.alto, 0], u1 = m.u0 + m.ancho / 3.6;
    const g = M.material('tex_f097.bmt', m.r > 16 ? [0.62, 0.68, 0.8, 1] : m.r > 14.6 ? [0.8, 0.85, 0.94, 1] : [1, 1, 1, 1]);
    M.cara(g, [suma(A, arriba), A, B, suma(B, arriba)], [[m.u0, m.v0], [m.u0, m.v0 + 0.5], [u1, m.v0 + 0.5], [u1, m.v0]]); // mira al centro
  }
  return M;
}
// Agrega los materiales de una Malla al final de un modelo leído.
function anexar(m, M) {
  const base = m.vs.length, n = M.aNmd('');
  m.vs.push(...n.vs);
  for (const mat of n.mats) m.mats.push({ ...mat, ini: mat.ini + m.is.length / 3 });
  m.is.push(...n.is.map((i) => i + base));
}

const modelos = new Map();
const modelo = (m) => { modelos.set(m.nombre, m); salidas.push({ nombre: `obj/${m.nombre}.nmd`, datos: escribirNmd(m) }); };

export function principal() {
  if (PNG) mkdirSync(PNG, { recursive: true });
  cielo(); luna();
  deNoche(leerBmt(sacar('obj/tex_f050.bmt')), PASTO, 'tex_f093');
  deNoche(leerBmt(sacar('obj/tex_f053.bmt')), PASTO.map(([t, c]) => [t, c.map((v) => v * 0.9)]), 'tex_f094');
  montes(sacar('obj/tex_f055.bma'));
  arboles(); cortaderas(); piedra(); corteza();

  modelo(domo().aNmd('mod_f090'));
  modelo(lunaQuad().aNmd('mod_f091'));
  modelo(castillo().aNmd('mod_f092'));
  modelo(copia('mod_f050', 'mod_f093', (m) => { m.mats[0].tex = 'tex_f093.bmt'; }));
  modelo(copia('mod_f052', 'mod_f094', (m) => { m.mats[0].tex = 'tex_f094.bmt'; }));
  modelo(arboles3d().aNmd('mod_f095'));
  modelo(copia('mod_f054', 'mod_f096', (m) => { m.mats[0].tex = 'tex_f095.bmt'; m.mats[1].tex = 'tex_f096.bmt'; estirarPasto(m, m.mats[2]); anexar(m, cortaderasCerca()); }));
  modelo(copia('mod_f053', 'mod_f097', (m) => { m.mats[0].col.splice(0, 3, 0.45, 0.56, 0.85); })); // neblina azulada

  const campo = leerCampo(sacar('dat/field.dat')), e = campo[ESCENARIO];
  const M = (nombre, tipo, z, escribeZ, mezcla) => ({ nombre: `${nombre}.nmd`, tipo, z, escribeZ, mezcla, pruebaAlfa: 0 });
  e.modelos = [
    M('mod_f093', 3, 1, 1, 0), // piso: el primero, su textura se corre con la pelea
    M('mod_f094', 3, 1, 1, 0), // lomas
    M('mod_f097', 3, 0, 0, 2), // neblina sobre el piso
    M('mod_f092', 1, 1, 1, 0), // loma del castillo, ruinas y monolito (iluminados)
    M('mod_f095', 1, 1, 1, 0), // árboles secos
    M('mod_f090', 3, 1, 0, 0), // cielo
    M('mod_f091', 3, 1, 0, 2), // luna
    M('mod_f096', 3, 1, 0, 2), // montes, árboles de cartel y cortaderas
  ];
  Object.assign(e, { efecto: 3, difusa: [0.74, 0.82, 1.08, 1], ambiente: [0.4, 0.44, 0.6, 1], dir: [0.18, -0.45, 0.3] });
  salidas.push({ nombre: 'dat/field.dat', datos: escribirCampo(campo) });
  miniatura(e);

  const v2 = readFileSync(join(JUEGO, 'vsobj2.dat')), nuevos = new Set(salidas.map((s) => s.nombre));
  const viejos = leerDat1Tabla(v2).filter((x) => !nuevos.has(x.nombre)).map((x) => ({ nombre: x.nombre, datos: v2.subarray(x.offset, x.offset + x.tam) }));
  mkdirSync(join(SCRATCH, 'mods'), { recursive: true });
  const destino = join(SCRATCH, 'mods', 'vsobj2.dat');
  writeFileSync(destino, escribirDat1([...salidas, ...viejos]));
  console.log(`${salidas.length} archivos nuevos (${[...modelos.keys()].join(', ')}) -> ${destino}`);
}

if (import.meta.url === `file:///${process.argv[1]?.replaceAll('\\', '/')}`) principal();
