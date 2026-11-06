// Lost Grave -Undead-: Lost Grave de noche, con luna roja, humo y fuegos en el horizonte, barricadas y una horda de
// zombis que se mueve. Es el escenario 10 de field.dat y el lugar 8 de la rueda de Stage Select (analisis/parches.txt).
// Los zombis, sus sombras y las llamas son modelos animados (d3d/animado.c): los primeros vértices llevan los datos
// (firma, huesos, figuras) y el resto es cada figura en su pose de descanso, en su espacio local.
// Medidas: los luchadores miden ~1 y la cámara anda a menos de 8.2 del centro; el piso es plano hasta ~11.6.
import { readFileSync } from 'node:fs';
import { leerBmt, leerPng } from './bmt.mjs';
import { Lienzo, azar, fbm, perlin, sat, suave, mezclar, hex, trazo, poligono } from './pintura.mjs';
import { Malla, rotY, unit, cruz, resta, suma, escalar } from './geometria.mjs';
import { sacar, textura, modelo, miniatura } from './comun.mjs';
import { domo, luna, lunaQuad, copia } from './moonlit.mjs';

export const ESCENARIO = 10, FILA = 8; // en field.dat y en la rueda (fila de nombres y miniaturas)
const RAD = Math.PI / 180;
const LUNA = { ang: 96, elev: 11, dist: 38, radio: 5.2 };
const FUEGOS_LEJOS = [[28, 1], [74, 0.7], [152, 1.2], [214, 0.8], [262, 1], [318, 0.9]]; // [ángulo, fuerza] en el horizonte
const R_BARRICADA = 9.4, R_EMPUJAN = 9.95, CARRILES = [{ r: 10.7, sentido: 1, vel: 0.2 }, { r: 11.3, sentido: -1, vel: 0.24 }];

// ---------- cielo: humo, luna roja y el resplandor de los incendios (panorama de 4096x1024, como moonlit) ----------
const vAElev = (v) => (v >= 0.25 ? -10 + ((1 - v) / 0.75) * 40 : 30 + ((0.25 - v) / 0.25) * 60);
const difAng = (a, b) => { let d = (a - b) % 360; if (d > 180) d -= 360; if (d < -180) d += 360; return d; };
function cielo() {
  const W = 4096, H = 1024, L = new Lienzo(W, H), rnd = azar(17);
  const lunaDir = [Math.cos(LUNA.elev * RAD) * Math.cos(LUNA.ang * RAD), Math.sin(LUNA.elev * RAD), Math.cos(LUNA.elev * RAD) * Math.sin(LUNA.ang * RAD)];
  const fuego = (az, e) => FUEGOS_LEJOS.reduce((s, [a, k]) => s + k * Math.exp(-((difAng(az, a) / 16) ** 2)), 0) * Math.exp(-Math.max(e, 0) / 6);
  const humo = (x, y) => { // densidad 0..1, periódica en x; columnas que suben desde los incendios
    const e = vAElev(y / H), az = (x / W) * 360;
    const col = FUEGOS_LEJOS.reduce((s, [a, k]) => s + k * Math.exp(-((difAng(az, a + e * 0.35) / (5 + e * 0.4)) ** 2)), 0);
    const n = fbm(x / 400, y / 260, 5, 13, 10, 0, 0.55) * 0.8 + 0.5 * perlin(x / 1024, y / 600, 19, 4);
    return sat(suave(0.05, 0.5, n) * suave(1, 6, e) * (1 - 0.65 * suave(20, 60, e)) + 0.55 * col * suave(-2, 4, e) * (1 - suave(8, 45, e)));
  };
  L.cada((x, y) => {
    const e = vAElev((y + 0.5) / H), az = ((x + 0.5) / W) * 360, th = az * RAD;
    const dir = [Math.cos(e * RAD) * Math.cos(th), Math.sin(e * RAD), Math.cos(e * RAD) * Math.sin(th)];
    const ang = Math.acos(sat(dir[0] * lunaDir[0] + dir[1] * lunaDir[1] + dir[2] * lunaDir[2])) / RAD;
    let c = e < 0 ? mezclar(hex('#1f0d11'), hex('#12080b'), sat(-e / 10)) : e < 6 ? mezclar(hex('#1f0d11'), hex('#170b13'), e / 6)
      : e < 20 ? mezclar(hex('#170b13'), hex('#0f0911'), (e - 6) / 14) : e < 45 ? mezclar(hex('#0f0911'), hex('#08060d'), (e - 20) / 25) : mezclar(hex('#08060d'), hex('#040308'), (e - 45) / 45);
    const halo = 0.24 * Math.exp(-((ang / 11) ** 2)) + 0.1 * Math.exp(-((ang / 36) ** 2)), f = fuego(az, e);
    c = c.map((v, k) => v + halo * [0.85, 0.22, 0.16][k] + f * [0.75, 0.25, 0.06][k]);
    const d = humo(x, y);
    if (d > 0.001) { // humo oscuro, con el borde rojo del lado de la luna y anaranjado abajo, sobre los fuegos
      const nucleo = mezclar(hex('#120a0e'), hex('#2a1418'), sat(1 - d) * 0.5);
      const lit = c.map((v, k) => nucleo[k] + halo * 0.9 * [0.9, 0.25, 0.18][k] + f * 1.1 * [0.95, 0.4, 0.12][k]);
      c = mezclar(c, lit, sat(d * 1.2));
    }
    return [...c, 1];
  });
  for (let i = 0; i < 900; i++) { // pocas estrellas: el humo tapa casi todo
    const x = rnd() * W, y = rnd() * H * 0.7, e = vAElev(y / H);
    if (e < 12) continue;
    const b = (1 - sat(humo(Math.floor(x), Math.floor(y)) * 3)) * suave(12, 30, e) * (0.2 + 0.8 * rnd() ** 3);
    if (b > 0.05) trazo(L, x, y, 0.5 + rnd() * 0.4, x, y, 0.5, mezclar([1, 0.9, 0.85], [1, 1, 1], rnd()), b);
  }
  textura('tex_f140', L.recorte(0, 0, 2048, 1024));
  textura('tex_f141', L.recorte(2048, 0, 2048, 1024));
}

