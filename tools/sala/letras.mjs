// La letra del lobby de salas (d3d/sala.c): las letras de los nombres de escenarios y personajes, de la pantalla
// de red y los dígitos de la de configuración, recortadas de las texturas del juego con su sombra (como el nombre
// de Sunlit Shore en tools/escenario/playa.mjs). Salen en obj/tex_sala.bmt con las medidas en obj/sala.txt, que
// van en mods/vsobj2.dat junto con los escenarios.
//
//   node tools/sala/letras.mjs [--png carpeta]   (también lo corre tools/escenario/playa.mjs)
//
// sala.txt: "letras n separación espacio" y una línea por letra: código, x, y, ancho y alto en el atlas, corrimiento
// del recorte respecto del trazo (x) y de la línea base (y), y ancho del trazo (lo que avanza la pluma, más la
// separación).
import { writeFileSync, mkdirSync } from 'node:fs';
import { join } from 'node:path';
import { sacar } from '../escenario/moonlit.mjs';
import { leerBmt, escribirBmt, png } from '../escenario/bmt.mjs';

// [textura, banda (y desde, alto), x del trazo desde y hasta]. Todas blancas con sombra; los dígitos son los de
// tex_sys69, que la pantalla de red usa a tamaño natural (más altos que las mayúsculas).
const B24 = (f) => ['tex_sys24', f * 32, 32], B25 = (f) => ['tex_sys25', f * 32, 32];
const RED = ['tex_sys79', 40, 30], RED2 = ['tex_sys79', 130, 30], AYUDA = ['tex_sys79', 158, 22], DIG = ['tex_sys69', 478, 30];
const L = (b, x0, x1, escala = 1, avance) => ({ banda: b, x0, x1, escala, avance });
export const LETRAS = {
  A: L(B25(0), 130, 148), B: L(B24(0), 171, 188), C: L(B25(3), 5, 23), D: L(RED2, 111, 131), G: L(B24(0), 85, 103),
  H: L(B24(3), 161, 184), K: L(B25(1), 132, 153), L: L(B24(0), 22, 39), M: L(B25(2), 131, 157), O: L(B24(3), 52, 70),
  P: L(B24(6), 161, 177), R: L(B25(0), 4, 21), S: L(B24(1), 158, 170), T: L(B24(6), 45, 62), W: L(B24(4), 69, 93),
  a: L(B24(0), 115, 126), c: L(B24(1), 173, 184), d: L(B24(4), 177, 191), e: L(B24(0), 141, 152), f: L(B24(6), 140, 152),
  g: L(RED, 33, 47), h: L(B24(2), 185, 198), i: L(B24(2), 200, 208), k: L(B25(1), 167, 182), l: L(B24(0), 189, 197),
  m: L(B24(7), 156, 178), n: L(B24(4), 160, 173), o: L(B24(0), 41, 53), r: L(B24(0), 104, 114), s: L(B24(0), 56, 65),
  t: L(B24(0), 69, 75), u: L(B24(0), 200, 211), v: L(B24(0), 128, 140), w: L(B24(2), 166, 185), y: L(B24(6), 201, 215),
  '-': L(B24(0), 162, 169), '/': L(RED, 93, 104), ':': L(RED2, 182, 187), '(': L(B24(7), 61, 70), ')': L(B24(7), 187, 196),
  // De la ayuda chica: se agranda (y se espacia como allá, cada 7 px) para que achicada como la ayuda quede igual.
  '·': L(AYUDA, 81, 83, 1.2, 8),
  ...Object.fromEntries([[3, 15], [22, 32], [41, 54], [61, 74], [82, 94], [102, 116], [122, 136], [143, 156], [162, 176], [181, 196]]
    .map(([x0, x1], d) => [String(d), L(DIG, x0, x1)])),
};
export const SEPARACION = 0.5, ESPACIO = 8; // en los nombres del juego los trazos quedan a 0-1 px

