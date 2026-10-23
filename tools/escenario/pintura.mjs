// Pintura procedural para texturas de escenarios: lienzo RGBA en floats (alfa directo), ruido de Perlin
// periódico, trazos y polígonos con antialias, desenfoque y salida a BMT/PNG con tramado.
import { escribirBmt, png } from './bmt.mjs';

export const sat = (v) => (v < 0 ? 0 : v > 1 ? 1 : v);
export const mezclar = (a, b, t) => a.map((v, i) => v + (b[i] - v) * t);
export const suave = (a, b, x) => { const t = sat((x - a) / (b - a)); return t * t * (3 - 2 * t); };
export const hex = (s) => [1, 3, 5].map((i) => parseInt(s.slice(i, i + 2), 16) / 255);

export function azar(semilla) { // mulberry32
  let s = semilla >>> 0;
  return () => { s = (s + 0x6d2b79f5) >>> 0; let t = Math.imul(s ^ (s >>> 15), 1 | s); t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t; return ((t ^ (t >>> 14)) >>> 0) / 4294967296; };
}

function hash(i, j, s) {
  let h = Math.imul(i, 0x27d4eb2d) ^ Math.imul(j, 0x165667b1) ^ Math.imul(s + 1, 0x9e3779b9);
  h = Math.imul(h ^ (h >>> 15), 0x85ebca6b); h ^= h >>> 13; h = Math.imul(h, 0xc2b2ae35);
  return ((h ^ (h >>> 16)) >>> 0) / 4294967296;
}

// Perlin 2D en [-1, 1]; con px/py > 0 se repite cada px/py unidades.
export function perlin(x, y, s = 0, px = 0, py = 0) {
  const xi = Math.floor(x), yi = Math.floor(y), fx = x - xi, fy = y - yi;
  const g = (i, j, dx, dy) => {
    const a = hash(px ? ((i % px) + px) % px : i, py ? ((j % py) + py) % py : j, s) * 2 * Math.PI;
    return Math.cos(a) * dx + Math.sin(a) * dy;
  };
  const n00 = g(xi, yi, fx, fy), n10 = g(xi + 1, yi, fx - 1, fy), n01 = g(xi, yi + 1, fx, fy - 1), n11 = g(xi + 1, yi + 1, fx - 1, fy - 1);
  const u = fx * fx * fx * (fx * (fx * 6 - 15) + 10), v = fy * fy * fy * (fy * (fy * 6 - 15) + 10);
  return (n00 + (n10 - n00) * u + (n01 - n00) * v + (n00 - n10 - n01 + n11) * u * v) * 1.41;
}

export function fbm(x, y, oct = 5, s = 0, px = 0, py = 0, ganancia = 0.5) {
  let v = 0, a = 1, f = 1, t = 0;
  for (let o = 0; o < oct; o++) { v += a * perlin(x * f, y * f, s + 101 * o, px * f, py * f); t += a; a *= ganancia; f *= 2; }
  return v / t;
}

export class Lienzo {
  constructor(w, h, fondo = [0, 0, 0, 0]) {
    this.w = w; this.h = h; this.d = new Float32Array(w * h * 4);
    for (let i = 0; i < w * h; i++) this.d.set(fondo, 4 * i);
  }
  // Composición "over" de un color con alfa a (ya multiplicado por la cobertura).
  sobre(x, y, c, a) {
    if (x < 0 || y < 0 || x >= this.w || y >= this.h || a <= 0) return;
    const d = this.d, i = 4 * (y * this.w + x), ad = d[i + 3] * (1 - a), ao = a + ad;
    for (let k = 0; k < 3; k++) d[i + k] = (c[k] * a + d[i + k] * ad) / ao;
    d[i + 3] = ao;
  }
  // Suma luz (para brillos sobre fondo opaco).
  sumar(x, y, c, a) {
    if (x < 0 || y < 0 || x >= this.w || y >= this.h) return;
    const i = 4 * (y * this.w + x);
    for (let k = 0; k < 3; k++) this.d[i + k] = Math.min(1, this.d[i + k] + c[k] * a);
  }
  cada(f) { // f(x, y) -> [r, g, b, a]
    for (let y = 0; y < this.h; y++) for (let x = 0; x < this.w; x++) this.d.set(f(x, y), 4 * (y * this.w + x));
  }
  leer(x, y) { const i = 4 * (y * this.w + x); return [this.d[i], this.d[i + 1], this.d[i + 2], this.d[i + 3]]; }
  recorte(x0, y0, w, h) {
    const l = new Lienzo(w, h);
    for (let y = 0; y < h; y++) l.d.set(this.d.subarray(4 * ((y0 + y) * this.w + x0), 4 * ((y0 + y) * this.w + x0 + w)), 4 * y * w);
    return l;
  }
  // Tramado de Bayer 4x4 a `bits` por canal: evita bandas en los degradés oscuros y achica la paleta del BMT.
  rgba8(bits = 8) {
    const B = [0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5], q = (1 << bits) - 1, o = Buffer.alloc(this.w * this.h * 4);
    for (let y = 0; y < this.h; y++) for (let x = 0; x < this.w; x++) {
      const i = y * this.w + x, t = (B[(y & 3) * 4 + (x & 3)] + 0.5) / 16;
      for (let k = 0; k < 4; k++) o[4 * i + k] = Math.round(Math.floor(sat(this.d[4 * i + k]) * q + t) * 255 / q);
    }
    return o;
  }
  // BMT con la menor cantidad de bits que deje la paleta en índices de 2 bytes (máximo 6 bits por canal).
  bmt() {
    for (let bits = 6; ; bits--) {
      const rgba = this.rgba8(bits), u = new Set();
      for (let i = 0; i < rgba.length && u.size <= 65536; i += 4) u.add(rgba.readUInt32LE(i));
      if (u.size <= 65536 || bits === 4) return escribirBmt({ w: this.w, h: this.h, rgba });
    }
  }
  png() { return png({ w: this.w, h: this.h, rgba: this.rgba8() }); }
}