// ---------- texturas de Lost Grave pasadas a noche (luminancia -> degradé) ----------
const gradiente = (stops, t) => {
  for (let i = 1; i < stops.length; i++) if (t <= stops[i][0]) return mezclar(stops[i - 1][1], stops[i][1], sat((t - stops[i - 1][0]) / (stops[i][0] - stops[i - 1][0])));
  return stops[stops.length - 1][1];
};
function teñir({ w, h, rgba }, stops, nombre) {
  const L = new Lienzo(w, h);
  L.cada((x, y) => { const i = 4 * (y * w + x); return [...gradiente(stops, (0.3 * rgba[i] + 0.59 * rgba[i + 1] + 0.11 * rgba[i + 2]) / 255), rgba[i + 3] / 255]; });
  textura(nombre, L);
}
const SUELO = [[0.15, hex('#0c0809')], [0.35, hex('#1f1515')], [0.5, hex('#30211f')], [0.65, hex('#43302b')], [0.85, hex('#634840')]];
const CIUDAD = [[0.1, hex('#07070b')], [0.3, hex('#15141c')], [0.5, hex('#272330')], [0.7, hex('#3c3542')], [0.9, hex('#5d505b')]];

// ---------- zombis ----------
// Atlas de 512x512 en 16 baldosas de 128: filas 0 y 1 camisas (torso arriba hasta v 0.6, manga y piel abajo),
// fila 2 pantalones (zapatos desde v 0.85), fila 3 cabezas (v 0 cuello, v 1 coronilla; la cara mira a u 0.5).
const PIEL = [hex('#8d9a82'), hex('#7d8a86'), hex('#a39f7e'), hex('#6f7a6a')];
function piel(L, x0, y0, w, h, tono, rnd) {
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
    const n = fbm((x0 + x) / 9, (y0 + y) / 9, 4, 71) * 0.5 + 0.5, m = suave(0.62, 0.8, fbm((x0 + x) / 22, (y0 + y) / 22, 3, 73) * 0.5 + 0.5);
    const c = mezclar(tono.map((v) => v * (0.82 + 0.3 * n)), hex('#4a3f4e'), m * 0.6); // moretones violáceos
    L.d.set([...c, 1], 4 * ((y0 + y) * L.w + x0 + x));
  }
  for (let i = 0; i < (w * h) / 900; i++) { // venas
    let px = x0 + rnd() * w, py = y0 + rnd() * h, a = rnd() * Math.PI * 2;
    for (let s = 0; s < 6; s++) { const nx = px + Math.cos(a) * 4, ny = py + Math.sin(a) * 4; trazo(L, px, py, 0.5, nx, ny, 0.4, hex('#3d4a52'), 0.35); px = nx; py = ny; a += (rnd() - 0.5) * 1.2; }
  }
}
function sangre(L, x, y, r, rnd, alfa = 0.85) {
  const col = hex('#3a0607');
  for (let i = 0; i < 7; i++) { const a = rnd() * Math.PI * 2, d = rnd() * r * 0.6; trazo(L, x + Math.cos(a) * d, y + Math.sin(a) * d, r * (0.25 + rnd() * 0.3), x + Math.cos(a) * d, y + Math.sin(a) * d, r * 0.2, col, alfa); }
  for (let i = 0; i < 3; i++) { const sx = x + (rnd() - 0.5) * r, l = r * (1 + rnd() * 2); trazo(L, sx, y, r * 0.12, sx + (rnd() - 0.5) * 2, y + l, r * 0.05, col, alfa * 0.9); } // chorreado hacia abajo
}
function tela(L, x0, y0, w, h, base, sem, rayas = 0) {
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
    const n = fbm((x0 + x) / 5, (y0 + y) / 5, 3, sem) * 0.5 + 0.5, pl = fbm((x0 + x) / 30, (y0 + y) / 10, 3, sem + 5) * 0.5 + 0.5;
    let c = base.map((v) => v * (0.75 + 0.25 * n) * (0.8 + 0.35 * pl));
    if (rayas && ((Math.floor((x0 + x) / 8) + Math.floor((y0 + y) / 8)) & 1)) c = c.map((v) => v * 0.6); // cuadrillé
    const sucio = suave(0.55, 0.85, fbm((x0 + x) / 18, (y0 + y) / 18, 4, sem + 9) * 0.5 + 0.5);
    L.d.set([...mezclar(c, hex('#2b2219'), sucio * 0.55), 1], 4 * ((y0 + y) * L.w + x0 + x));
  }
}
function atlasZombis() {
  const L = new Lienzo(512, 512), rnd = azar(41), T = 128;
  const CAMISAS = [['#c9c4b6', 0], ['#3f5a7a', 0], ['#7a2f2a', 1], ['#3c4a35', 0], ['#8a8170', 1], ['#2b2b30', 0], ['#5b6f8c', 1], ['#9a8a4a', 0]];
  CAMISAS.forEach(([c, rayas], k) => {
    const x0 = (k % 4) * T, y0 = Math.floor(k / 4) * T;
    tela(L, x0, y0, T, Math.round(T * 0.75), hex(c), 80 + k, rayas);
    piel(L, x0, y0 + Math.round(T * 0.75), T, T - Math.round(T * 0.75), PIEL[k % 4], rnd);
    for (let i = 0; i < 4; i++) { // roturas con piel adentro
      const cx = x0 + 10 + rnd() * (T - 20), cy = y0 + 8 + rnd() * T * 0.45, r = 5 + rnd() * 9, pts = [];
      for (let a = 0; a < 9; a++) { const ang = (a / 9) * Math.PI * 2, rr = r * (0.6 + rnd() * 0.6); pts.push([cx + Math.cos(ang) * rr, cy + Math.sin(ang) * rr * 0.7]); }
      poligono(L, pts, PIEL[(k + 1) % 4].map((v) => v * 0.8));
    }
    sangre(L, x0 + T * (0.42 + rnd() * 0.16), y0 + T * 0.52, 14, rnd); // del cuello para abajo, adelante (u 0.5)
    sangre(L, x0 + T * (0.3 + rnd() * 0.4), y0 + T * (0.15 + rnd() * 0.25), 9, rnd, 0.6);
  });
  const PANTALONES = ['#2f4466', '#6b5d43', '#1e1e22', '#55565a'];
  PANTALONES.forEach((c, k) => {
    const x0 = k * T, y0 = 2 * T;
    tela(L, x0, y0, T, Math.round(T * 0.85), hex(c), 90 + k);
    for (let y = Math.round(T * 0.85); y < T; y++) for (let x = 0; x < T; x++) L.d.set([...hex(k & 1 ? '#1a1410' : '#26201b').map((v) => v * (0.8 + 0.3 * rnd())), 1], 4 * ((y0 + y) * L.w + x0 + x));
    sangre(L, x0 + T * 0.5, y0 + T * 0.1, 10, rnd, 0.5);
    sangre(L, x0 + T * (0.2 + rnd() * 0.6), y0 + T * 0.6, 8, rnd, 0.6);
  });
  const PELO = ['#1a1512', '#3b2a1c', null, '#7a7468'];
  PELO.forEach((pelo, k) => {
    const x0 = k * T, y0 = 3 * T;
    piel(L, x0, y0, T, T, PIEL[k], rnd);
    const P = (u, v) => [x0 + u * T, y0 + v * T];
    if (pelo) for (let y = 0; y < T; y++) for (let x = 0; x < T; x++) { // arriba y atrás; ralo, con calvas
      const u = x / T, v = y / T, atras = Math.min(u, 1 - u), lim = 0.66 - 0.32 * suave(0.12, 0.32, atras) + 0.08 * perlin(x / 9, 0, 300 + k);
      if (v > lim && fbm(x / 6, y / 6, 3, 310 + k) > -0.35) L.d.set([...hex(pelo).map((c) => c * (0.7 + 0.5 * rnd())), 1], 4 * ((y0 + y) * L.w + x0 + x));
    }
    for (const u of [0.43, 0.57]) { // ojos hundidos con un brillo pálido
      const [ex, ey] = P(u, 0.52);
      trazo(L, ex, ey, 6.5, ex, ey, 6.5, hex('#120a0c'), 0.9);
      trazo(L, ex + (u < 0.5 ? 0.8 : -0.8), ey - 0.5, 1.5, ex, ey, 1.5, hex(k === 2 ? '#d84a3a' : '#d9dccb'), 0.9);
    }
    const [bx, by] = P(0.5, 0.3); // boca abierta, oscura, con dientes y sangre que chorrea
    poligono(L, [[bx - 7, by - 3], [bx + 7, by - 3], [bx + 5, by + 6], [bx - 5, by + 6]], hex('#150708'));
    for (let i = -2; i <= 2; i++) trazo(L, bx + i * 2.6, by - 3, 0.9, bx + i * 2.6, by - 0.5, 0.6, hex('#b9ad8c'), 0.9);
    sangre(L, bx, by + 4, 7, rnd);
    const [nx, ny] = P(0.5, 0.42);
    trazo(L, nx, ny - 3, 1.6, nx, ny + 2, 2.2, hex('#3b3434'), 0.5); // sombra de la nariz
  });
  textura('tex_f149', L);
}