// Las ayudas de arriba a la derecha van en la cursiva fina y chica de las de la pantalla de red (tex_sys79), armadas
// con pedazos de ellas; la A y la P no están ahí y salen de las de Replay (tex_sys92), achicadas a la misma altura.
// Cada pedazo: [banda, x desde, x hasta] y el hueco hasta el siguiente (los de las ayudas originales).
const AY1 = ['tex_sys79', 158, 22], AY2 = ['tex_sys79', 179, 23], AYR = ['tex_sys92', 449, 21];
const P = ([b, x0, x1, escala = 1], hueco = 1) => ({ banda: b, x0, x1, escala, hueco });
const BUTTON = [AY1, 2, 57], PUNTOS = [AY1, 81, 97], A_ = [AYR, 66, 78, 0.92], B_ = [AY1, 2, 13], C_ = [AY1, 66, 78], D_ = [AY2, 66, 79];
const S_ = [AY2, 101, 111], t_ = [AY2, 148, 153], a_ = [AY2, 189, 197], r_ = [AY1, 149, 155], n_ = [AY1, 49, 57], c_ = [AY2, 138, 145],
  e_ = [AY2, 113, 120], l_ = [AY2, 123, 127], o_ = [AY1, 39, 47], y_ = [AY2, 198, 208], P_ = [AYR, 102, 113, 0.92], D2 = [AY2, 159, 172];
const boton = (letra) => [P(BUTTON, 8), P(letra, 3), P(PUNTOS, 3)];
const palabra = (...letras) => letras.map((l) => P(l));
// El orden es el que usa d3d/sala.c.
export const AYUDAS = [
  ['Button A···Play', [...boton(A_), ...palabra(P_, l_, a_, y_)]],
  ['Button C···Server On/Off', [P([AY1, 2, 224])]],
  ['Button D···Select Delay', [P([AY2, 2, 208])]],
  ['Button A···Start', [...boton(A_), ...palabra(S_, t_, a_, r_, t_)]],
  ['Button B···Cancel', [...boton(B_), ...palabra(C_, a_, n_, c_, e_, l_)]],
  ['Button D···Player / Server', [...boton(D_), ...palabra(P_, l_, a_, y_, e_), P(r_, 6), P([AY1, 186, 193], 5), P([AY1, 100, 155])]],
  ['Button A···Done', [...boton(A_), ...palabra(D2, o_, n_, e_)]],
];
// Lo que escribe el lobby con esta letra (d3d/sala.c): tiene que estar todo.
export const TEXTOS = ['Players:', 'Rooms:', 'Room', 'Ping', 'Client', 'Server', 'Player', 'ms', 'vs', '0123456789/:-()'];

const texturas = new Map();
const textura = (n) => texturas.get(n) ?? texturas.set(n, leerBmt(sacar(`obj/${n}.bmt`))).get(n);

