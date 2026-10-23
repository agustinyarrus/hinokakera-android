// Modelos nmd_ de Hinokakera CE (formato sacado del cargador del juego, 0x404cb0 en el exe):
//   "nmd_", u8 largo + nombre, u32 materiales, u32 vértices, u32 índices, u32 (desconocido)
//   material: u8 largo + nombre, 13 floats (difuso, ambiente?, especular?, potencia), u8 largo + textura,
//             u32 primer índice, u32 cantidad de triángulos
//   vértice: pos xyz, normal xyz, u32 color, uv (36 bytes) · índices: u16
export function leerNmd(b) {
  let p = 0;
  const str = () => { const n = b[p++]; const s = b.toString('latin1', p, p + n); p += n; return s; };
  const u = () => { const v = b.readUInt32LE(p); p += 4; return v; };
  const f = () => { const v = b.readFloatLE(p); p += 4; return v; };
  const magia = b.toString('latin1', 0, 4); p = 4;
  const m = { magia, nombre: str(), nmat: u(), nv: u(), ni: u(), x: u(), mats: [], vs: [], is: [] };
  for (let i = 0; i < m.nmat; i++) {
    const mat = { nombre: str(), col: [] };
    for (let k = 0; k < 13; k++) mat.col.push(f());
    mat.tex = str(); mat.ini = u(); mat.n = u();
    m.mats.push(mat);
  }
  for (let i = 0; i < m.nv; i++) m.vs.push({ p: [f(), f(), f()], n: [f(), f(), f()], c: u(), uv: [f(), f()] });
  for (let i = 0; i < m.ni; i++) { m.is.push(b.readUInt16LE(p)); p += 2; }
  m.fin = p;
  return m;
}

// Inverso de leerNmd. mat.ini y mat.n cuentan triángulos (el ini de cada material es la suma de los anteriores).
export function escribirNmd(m) {
  const partes = [Buffer.from('nmd_', 'latin1')];
  const str = (s) => { const b = Buffer.from(s, 'latin1'); partes.push(Buffer.from([b.length]), b); };
  const num = (f, vs) => { const b = Buffer.alloc(4 * vs.length); vs.forEach((v, i) => f.call(b, v, 4 * i)); partes.push(b); };
  const u32 = (...vs) => num(Buffer.prototype.writeUInt32LE, vs.map((v) => v >>> 0));
  str(m.nombre);
  u32(m.mats.length, m.vs.length, m.is.length, m.is.length / 3);
  for (const mat of m.mats) {
    str(mat.nombre || '');
    num(Buffer.prototype.writeFloatLE, mat.col);
    str(mat.tex);
    u32(mat.ini, mat.n);
  }
  const v = Buffer.alloc(36 * m.vs.length);
  m.vs.forEach((x, i) => {
    [...x.p, ...x.n].forEach((f, k) => v.writeFloatLE(f, 36 * i + 4 * k));
    v.writeUInt32LE(x.c >>> 0, 36 * i + 24); v.writeFloatLE(x.uv[0], 36 * i + 28); v.writeFloatLE(x.uv[1], 36 * i + 32);
  });
  const ix = Buffer.alloc(2 * m.is.length);
  m.is.forEach((k, i) => ix.writeUInt16LE(k, 2 * i));
  partes.push(v, ix);
  return Buffer.concat(partes);
}