// Baldosa del atlas: u, v de 0 a 1 dentro de la baldosa (col, fila), con un margen para el filtrado.
const baldosa = (col, fila) => (u, v) => [(col + 0.02 + 0.96 * u) / 4, (fila + 0.02 + 0.96 * v) / 4];

// Anillos elípticos alrededor del eje y (torso, cadera): u alrededor (0 atrás, 0.5 adelante), v de abajo arriba.
function anillos(M, g, cy, alturas, rx, rz, lados, uv, tapaAbajo) {
  const ids = alturas.map((y, i) => Array.from({ length: lados + 1 }, (_, k) => {
    const a = (k / lados) * 2 * Math.PI, sn = Math.sin(a), cs = -Math.cos(a);
    const p = [rx[i] * sn, y, cy + rz[i] * cs], n = unit([sn / rx[i], 0, cs / rz[i]]);
    return M.vertice(p, n, uv(k / lados, i / (alturas.length - 1)));
  }));
  for (let i = 0; i + 1 < ids.length; i++) for (let k = 0; k < lados; k++) {
    const a = ids[i][k], b = ids[i][k + 1], c = ids[i + 1][k + 1], d = ids[i + 1][k];
    g.tris.push(a, d, c, a, c, b);
  }
  for (const [fila, y, sg] of [[ids.at(-1), alturas.at(-1), 1], ...(tapaAbajo ? [[ids[0], alturas[0], -1]] : [])]) {
    const c = M.vertice([0, y, cy], [0, sg, 0], uv(0.5, sg > 0 ? 1 : 0));
    for (let k = 0; k < lados; k++) sg > 0 ? g.tris.push(c, fila[k + 1], fila[k]) : g.tris.push(c, fila[k], fila[k + 1]);
  }
}
// Le pone a los vértices desde `desde` una uv del atlas (las de Malla.tubo van de 0 a 1) o una fija.
function remapear(M, desde, f) { for (let i = desde; i < M.vs.length; i++) M.vs[i].uv = f(M.vs[i].uv); }