// Trazos blancos conexos de una banda; a cada letra le toca la tinta (y la sombra) más cercana a su trazo.
function banda(nombre, y0, h) {
  const t = textura(nombre), W = t.w, al = (x, y) => t.rgba[4 * ((y0 + y) * W + x) + 3];
  const blanco = (x, y) => al(x, y) > 128 && t.rgba[4 * ((y0 + y) * W + x)] > 140;
  const comp = new Int32Array(W * h).fill(-1), cajas = [];
  for (let y = 0; y < h; y++) for (let x = 0; x < W; x++) {
    if (comp[y * W + x] >= 0 || !blanco(x, y)) continue;
    const k = cajas.length, caja = [x, y, x, y], pila = [[x, y]]; comp[y * W + x] = k; cajas.push(caja);
    while (pila.length) {
      const [a, b] = pila.pop();
      caja[0] = Math.min(caja[0], a); caja[1] = Math.min(caja[1], b); caja[2] = Math.max(caja[2], a); caja[3] = Math.max(caja[3], b);
      for (let dy = -1; dy <= 1; dy++) for (let dx = -1; dx <= 1; dx++) {
        const u = a + dx, v = b + dy;
        if (u >= 0 && v >= 0 && u < W && v < h && comp[v * W + u] < 0 && blanco(u, v)) { comp[v * W + u] = k; pila.push([u, v]); }
      }
    }
  }
  const asignado = Int32Array.from(comp), cola = [];
  for (let i = 0; i < W * h; i++) if (asignado[i] >= 0) cola.push(i);
  for (let q = 0; q < cola.length; q++) {
    const i = cola[q], x = i % W, y = (i / W) | 0;
    for (const [dx, dy] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) {
      const u = x + dx, v = y + dy, j = v * W + u;
      if (u >= 0 && v >= 0 && u < W && v < h && asignado[j] < 0 && al(u, v) > 0) { asignado[j] = asignado[i]; cola.push(j); }
    }
  }
  // Línea base: el pie más común de las letras (las redondas bajan un píxel más: entre dos vecinos, el de arriba).
  const pies = new Map();
  for (const c of cajas) if (c[3] - c[1] >= 8) pies.set(c[3], (pies.get(c[3]) ?? 0) + 1);
  const [p1, p2] = [...pies].sort((a, b) => b[1] - a[1]);
  const base = p2 && Math.abs(p1[0] - p2[0]) === 1 && p2[1] >= p1[1] / 2 ? Math.min(p1[0], p2[0]) : p1[0];
  return { t, W, h, y0, cajas, asignado, base };
}

function glifo(ch, { banda: [nombre, y0, h], x0, x1, escala, avance }) {
  const b = banda(nombre, y0, h), mias = new Set();
  b.cajas.forEach((c, k) => { if (c[0] >= x0 - 1 && c[2] <= x1 + 1) mias.add(k); });
  if (!mias.size) throw new Error(`letras: no encontré '${ch}' en ${nombre}`);
  let gx0 = Infinity, gx1 = -1, gy0 = Infinity, gy1 = -1, sx0 = Infinity, sx1 = -1;
  for (const k of mias) { sx0 = Math.min(sx0, b.cajas[k][0]); sx1 = Math.max(sx1, b.cajas[k][2]); }
  for (let i = 0; i < b.W * h; i++) if (mias.has(b.asignado[i])) {
    const x = i % b.W, y = (i / b.W) | 0;
    gx0 = Math.min(gx0, x); gx1 = Math.max(gx1, x); gy0 = Math.min(gy0, y); gy1 = Math.max(gy1, y);
  }
  const w = gx1 - gx0 + 1, hh = gy1 - gy0 + 1, rgba = Buffer.alloc(w * hh * 4);
  for (let y = gy0; y <= gy1; y++) for (let x = gx0; x <= gx1; x++) {
    if (!mias.has(b.asignado[y * b.W + x])) continue;
    b.t.rgba.copy(rgba, 4 * ((y - gy0) * w + x - gx0), 4 * ((b.y0 + y) * b.W + x), 4 * ((b.y0 + y) * b.W + x) + 4);
  }
  const g = { ch, w, h: hh, rgba, cx: gx0 - sx0, cy: gy0 - b.base, sw: sx1 - sx0 + 1 };
  const r = escala === 1 ? g : escalar(g, escala);
  if (avance) r.sw = avance;
  return r;
}