// Segmento con radio variable y antialias (ramas, tallos, filamentos).
export function trazo(L, x0, y0, r0, x1, y1, r1, c, a = 1) {
  const dx = x1 - x0, dy = y1 - y0, l2 = dx * dx + dy * dy || 1e-9, rm = Math.max(r0, r1) + 1.5;
  for (let y = Math.max(0, Math.floor(Math.min(y0, y1) - rm)); y <= Math.min(L.h - 1, Math.ceil(Math.max(y0, y1) + rm)); y++) {
    for (let x = Math.max(0, Math.floor(Math.min(x0, x1) - rm)); x <= Math.min(L.w - 1, Math.ceil(Math.max(x0, x1) + rm)); x++) {
      const px = x + 0.5, py = y + 0.5, t = sat(((px - x0) * dx + (py - y0) * dy) / l2);
      const dist = Math.hypot(px - x0 - t * dx, py - y0 - t * dy), r = r0 + (r1 - r0) * t, w = Math.max(r, 0.5);
      const cob = sat(w + 0.5 - dist) * (r / w);
      if (cob > 0) L.sobre(x, y, c, a * cob);
    }
  }
}

// Polígono simple relleno con antialias (4 subfilas por píxel, cobertura horizontal exacta). wrap: repite en x.
export function poligono(L, pts, c, a = 1, wrap = false) {
  const ys = pts.map((p) => p[1]), y0 = Math.max(0, Math.floor(Math.min(...ys))), y1 = Math.min(L.h - 1, Math.ceil(Math.max(...ys)));
  const xs = pts.map((p) => p[0]), xmin = Math.floor(Math.min(...xs)), xmax = Math.ceil(Math.max(...xs));
  const fila = new Float32Array(xmax - xmin + 2);
  for (let y = y0; y <= y1; y++) {
    fila.fill(0);
    for (let s = 0; s < 4; s++) {
      const sy = y + (s + 0.5) / 4, cortes = [];
      for (let i = 0; i < pts.length; i++) {
        const [ax, ay] = pts[i], [bx, by] = pts[(i + 1) % pts.length];
        if ((ay <= sy) !== (by <= sy)) cortes.push(ax + ((sy - ay) / (by - ay)) * (bx - ax));
      }
      cortes.sort((p, q) => p - q);
      for (let k = 0; k + 1 < cortes.length; k += 2) {
        const xa = cortes[k] - xmin, xb = cortes[k + 1] - xmin;
        for (let x = Math.floor(xa); x < Math.ceil(xb); x++) fila[x] += (Math.min(xb, x + 1) - Math.max(xa, x)) / 4;
      }
    }
    for (let i = 0; i < fila.length; i++) if (fila[i] > 0) {
      const x = xmin + i;
      L.sobre(wrap ? ((x % L.w) + L.w) % L.w : x, y, c, a * Math.min(1, fila[i]));
    }
  }
}

// Desenfoque gaussiano aproximado (3 cajas) sobre el lienzo premultiplicado; wrap repite en x.
export function desenfocar(L, radio, wrap = false) {
  const { w, h, d } = L, n = w * h;
  for (let i = 0; i < n; i++) for (let k = 0; k < 3; k++) d[4 * i + k] *= d[4 * i + 3];
  const tmp = new Float32Array(Math.max(w, h) * 4), r = Math.max(1, Math.round(radio / 1.7));
  const pasada = (len, idx, envolver) => {
    for (let k = 0; k < 4; k++) {
      let acc = 0;
      for (let j = -r; j <= r; j++) acc += d[idx(envolver ? ((j % len) + len) % len : Math.min(len - 1, Math.max(0, j))) + k];
      for (let i = 0; i < len; i++) {
        tmp[4 * i + k] = acc / (2 * r + 1);
        const sale = i - r, entra = i + r + 1;
        acc += d[idx(envolver ? ((entra % len) + len) % len : Math.min(len - 1, entra)) + k] - d[idx(envolver ? ((sale % len) + len) % len : Math.max(0, sale)) + k];
      }
    }
    for (let i = 0; i < len; i++) for (let k = 0; k < 4; k++) d[idx(i) + k] = tmp[4 * i + k];
  };
  for (let vez = 0; vez < 3; vez++) {
    for (let y = 0; y < h; y++) pasada(w, (x) => 4 * (y * w + x), wrap);
    for (let x = 0; x < w; x++) pasada(h, (y) => 4 * (y * w + x), false);
  }
  for (let i = 0; i < n; i++) { const a = d[4 * i + 3]; if (a > 1e-6) for (let k = 0; k < 3; k++) d[4 * i + k] /= a; }
}