export const PIVOTES = [[0, 0.52, 0, -1], [0, 0.56, 0, 0], [0, 0.845, 0, 1], [0.115, 0.8, 0, 1], [-0.115, 0.8, 0, 1], [0.055, 0.5, 0, 0], [-0.055, 0.5, 0, 0]];

// Un zombi de alto 1 mirando a +z, con el hueso de cada vértice. Todas las variantes tienen la misma malla.
function zombi(v) {
  const M = new Malla(), g = M.material('tex_f149.bmt', [1, 1, 1, 1], [0, 0, 0, 0, 0, 0, 0, 0, 0]), huesos = [];
  const parte = (h, armar) => { const d = M.vs.length; armar(d); while (huesos.length < M.vs.length) huesos.push(h); };
  const camisa = baldosa(v.camisa % 4, Math.floor(v.camisa / 4)), pantalon = baldosa(v.pantalon, 2), cabeza = baldosa(v.cabeza, 3);
  const g2 = v.gordo;
  parte(0, () => anillos(M, g, 0, [0.45, 0.5, 0.56], [0.078 * g2, 0.086 * g2, 0.08 * g2], [0.052 * g2, 0.058 * g2, 0.055 * g2], 8, (u, t) => pantalon(u, 0.02 + 0.1 * t), true));
  parte(1, () => anillos(M, g, -0.004, [0.53, 0.62, 0.71, 0.79, 0.84], [0.078 * g2, 0.083 * g2, 0.097 * g2, 0.106, 0.045], [0.054 * g2, 0.058 * g2, 0.062 * g2, 0.058, 0.03], 8, (u, t) => camisa(u, 0.6 * t), false));
  parte(2, (d) => {
    M.tubo(g, [[0, 0.83, 0.004], [0, 0.85, 0.01], [0, 0.89, 0.016], [0, 0.93, 0.012], [0, 0.965, 0.004], [0, 0.987, 0]], [0.032, 0.05, 0.063, 0.061, 0.047, 0.012], 8);
    remapear(M, d, ([u, t]) => cabeza(u, t));
  });
  for (const lado of [1, -1]) { // brazos: colgando, con el codo apenas doblado
    parte(lado > 0 ? 3 : 4, (d) => {
      M.tubo(g, [[0.115 * lado, 0.8, 0], [0.125 * lado, 0.66, -0.012], [0.128 * lado, 0.52, 0.012], [0.126 * lado, 0.47, 0.02]], [0.034, 0.028, 0.023, 0.021], 6);
      remapear(M, d, ([u, t]) => camisa(u, 0.6 + 0.35 * t));
      const m0 = M.vs.length;
      M.caja(g, -0.016, -0.06, 0.016, 0, 0.034, (x, y, z) => [0.126 * lado + x, 0.47 + y, 0.02 + z]); // mano
      remapear(M, m0, () => camisa(0.5, 0.97));
    });
  }
  for (const lado of [1, -1]) { // piernas y zapatos
    parte(lado > 0 ? 5 : 6, (d) => {
      M.tubo(g, [[0.055 * lado, 0.5, 0], [0.056 * lado, 0.28, 0.012], [0.055 * lado, 0.06, -0.006]], [0.043, 0.033, 0.025], 6);
      remapear(M, d, ([u, t]) => pantalon(u, 0.12 + 0.72 * t));
      const p0 = M.vs.length;
      M.caja(g, -0.03, 0, 0.03, 0.05, 0.12, (x, y, z) => [0.055 * lado + x, 0.008 + y, 0.025 + z]);
      remapear(M, p0, () => pantalon(0.5, 0.93));
    });
  }
  const n = M.aNmd('');
  return { vs: n.vs, is: n.is, huesos };
}

// Datos para d3d/animado.c en vértices que ningún triángulo usa (posición y uv: 5 floats cada uno).
export function cabecera(figuras, nV, pivotes, huesos) {
  const f = [7777, -3131, 2468, 1, 0, figuras.length, nV, pivotes.length, ...pivotes.flat()];
  for (const z of figuras) f.push(z.x, z.z, z.rumbo, z.escala, z.tipo, z.fase, z.vel, z.radio, z.brazo ?? 0, z.incl ?? 0, z.cojera ?? 0, z.sentido ?? 0);
  f.push(...huesos);
  while (f.length % 5) f.push(0);
  f[4] = f.length / 5;
  const vs = [];
  for (let i = 0; i < f.length; i += 5) vs.push({ p: f.slice(i, i + 3), n: [0, 1, 0], c: 0xffffffff, uv: f.slice(i + 3, i + 5) });
  return vs;
}
// Modelo animado: datos + cada figura con su copia de la malla. La vista lleva las figuras colocadas en reposo.
export function animado(nombre, figuras, malla, pivotes, mats) {
  const datos = cabecera(figuras, malla.vs.length, pivotes, malla.huesos), D = datos.length, vs = [...datos], is = [], vVs = [], vIs = [];
  figuras.forEach((z, k) => {
    const base = D + k * malla.vs.length, dz = malla.variante ? malla.variante(z) : malla;
    if (dz.vs.length !== malla.vs.length || dz.huesos.join() !== malla.huesos.join()) throw new Error(`${nombre}: las variantes tienen que compartir la malla`);
    vs.push(...dz.vs);
    is.push(...dz.is.map((i) => i + base));
    vVs.push(...dz.vs.map((v) => ({ ...v, p: suma(rotY(escalar(v.p, z.escala), z.rumbo), [z.x, 0, z.z]), n: rotY(v.n, z.rumbo) })));
    vIs.push(...dz.is.map((i) => i + k * malla.vs.length));
  });
  if (vs.length > 65535) throw new Error(`${nombre}: ${vs.length} vértices`);
  const m = { nombre, mats: mats(is.length / 3), vs, is }, v = { nombre, mats: mats(vIs.length / 3), vs: vVs, is: vIs };
  return [m, v];
}