function frase(piezas) {
  const gs = piezas.map((p) => ({ ...glifo('?', p), hueco: p.hueco }));
  let x = 0, x0 = Infinity, x1 = -Infinity, y0 = Infinity, y1 = -Infinity;
  const puestas = gs.map((g) => { const px = x + g.cx; x0 = Math.min(x0, px); x1 = Math.max(x1, px + g.w); y0 = Math.min(y0, g.cy); y1 = Math.max(y1, g.cy + g.h); x += g.sw + g.hueco; return [g, px]; });
  const w = x1 - x0, h = y1 - y0, rgba = Buffer.alloc(w * h * 4);
  for (const [g, px] of puestas) for (let yy = 0; yy < g.h; yy++) for (let xx = 0; xx < g.w; xx++) {
    const s = 4 * (yy * g.w + xx), d = 4 * ((g.cy - y0 + yy) * w + px - x0 + xx), a = g.rgba[s + 3] / 255, ad = rgba[d + 3] / 255 * (1 - a), ao = a + ad;
    if (ao <= 0) continue;
    for (let c = 0; c < 3; c++) rgba[d + c] = Math.round((g.rgba[s + c] * a + rgba[d + c] * ad) / ao);
    rgba[d + 3] = Math.round(ao * 255);
  }
  return { w, h, rgba, cx: x0, cy: y0, sw: x - gs.at(-1).hueco };
}

// Cambio de tamaño con filtro de área (alfa premultiplicado), para los dígitos y el punto de la ayuda.
function escalar(g, k) {
  const w = Math.max(1, Math.round(g.w * k)), h = Math.max(1, Math.round(g.h * k)), rgba = Buffer.alloc(w * h * 4);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
    const ax = x / k, bx = (x + 1) / k, ay = y / k, by = (y + 1) / k, s = [0, 0, 0, 0];
    let area = 0;
    for (let sy = Math.floor(ay); sy < Math.ceil(by); sy++) for (let sx = Math.floor(ax); sx < Math.ceil(bx); sx++) {
      const f = (Math.min(bx, sx + 1) - Math.max(ax, sx)) * (Math.min(by, sy + 1) - Math.max(ay, sy));
      if (f <= 0) continue;
      area += f;
      if (sx >= g.w || sy >= g.h) continue;
      const i = 4 * (sy * g.w + sx), a = g.rgba[i + 3] / 255;
      for (let c = 0; c < 3; c++) s[c] += g.rgba[i + c] * a * f;
      s[3] += a * f;
    }
    const a = s[3] / area, i = 4 * (y * w + x);
    for (let c = 0; c < 3; c++) rgba[i + c] = s[3] ? Math.round(s[c] / s[3]) : 0;
    rgba[i + 3] = Math.round(a * 255);
  }
  return { ...g, w, h, rgba, cx: Math.round(g.cx * k), cy: Math.round(g.cy * k), sw: Math.round(g.sw * k) };
}

export function atlasLetras() {
  for (const t of TEXTOS) for (const ch of t) if (ch !== ' ' && !LETRAS[ch]) throw new Error(`letras: falta '${ch}' (de "${t}")`);
  const glifos = Object.entries(LETRAS).map(([ch, d]) => glifo(ch, d));
  const frases = AYUDAS.map(([texto, piezas], i) => ({ ...frase(piezas), texto, id: i }));
  const todo = [...glifos, ...frases].sort((a, b) => b.h - a.h);
  const W = 512, PAD = 2;
  let x = PAD, y = PAD, fila = 0;
  for (const g of todo) { // estantes
    if (x + g.w + PAD > W) { x = PAD; y += fila + PAD; fila = 0; }
    g.ax = x; g.ay = y; x += g.w + PAD; fila = Math.max(fila, g.h);
  }
  let H = 32;
  while (H < y + fila + PAD) H *= 2;
  const rgba = Buffer.alloc(W * H * 4);
  for (const g of todo) for (let yy = 0; yy < g.h; yy++) g.rgba.copy(rgba, 4 * ((g.ay + yy) * W + g.ax), 4 * yy * g.w, 4 * (yy + 1) * g.w);
  const lineas = [`letras ${glifos.length} ${SEPARACION} ${ESPACIO}`];
  for (const g of glifos) lineas.push([g.ch.charCodeAt(0), g.ax, g.ay, g.w, g.h, g.cx, g.cy, g.sw].join(' '));
  lineas.push(`frases ${frases.length}`);
  for (const f of frases) lineas.push([f.id, f.ax, f.ay, f.w, f.h, f.cx, f.cy, f.sw].join(' ') + ` ${f.texto}`);
  return { w: W, h: H, rgba, glifos, frases, texto: lineas.join('\n') + '\n' };
}

