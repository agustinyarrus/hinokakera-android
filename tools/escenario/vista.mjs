// Vista de un escenario sin HUD ni personajes: rasterizador por software con los modelos, texturas y banderas
// de field.dat (Z, escribe Z, mezcla), sin las caras de espaldas y con la luz de los modelos con normales. Sirve para la miniatura de la
// selección de escenario y para mirar el escenario sin correr el juego.
//   modelos: [{ nmd, tipo, z, escribeZ, mezcla }] en el orden de field.dat
//   texturas: (nombre) -> { w, h, rgba } (RGBA8)
//   luz: { difusa, ambiente, dir }   cam: { pos, rumbo (grados, 90 = +Z), cabeceo, fovV }
const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];

export function vista({ w, h, modelos, texturas, luz, cam }) {
  const rgb = new Float32Array(w * h * 3), zb = new Float32Array(w * h).fill(Infinity);
  const r = (cam.rumbo * Math.PI) / 180, p = (cam.cabeceo * Math.PI) / 180, f = h / 2 / Math.tan((cam.fovV * Math.PI) / 360);
  const F = [Math.cos(p) * Math.cos(r), Math.sin(p), Math.cos(p) * Math.sin(r)], R = [Math.sin(r), 0, -Math.cos(r)];
  const U = [F[1] * R[2] - F[2] * R[1], F[2] * R[0] - F[0] * R[2], F[0] * R[1] - F[1] * R[0]];
  const L = (() => { const l = Math.hypot(...luz.dir); return luz.dir.map((v) => -v / l); })();
  const CERCA = 0.05;

  const muestra = (t, u, v) => { // bilineal, repetida
    const x = (((u * t.w - 0.5) % t.w) + t.w) % t.w, y = (((v * t.h - 0.5) % t.h) + t.h) % t.h;
    const x0 = Math.floor(x), y0 = Math.floor(y), x1 = (x0 + 1) % t.w, y1 = (y0 + 1) % t.h, fx = x - x0, fy = y - y0, o = [0, 0, 0, 0];
    for (const [xx, yy, k] of [[x0, y0, (1 - fx) * (1 - fy)], [x1, y0, fx * (1 - fy)], [x0, y1, (1 - fx) * fy], [x1, y1, fx * fy]]) {
      const i = 4 * (yy * t.w + xx);
      for (let c = 0; c < 4; c++) o[c] += (t.rgba[i + c] / 255) * k;
    }
    return o;
  };

  for (const mod of modelos) {
    const { nmd } = mod, lit = mod.tipo <= 1;
    const vs = nmd.vs.map((v) => {
      // la luz del shader de los escenarios: max(difusa * (N·L + 1) / 2, ambiente), recortada a 1
      const d = sub(v.p, cam.pos), col = lit ? [0, 1, 2].map((k) => Math.min(1, Math.max(luz.difusa[k] * (dot(v.n, L) + 1) * 0.5, luz.ambiente[k]))) : [1, 1, 1];
      return { x: dot(d, R), y: dot(d, U), z: dot(d, F), u: v.uv[0], v: v.uv[1], col };
    });
    for (const mat of nmd.mats) {
      const t = mat.tex ? texturas(mat.tex) : null;
      for (let k = mat.ini; k < mat.ini + mat.n; k++) {
        let poli = [0, 1, 2].map((j) => vs[nmd.is[3 * k + j]]);
        const [a, b, c] = poli, u = [b.x - a.x, b.y - a.y, b.z - a.z], v = [c.x - a.x, c.y - a.y, c.z - a.z];
        if ((u[1] * v[2] - u[2] * v[1]) * a.x + (u[2] * v[0] - u[0] * v[2]) * a.y + (u[0] * v[1] - u[1] * v[0]) * a.z >= 0) continue; // de espaldas: el juego descarta esas caras (CULL_CCW)
        if (poli.every((q) => q.z < CERCA)) continue;
        if (poli.some((q) => q.z < CERCA)) { // recorte contra el plano cercano
          const out = [];
          for (let i = 0; i < poli.length; i++) {
            const a = poli[i], b = poli[(i + 1) % poli.length];
            if (a.z >= CERCA) out.push(a);
            if ((a.z >= CERCA) !== (b.z >= CERCA)) {
              const s = (CERCA - a.z) / (b.z - a.z), m = {};
              for (const c of ['x', 'y', 'z', 'u', 'v']) m[c] = a[c] + (b[c] - a[c]) * s;
              m.col = a.col.map((c, j) => c + (b.col[j] - c) * s);
              out.push(m);
            }
          }
          poli = out;
        }
        for (let i = 1; i + 1 < poli.length; i++) triangulo(poli[0], poli[i], poli[i + 1], t, mat.col, mod);
      }
    }
  }

  function triangulo(a, b, c, t, mcol, mod) {
    const P = [a, b, c].map((q) => ({ sx: w / 2 + (f * q.x) / q.z, sy: h / 2 - (f * q.y) / q.z, iz: 1 / q.z, q }));
    const x0 = Math.max(0, Math.floor(Math.min(...P.map((q) => q.sx)))), x1 = Math.min(w - 1, Math.ceil(Math.max(...P.map((q) => q.sx))));
    const y0 = Math.max(0, Math.floor(Math.min(...P.map((q) => q.sy)))), y1 = Math.min(h - 1, Math.ceil(Math.max(...P.map((q) => q.sy))));
    const area = (P[1].sx - P[0].sx) * (P[2].sy - P[0].sy) - (P[2].sx - P[0].sx) * (P[1].sy - P[0].sy);
    if (Math.abs(area) < 1e-9) return;
    for (let y = y0; y <= y1; y++) for (let x = x0; x <= x1; x++) {
      const px = x + 0.5, py = y + 0.5;
      const w0 = ((P[1].sx - px) * (P[2].sy - py) - (P[2].sx - px) * (P[1].sy - py)) / area;
      const w1 = ((P[2].sx - px) * (P[0].sy - py) - (P[0].sx - px) * (P[2].sy - py)) / area, w2 = 1 - w0 - w1;
      if (w0 < 0 || w1 < 0 || w2 < 0) continue;
      const iz = w0 * P[0].iz + w1 * P[1].iz + w2 * P[2].iz, z = 1 / iz, i = y * w + x;
      if (mod.z && z >= zb[i]) continue;
      const k0 = (w0 * P[0].iz) / iz, k1 = (w1 * P[1].iz) / iz, k2 = (w2 * P[2].iz) / iz;
      const u = k0 * a.u + k1 * b.u + k2 * c.u, v = k0 * a.v + k1 * b.v + k2 * c.v;
      const tx = t ? muestra(t, u, v) : [1, 1, 1, 1], al = tx[3] * mcol[3];
      const src = [0, 1, 2].map((j) => tx[j] * mcol[j] * (k0 * a.col[j] + k1 * b.col[j] + k2 * c.col[j]));
      if (mod.mezcla === 2) for (let j = 0; j < 3; j++) rgb[3 * i + j] = src[j] * al + rgb[3 * i + j] * (1 - al);
      else if (mod.mezcla === 3) for (let j = 0; j < 3; j++) rgb[3 * i + j] += src[j] * al;
      else for (let j = 0; j < 3; j++) rgb[3 * i + j] = src[j];
      if (mod.escribeZ) zb[i] = z;
    }
  }
  return rgb;
}