function horda(ocupado) {
  const rnd = azar(2024), figuras = [];
  // contra la barricada, mirando a la pelea (no donde hay un auto cruzado)
  for (let i = 0; i < 30; i++) {
    const th = ((i + 0.5) / 30) * 2 * Math.PI + (rnd() - 0.5) * 0.15, r = R_EMPUJAN + (rnd() - 0.5) * 0.3, x = r * Math.cos(th), z = r * Math.sin(th);
    if (ocupado.some(([a, b]) => th > a && th < b)) continue;
    figuras.push({ x, z, rumbo: Math.atan2(-x, -z) + (rnd() - 0.5) * 0.5, escala: 0.92 + rnd() * 0.16, tipo: 0, fase: rnd(), vel: 0.35 + rnd() * 0.25, radio: r });
  }
  // dando vueltas en dos carriles, cada uno para su lado y a velocidad pareja (no se atraviesan)
  for (const c of CARRILES) for (let i = 0; i < 10; i++) {
    const th = ((i + rnd() * 0.6) / 10) * 2 * Math.PI, r = c.r + (rnd() - 0.5) * 0.12;
    figuras.push({ x: r * Math.cos(th), z: r * Math.sin(th), rumbo: 0, escala: 0.92 + rnd() * 0.16, tipo: 1, fase: rnd(), vel: c.vel, radio: r, sentido: c.sentido });
  }
  for (const z of figuras) {
    const b = rnd();
    Object.assign(z, { brazo: b < 0.5 ? 0 : b < 0.75 ? 1 : b < 0.9 ? 2 : 3, incl: (rnd() - 0.5) * 0.8, cojera: rnd() < 0.4 ? 0.3 + rnd() * 0.6 : 0,
      camisa: Math.floor(rnd() * 8), pantalon: Math.floor(rnd() * 4), cabeza: Math.floor(rnd() * 4), gordo: 0.9 + rnd() * 0.3 });
    if (z.tipo === 1) { const th = Math.atan2(z.z, z.x); z.rumbo = Math.atan2(-z.sentido * Math.sin(th), z.sentido * Math.cos(th)); }
  }
  const base = zombi(figuras[0]), malla = { ...base, variante: (z) => zombi(z) };
  const mat = (n) => [{ nombre: '', col: [1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0], tex: 'tex_f149.bmt', ini: 0, n }];
  const [zm, zv] = animado('mod_f147', figuras, malla, PIVOTES, mat);
  // sombras: un cuadrado por zombi con las mismas figuras (las que caminan siguen a su zombi)
  const S = new Malla(), gs = S.material('tex_f154.bmt');
  S.cara(gs, [[-0.17, 0.006, 0.12], [0.17, 0.006, 0.12], [0.17, 0.006, -0.12], [-0.17, 0.006, -0.12]], [[0, 0], [1, 0], [1, 1], [0, 1]]);
  const sn = S.aNmd(''), sombra = { vs: sn.vs, is: sn.is, huesos: sn.vs.map(() => 0) };
  const [sm, sv] = animado('mod_f148', figuras, sombra, [[0, 0, 0, -1]], (n) => [{ nombre: '', col: [1, 1, 1, 0.75, 0, 0, 0, 0, 1, 1, 1, 0, 0], tex: 'tex_f154.bmt', ini: 0, n }]);
  return { zm, zv, sm, sv, figuras };
}

function sombraTex() {
  const L = new Lienzo(64, 64);
  L.cada((x, y) => { const dx = (x + 0.5 - 32) / 30, dy = (y + 0.5 - 32) / 30, r = Math.hypot(dx, dy); return [0, 0, 0, 0.8 * (1 - suave(0.35, 1, r))]; });
  textura('tex_f154', L);
}

