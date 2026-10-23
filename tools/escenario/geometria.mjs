// Mallas para modelos nmd_: caras planas, extrusión de polígonos, cilindros y alturas.
// La cara visible es la de la normal (b - a) x (c - a), como en los modelos del juego.
export const resta = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
export const suma = (a, b) => [a[0] + b[0], a[1] + b[1], a[2] + b[2]];
export const escalar = (a, k) => [a[0] * k, a[1] * k, a[2] * k];
export const cruz = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
export const punto = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
export const unit = (a) => { const l = Math.hypot(...a) || 1; return [a[0] / l, a[1] / l, a[2] / l]; };

// Rotaciones de puntos (radianes); se componen a mano en cada transformación.
export const rotY = (p, a) => [p[0] * Math.cos(a) + p[2] * Math.sin(a), p[1], -p[0] * Math.sin(a) + p[2] * Math.cos(a)];
export const rotZ = (p, a) => [p[0] * Math.cos(a) - p[1] * Math.sin(a), p[0] * Math.sin(a) + p[1] * Math.cos(a), p[2]];
export const rotX = (p, a) => [p[0], p[1] * Math.cos(a) - p[2] * Math.sin(a), p[1] * Math.sin(a) + p[2] * Math.cos(a)];

// Triangulación por orejas de un polígono simple antihorario (en el plano xy).
export function triangular(pts) {
  const area2 = (a, b, c) => (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
  const idx = pts.map((_, i) => i), tris = [];
  while (idx.length > 3) {
    let corte = -1;
    for (let k = 0; k < idx.length && corte < 0; k++) {
      const i0 = idx[(k + idx.length - 1) % idx.length], i1 = idx[k], i2 = idx[(k + 1) % idx.length];
      const [a, b, c] = [pts[i0], pts[i1], pts[i2]];
      if (area2(a, b, c) <= 1e-9) continue;
      if (!idx.some((j) => j !== i0 && j !== i1 && j !== i2 && area2(a, b, pts[j]) >= 0 && area2(b, c, pts[j]) >= 0 && area2(c, a, pts[j]) >= 0)) corte = k;
    }
    if (corte < 0) break; // lo que queda es degenerado
    const n = idx.length;
    tris.push([idx[(corte + n - 1) % n], idx[corte], idx[(corte + 1) % n]]);
    idx.splice(corte, 1);
  }
  if (idx.length === 3) tris.push(idx.slice());
  return tris;
}
// Saca puntos repetidos y alineados: con ellos el corte de orejas se traba y deja caras sin tapar.
export function limpiar(pts) {
  let p = pts.slice(), cambio = true;
  while (cambio && p.length > 3) {
    cambio = false;
    for (let i = 0; i < p.length && p.length > 3; i++) {
      const a = p[(i + p.length - 1) % p.length], b = p[i], c = p[(i + 1) % p.length];
      if (Math.abs((b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])) < 1e-9) { p.splice(i, 1); cambio = true; i--; }
    }
  }
  return p;
}
export const areaFirmada = (pts) => pts.reduce((s, p, i) => { const q = pts[(i + 1) % pts.length]; return s + p[0] * q[1] - q[0] * p[1]; }, 0) / 2;

export class Malla {
  constructor() { this.vs = []; this.grupos = []; }
  material(tex, col = [1, 1, 1, 1], extra = [0, 0, 0, 0, 1, 1, 1, 0, 0]) {
    let g = this.grupos.find((g) => g.tex === tex && g.col.join() === col.join());
    if (!g) this.grupos.push(g = { tex, col, extra, tris: [] });
    return g;
  }
  vertice(p, n, uv) { this.vs.push({ p, n, c: 0xffffffff, uv }); return this.vs.length - 1; }
  // Polígono plano convexo en abanico, con normal plana.
  cara(g, ps, uvs) {
    const n = unit(cruz(resta(ps[1], ps[0]), resta(ps[2], ps[0])));
    const ids = ps.map((p, i) => this.vertice(p, n, uvs[i]));
    for (let i = 1; i + 1 < ids.length; i++) g.tris.push(ids[0], ids[i], ids[i + 1]);
  }
  // Polígono 2D (x, y) extruido en z de -t/2 a t/2; xf lleva (x, y, z) local al mundo.
  extruir(g, pts, t, xf, esc = 0.25) {
    pts = limpiar(pts);
    if (areaFirmada(pts) < 0) pts = pts.reverse();
    const tris = triangular(pts);
    for (const lado of [1, -1]) {
      const P = pts.map(([x, y]) => xf(x, y, (lado * t) / 2));
      const n = unit(cruz(resta(xf(1, 0, 0), xf(0, 0, 0)), resta(xf(0, 1, 0), xf(0, 0, 0))).map((v) => v * lado));
      const ids = P.map((p, i) => this.vertice(p, n, [pts[i][0] * esc * lado, -pts[i][1] * esc]));
      for (const [a, b, c] of tris) lado > 0 ? g.tris.push(ids[a], ids[b], ids[c]) : g.tris.push(ids[a], ids[c], ids[b]);
    }
    let u = 0;
    for (let i = 0; i < pts.length; i++) {
      const [ax, ay] = pts[i], [bx, by] = pts[(i + 1) % pts.length], l = Math.hypot(bx - ax, by - ay);
      if (l < 1e-6) continue;
      this.cara(g, [xf(ax, ay, t / 2), xf(ax, ay, -t / 2), xf(bx, by, -t / 2), xf(bx, by, t / 2)],
        [[u * esc, 0], [u * esc, t * esc], [(u + l) * esc, t * esc], [(u + l) * esc, 0]]);
      u += l;
    }
  }
  caja(g, x0, y0, x1, y1, t, xf, esc) { this.extruir(g, [[x0, y0], [x1, y0], [x1, y1], [x0, y1]], t, xf, esc); }
  // Tubo con radio variable por una polilínea 3D (ramas, tallos), normales suaves.
  tubo(g, puntos, radios, lados = 6, vRep = 1) {
    const anillos = puntos.map((p, i) => {
      const dir = unit(resta(puntos[Math.min(i + 1, puntos.length - 1)], puntos[Math.max(i - 1, 0)]));
      const e1 = unit(cruz(dir, Math.abs(dir[1]) < 0.9 ? [0, 1, 0] : [1, 0, 0])), e2 = cruz(e1, dir);
      return Array.from({ length: lados + 1 }, (_, k) => {
        const a = (k / lados) * 2 * Math.PI, n = suma(escalar(e1, Math.cos(a)), escalar(e2, Math.sin(a)));
        return this.vertice(suma(p, escalar(n, radios[i])), n, [k / lados, (i / (puntos.length - 1)) * vRep]);
      });
    });
    for (let i = 0; i + 1 < anillos.length; i++) for (let k = 0; k < lados; k++) {
      const a = anillos[i][k], b = anillos[i][k + 1], c = anillos[i + 1][k + 1], d = anillos[i + 1][k];
      g.tris.push(a, d, c, a, c, b);
    }
  }
  aNmd(nombre) {
    const is = [], mats = [];
    for (const g of this.grupos) {
      if (!g.tris.length) continue;
      mats.push({ nombre: '', col: [...g.col, ...g.extra], tex: g.tex, ini: is.length / 3, n: g.tris.length / 3 });
      is.push(...g.tris);
    }
    if (this.vs.length > 65535) throw new Error(`${nombre}: ${this.vs.length} vértices (máx. 65535)`);
    return { nombre, mats, vs: this.vs, is };
  }
}