// Lo que se suma a vsobj2.dat.
export function archivosSala() {
  const a = atlasLetras();
  return [{ nombre: 'obj/tex_sala.bmt', datos: escribirBmt(a) }, { nombre: 'obj/sala.txt', datos: Buffer.from(a.texto, 'latin1') }];
}

// Vista previa: el atlas y algunos textos escritos con la misma regla que d3d/sala.c, sobre el fondo de la red.
export function vistaPrevia(carpeta) {
  const a = atlasLetras(), fondo = textura('tex_sys19'), W = 800, H = 240, img = Buffer.alloc(W * H * 4);
  for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
    const i = 4 * (Math.floor((y + 40) * 512 / 600) * 512 + Math.floor(x * 512 / 800));
    for (let c = 0; c < 3; c++) img[4 * (y * W + x) + c] = Math.round(fondo.rgba[i + c] * fondo.rgba[i + 3] / 255);
    img[4 * (y * W + x) + 3] = 255;
  }
  const porCh = new Map(a.glifos.map((g) => [g.ch, g]));
  const escribir = (texto, x, base, k = 1) => {
    for (const ch of texto) {
      if (ch === ' ') { x += ESPACIO * k; continue; }
      const g = porCh.get(ch);
      for (let yy = 0; yy < Math.round(g.h * k); yy++) for (let xx = 0; xx < Math.round(g.w * k); xx++) {
        const sx = Math.min(g.w - 1, Math.floor(xx / k)), sy = Math.min(g.h - 1, Math.floor(yy / k)), s = 4 * (sy * g.w + sx);
        const px = Math.round(x + g.cx * k) + xx, py = Math.round(base + g.cy * k) + yy, al = g.rgba[s + 3] / 255;
        if (px < 0 || py < 0 || px >= W || py >= H) continue;
        for (let c = 0; c < 3; c++) img[4 * (py * W + px) + c] = Math.round(img[4 * (py * W + px) + c] * (1 - al) + g.rgba[s + c] * al);
      }
      x += (g.sw + SEPARACION) * k;
    }
  };
  escribir('Players: 12', 20, 40); escribir('Room', 300, 40); escribir('Ping 34 ms', 450, 40); escribir('Client', 650, 40);
  a.frases.forEach((f, i) => { // las ayudas, tal cual
    const x0 = 20 + (i % 2) * 380, base = 80 + Math.floor(i / 2) * 24;
    for (let yy = 0; yy < f.h; yy++) for (let xx = 0; xx < f.w; xx++) {
      const s = 4 * ((f.ay + yy) * a.w + f.ax + xx), al = a.rgba[s + 3] / 255, d = 4 * ((base + f.cy + yy) * W + x0 + f.cx + xx);
      for (let c = 0; c < 3; c++) img[d + c] = Math.round(img[d + c] * (1 - al) + a.rgba[s + c] * al);
    }
  });
  escribir('Server  Player  Rooms: 3  0123456789  (Room)  10/ 12', 20, 200);
  mkdirSync(carpeta, { recursive: true });
  writeFileSync(join(carpeta, 'letras-atlas.png'), png({ w: a.w, h: a.h, rgba: a.rgba }));
  writeFileSync(join(carpeta, 'letras-muestra.png'), png({ w: W, h: H, rgba: img }));
}

if (import.meta.url === `file:///${process.argv[1]?.replaceAll('\\', '/')}`) {
  const i = process.argv.indexOf('--png');
  const a = atlasLetras();
  console.log(`letras: ${a.glifos.length} en un atlas de ${a.w}x${a.h}`);
  if (i > 0) vistaPrevia(process.argv[i + 1]);
}