// ---------- barricadas: barreras de hormigón, bolsas de arena, tachos y autos quemados ----------
function hormigon() {
  const N = 256, L = new Lienzo(N, N);
  L.cada((x, y) => { // u: alrededor del perfil (0 abajo, 1 arriba de un lado); v: a lo largo de la barrera
    const u = x / N, n = fbm(x / 16, y / 16, 5, 501, N / 16, N / 16) * 0.5 + 0.5;
    let c = hex('#77736b').map((v) => v * (0.7 + 0.45 * n));
    if (u > 0.5 && u < 0.72) { const raya = Math.floor((y + x * 0.5) / 22) & 1; c = mezclar(c, raya ? hex('#c8a12a') : hex('#1b1a17'), 0.85 * (0.75 + 0.25 * n)); }
    c = mezclar(c, hex('#2a2420'), suave(0.5, 0.9, fbm(x / 40, y / 40, 4, 505, N / 40, N / 40) * 0.5 + 0.5) * 0.6 + (1 - suave(0, 0.18, u)) * 0.35);
    return [...c, 1];
  });
  textura('tex_f150', L);
}
function bolsas() {
  const N = 256, L = new Lienzo(N, N);
  L.cada((x, y) => {
    const tr = ((x >> 1) + (y >> 1)) & 1 ? 0.92 : 1.05, n = fbm(x / 10, y / 10, 4, 601, N / 10, N / 10) * 0.5 + 0.5;
    const borde = Math.min(x % 64, 64 - (x % 64), y % 32, 32 - (y % 32)) < 3 ? 0.6 : 1; // costuras
    return [...hex('#8a7a55').map((v) => v * tr * borde * (0.65 + 0.45 * n)), 1];
  });
  textura('tex_f151', L);
}
function chapa() {
  const N = 256, L = new Lienzo(N, N);
  L.cada((x, y) => {
    const n = fbm(x / 20, y / 20, 5, 701, N / 20, N / 20) * 0.5 + 0.5, oxido = suave(0.45, 0.7, n), q = suave(0.6, 0.85, fbm(x / 12, y / 12, 4, 705, N / 12, N / 12) * 0.5 + 0.5);
    const c = mezclar(mezclar(hex('#5a1f1a'), hex('#6b3a1e'), oxido), hex('#111010'), q * 0.85); // pintura vieja, óxido y quemado
    return [...c.map((v) => v * (0.8 + 0.3 * (fbm(x / 4, y / 4, 2, 709, N / 4, N / 4) * 0.5 + 0.5))), 1];
  });
  textura('tex_f152', L);
  const O = new Lienzo(64, 64);
  O.cada((x, y) => [...hex('#151517').map((v) => v * (0.8 + 0.4 * (fbm(x / 8, y / 8, 3, 711, 8, 8) * 0.5 + 0.5))), 1]);
  textura('tex_f153', O); // gomas, vidrios y huecos
}

// Coloca un objeto mirando al centro en el ángulo th y radio r: x local radial, z tangente.
const enAnillo = (th, r, giro = 0, y0 = 0) => (x, y, z) => {
  const a = th + giro, rad = [Math.cos(a), 0, Math.sin(a)], tan = [-Math.sin(a), 0, Math.cos(a)];
  return [r * Math.cos(th) + rad[0] * x + tan[0] * z, y + y0, r * Math.sin(th) + rad[2] * x + tan[2] * z];
};
function barricadas(fuegos) {
  const M = new Malla(), SIN = [0, 0, 0, 0, 0, 0, 0, 0, 0];
  const gH = M.material('tex_f150.bmt', [1, 1, 1, 1], SIN), gB = M.material('tex_f151.bmt', [1, 1, 1, 1], SIN), gC = M.material('tex_f152.bmt', [1, 1, 1, 1], SIN), gO = M.material('tex_f153.bmt', [1, 1, 1, 1], SIN);
  const rnd = azar(909), perfil = [[-0.17, 0], [0.17, 0], [0.17, 0.04], [0.1, 0.12], [0.06, 0.45], [-0.06, 0.45], [-0.1, 0.12], [-0.17, 0.04]];
  const tacho = (th, r) => {
    const xf = enAnillo(th, r), lados = 10, pts = [0, 0.04, 0.25, 0.46, 0.5].map((y) => xf(0, y, 0));
    M.tubo(gC, pts, [0.17, 0.172, 0.172, 0.172, 0.168], lados, 2);
    M.cara(gO, Array.from({ length: lados }, (_, k) => { const a = -(k / lados) * 2 * Math.PI; return xf(0.165 * Math.cos(a), 0.49, 0.165 * Math.sin(a)); }), Array.from({ length: lados }, () => [0.5, 0.5]));
  };
  const auto = (th, r, giro, vuelco) => {
    const xf0 = enAnillo(th, r, giro), cuerpo = [[-1.3, 0.14], [1.3, 0.14], [1.32, 0.38], [1.1, 0.45], [0.5, 0.47], [0.2, 0.74], [-0.6, 0.74], [-0.9, 0.48], [-1.3, 0.44]];
    // auto: x largo, y alto, z ancho. Largo a lo largo del anillo; volcado, apoyado de costado (rotaciones, sin espejar)
    const xfv = vuelco ? (x, y, z) => xf0(y - 0.4, z + 0.5, x) : (x, y, z) => xf0(-z, y, x);
    M.extruir(gC, cuerpo, 1.0, xfv, 0.6);
    for (const lado of [1, -1]) { // vidrios oscuros a los costados de la cabina
      M.cara(gO, lado > 0 ? [[0.45, 0.48, 0.505], [0.18, 0.7, 0.505], [-0.56, 0.7, 0.505], [-0.84, 0.49, 0.505]].map(([x, y, z]) => xfv(x, y, z)) : [[-0.84, 0.49, -0.505], [-0.56, 0.7, -0.505], [0.18, 0.7, -0.505], [0.45, 0.48, -0.505]].map(([x, y, z]) => xfv(x, y, z)), [[0, 0], [1, 0], [1, 1], [0, 1]]);
      for (const wx of [-0.85, 0.85]) M.tubo(gO, [xfv(wx, 0.18, 0.44 * lado), xfv(wx, 0.18, 0.56 * lado)], [0.18, 0.18], 10);
    }
  };
  let th = 0.05, proximo = 0;
  const huecosFuego = [], ocupado = [], ANG_FUEGOS = [22, 96, 158, 236, 302].map((a) => a * RAD);
  while (th < 2 * Math.PI - 0.15) {
    const r = R_BARRICADA + (rnd() - 0.5) * 0.25, tipo = rnd();
    if (proximo < ANG_FUEGOS.length && th >= ANG_FUEGOS[proximo]) { // tacho prendido entre barreras
      tacho(th + 0.2 / r, r - 0.05); huecosFuego.push([th + 0.2 / r, r - 0.05]);
      th += 0.45 / r; proximo++;
      continue;
    }
    if (tipo < 0.55) { // dos o tres barreras seguidas, alguna volcada
      const n = 2 + Math.floor(rnd() * 2);
      for (let k = 0; k < n; k++) {
        const a = th + (k * 1.15) / R_BARRICADA, caida = rnd() < 0.12;
        const xf = enAnillo(a, r + (rnd() - 0.5) * 0.12, (rnd() - 0.5) * 0.25);
        M.extruir(gH, perfil, 1.1, caida ? (x, y, z) => xf(y - 0.2, -x + 0.17, z) : xf, 1.5);
      }
      th += (n * 1.15) / R_BARRICADA + 0.08;
    } else if (tipo < 0.8) { // pared de bolsas de arena en arco
      const largo = 1.4 + rnd() * 0.8, n = Math.floor(largo / 0.26);
      for (let fila = 0; fila < 3; fila++) for (let k = 0; k < n - fila; k++) {
        const a = th + ((k + fila * 0.5) * 0.26) / r, xf = enAnillo(a, r + (rnd() - 0.5) * 0.04, (rnd() - 0.5) * 0.2, fila * 0.085);
        M.caja(gB, -0.07, 0, 0.07, 0.09, 0.25, xf, 2);
      }
      th += largo / r + 0.06;
    } else if (tipo < 0.88) { // tachos apagados
      tacho(th + 0.2 / r, r - 0.05);
      if (rnd() < 0.5) tacho(th + 0.55 / r, r + 0.1);
      th += 0.8 / r;
    } else { // auto quemado, cruzado; el primero todavía arde
      const vuelco = rnd() < 0.3;
      auto(th + 1.4 / r, r + 0.35, (rnd() - 0.5) * 0.6, vuelco);
      if (!ocupado.length) fuegos.push({ th: th + 1.25 / r, r: r + 0.3, alto: vuelco ? 0.95 : 0.72, grande: 1.7 });
      ocupado.push([th - 0.05, th + 3 / r]);
      th += 2.9 / r;
    }
  }
  fuegos.push(...huecosFuego.map(([a, r]) => ({ th: a, r, alto: 0.5, grande: 1 })));
  return { M, ocupado };
}

// ---------- fuegos: llamas que titilan y el resplandor en el piso (aditivos, animados) ----------
function fuegoTex() {
  const L = new Lienzo(256, 256);
  L.cada((x, y) => {
    if (x < 128) { // llama: lengua que se afina hacia arriba, núcleo amarillo
      const u = (x + 0.5) / 128 - 0.5, v = 1 - (y + 0.5) / 256, ancho = 0.42 * Math.pow(1 - v, 0.7) * (1 - 0.25 * Math.sin(v * 6)) + 0.02;
      const n = fbm(x / 14, y / 22, 4, 801) * 0.5 + 0.5, d = Math.abs(u + 0.06 * Math.sin(v * 9 + n * 3)) / ancho;
      const k = sat(1 - d) * suave(0, 0.08, v) * (0.6 + 0.6 * n) * (1 - suave(0.75, 1, v));
      const c = mezclar(mezclar(hex('#5a0c02'), hex('#ff6a10'), sat(k * 1.6)), hex('#fff1a8'), sat((k - 0.55) * 2.2));
      return [...c, sat(k * 1.3)];
    }
    const cy = y < 128 ? 64 : 192, r = Math.hypot(x + 0.5 - 192, y + 0.5 - cy) / 64; // arriba resplandor del piso, abajo halo
    const k = y < 128 ? Math.exp(-r * r * 3.2) * (1 - suave(0.85, 1, r)) : Math.exp(-r * r * 6) * (1 - suave(0.8, 1, r));
    return [...mezclar(hex('#ff5a14'), hex('#ffb04a'), k), k * (y < 128 ? 0.55 : 0.8)];
  });
  textura('tex_f155', L);
}
// Un fuego con la base de la llama a la altura `alto`: resplandor en el piso (hueso 0, quieto) y halo + dos llamas
// cruzadas (hueso 1, titilan alrededor de la base).
export function fuegoMalla(alto, k) {
  const M = new Malla(), g = M.material('tex_f155.bmt'), huesos = [], b = alto - 0.04;
  M.cara(g, [[-0.8 * k, 0.008, 0.8 * k], [0.8 * k, 0.008, 0.8 * k], [0.8 * k, 0.008, -0.8 * k], [-0.8 * k, 0.008, -0.8 * k]], [[0.5, 0], [1, 0], [1, 0.5], [0.5, 0.5]]);
  while (huesos.length < M.vs.length) huesos.push(0);
  M.cara(g, [[-0.45 * k, b - 0.21 * k, 0], [0.45 * k, b - 0.21 * k, 0], [0.45 * k, b + 0.59 * k, 0], [-0.45 * k, b + 0.59 * k, 0]], [[0.5, 1], [1, 1], [1, 0.5], [0.5, 0.5]]); // mira al centro
  for (const a of [0, Math.PI / 2]) {
    const q = [[-0.17 * k, b, 0], [0.17 * k, b, 0], [0.17 * k, b + 0.52 * k, 0], [-0.17 * k, b + 0.52 * k, 0]].map((p) => rotY(p, a));
    M.cara(g, q, [[0, 1], [0.5, 1], [0.5, 0], [0, 0]]);
    M.cara(g, [...q].reverse(), [[0, 0], [0.5, 0], [0.5, 1], [0, 1]]);
  }
  while (huesos.length < M.vs.length) huesos.push(1);
  const n = M.aNmd('');
  return { vs: n.vs, is: n.is, huesos };
}
function fuegos(lista) {
  const malla = { ...fuegoMalla(0.5, 1), variante: (f) => fuegoMalla(f.radio, f.grande) };
  const figuras = lista.map((f, k) => {
    const x = f.r * Math.cos(f.th), z = f.r * Math.sin(f.th);
    return { x, z, rumbo: Math.atan2(-x, -z), escala: 1, tipo: 2, fase: k * 0.37, vel: 1.3 + 0.2 * (k % 3), radio: f.alto, grande: f.grande };
  });
  return animado('mod_f149', figuras, malla, [[0, 0, 0, -1], [0, 0, 0, 0]], (cant) => [{ nombre: '', col: [1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 0, 0], tex: 'tex_f155.bmt', ini: 0, n: cant }]);
}

// Agrega el escenario 10 a `campo` y pone su miniatura y su nombre en la fila 8 de tex_sys42 y tex_sys24 (las dos ya
// con las filas de la rueda, ver rueda() en comun.mjs).
export function undead(campo, s42, s24) {
  cielo();
  luna('tex_f142', [1, 0.45, 0.32], [0.55, 0.13, 0.1], [0.9, 0.22, 0.16]);
  teñir(leerBmt(sacar('obj/tex_f010.bmt')), SUELO, 'tex_f143');
  teñir(leerBmt(sacar('obj/tex_f015.bmt')), CIUDAD, 'tex_f144');
  teñir(leerBmt(sacar('obj/tex_f022.bmt')), SUELO.map(([t, c]) => [t, c.map((v) => v * 1.1)]), 'tex_f145');
  teñir(leerBmt(sacar('obj/tex_f023.bmt')), CIUDAD, 'tex_f146');
  teñir(leerBmt(sacar('obj/tex_f024.bmt')), CIUDAD, 'tex_f147');
  teñir(leerBmt(sacar('obj/tex_f025.bmt')), [[0, hex('#000000')], [1, hex('#8a3a20')]], 'tex_f148');
  atlasZombis(); sombraTex(); hormigon(); bolsas(); chapa(); fuegoTex();

  modelo(domo('tex_f140.bmt', 'tex_f141.bmt').aNmd('mod_f140'));
  modelo(lunaQuad(LUNA, 'tex_f142.bmt').aNmd('mod_f141'));
  modelo(copia('mod_f010', 'mod_f142', (m) => { m.mats[0].tex = 'tex_f143.bmt'; }));
  const TEX = { 'tex_f015.bmt': 'tex_f144.bmt', 'tex_f022.bmt': 'tex_f145.bmt', 'tex_f023.bmt': 'tex_f146.bmt', 'tex_f024.bmt': 'tex_f147.bmt' };
  modelo(copia('mod_f012', 'mod_f143', (m) => { for (const t of m.mats) t.tex = TEX[t.tex] ?? t.tex; }));
  modelo(copia('mod_f014', 'mod_f144', (m) => { m.mats[0].tex = 'tex_f148.bmt'; }));
  modelo(copia('mod_f013', 'mod_f145', (m) => { m.mats[0].col.splice(0, 3, 0.6, 0.32, 0.3); })); // bruma rojiza sobre el piso
  const listaFuegos = [], b = barricadas(listaFuegos);
  modelo(b.M.aNmd('mod_f146'));
  const h = horda(b.ocupado);
  modelo(h.zm, h.zv); modelo(h.sm, h.sv);
  const [fm, fv] = fuegos(listaFuegos);
  modelo(fm, fv);

  const M = (nombre, tipo, z, escribeZ, mezcla) => ({ nombre: `${nombre}.nmd`, tipo, z, escribeZ, mezcla, pruebaAlfa: 0 });
  const e = {
    borrado: [0, 0, 0, 0], escala: 0.01,
    modelos: [
      M('mod_f142', 1, 1, 1, 0), // piso: el primero, su textura se corre con la pelea
      M('mod_f143', 1, 1, 1, 0), // edificios y dunas
      M('mod_f145', 1, 0, 0, 2), // bruma sobre el piso (como el mod_f013 de Lost Grave)
      M('mod_f146', 1, 1, 1, 0), // barricadas
      M('mod_f147', 1, 1, 1, 0), // zombis (animados)
      M('mod_f148', 3, 1, 0, 2), // sus sombras (animadas)
      M('mod_f140', 3, 1, 0, 0), // cielo
      M('mod_f141', 3, 1, 0, 2), // luna
      M('mod_f144', 3, 1, 0, 2), // bruma del horizonte
      M('mod_f149', 3, 1, 0, 3), // fuegos (animados, aditivos)
    ],
    // sin partículas: el polvo (1, 2) tapa a los zombis, 4 es un resplandor de atardecer y 5 las rocas del final
    efecto: 0, efecto2: 0, difusa: [1, 0.66, 0.58, 1], ambiente: [0.36, 0.32, 0.44, 1], dir: [0.15, -0.5, 0.35], viento: [10, 0, 0],
  };
  if (campo.length !== ESCENARIO) throw new Error(`field.dat tiene ${campo.length} escenarios (se esperaban ${ESCENARIO})`);
  campo.push(e);

  miniatura(e, FILA, s42, 'vista-undead.png');
  const nombre = leerPng(readFileSync(new URL('nombre-undead.png', import.meta.url)));
  for (let y = 0; y < 32; y++) nombre.rgba.copy(s24.rgba, 4 * (FILA * 32 + y) * s24.w, 4 * y * nombre.w, 4 * (y + 1) * nombre.w);
}
