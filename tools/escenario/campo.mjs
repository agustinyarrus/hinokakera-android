// dat/field.dat (cargador en 0x4984b0 del exe): "fld_", u32 cantidad de escenarios y por cada uno:
//   u32 modelos, color de borrado (4 f32; alfa 0 = solo Z), f32 escala del desplazamiento del piso,
//   modelos {u32 largo, nombre, u32 tipo de vértice (0 xyz+normal, 1 +uv, 2 xyz, 3 xyz+uv), Z, escribe Z,
//            mezcla (0 no, 1 oscurece, 2 alfa, 3 aditiva, 4 y 5 multiplican), prueba alfa},
//   u32 efecto, u32 efecto 2 (partículas: 0 nada, 1 a 6), luz difusa (4 f32), ambiente (4 f32),
//   dirección de la luz (3 f32) y viento (3 f32, mueve la ropa y el pelo).
// El modelo 0 es el piso: su textura se corre con el centro de la pelea. Todos los modelos siguen ese centro.
export function leerCampo(b) {
  if (b.toString('latin1', 0, 4) !== 'fld_') throw new Error('no es fld_');
  let p = 8;
  const f = (n) => { const r = []; for (let i = 0; i < n; i++, p += 4) r.push(b.readFloatLE(p)); return r; };
  const u = () => { const v = b.readUInt32LE(p); p += 4; return v; };
  const escenarios = [];
  for (let r = b.readUInt32LE(4); r > 0; r--) {
    const n = u(), e = { borrado: f(4), escala: f(1)[0], modelos: [] };
    for (let i = 0; i < n; i++) {
      const l = u(), nombre = b.toString('latin1', p, p + l); p += l;
      e.modelos.push({ nombre, tipo: u(), z: u(), escribeZ: u(), mezcla: u(), pruebaAlfa: u() });
    }
    Object.assign(e, { efecto: u(), efecto2: u(), difusa: f(4), ambiente: f(4), dir: f(3), viento: f(3) });
    escenarios.push(e);
  }
  return escenarios;
}

export function escribirCampo(escenarios) {
  const partes = [Buffer.from('fld_', 'latin1')];
  const u32 = (...vs) => { const b = Buffer.alloc(4 * vs.length); vs.forEach((v, i) => b.writeUInt32LE(v >>> 0, 4 * i)); partes.push(b); };
  const f32 = (...vs) => { const b = Buffer.alloc(4 * vs.length); vs.forEach((v, i) => b.writeFloatLE(v, 4 * i)); partes.push(b); };
  u32(escenarios.length);
  for (const e of escenarios) {
    u32(e.modelos.length); f32(...e.borrado, e.escala);
    for (const m of e.modelos) {
      const nom = Buffer.from(m.nombre, 'latin1');
      u32(nom.length); partes.push(nom); u32(m.tipo, m.z, m.escribeZ, m.mezcla, m.pruebaAlfa);
    }
    u32(e.efecto, e.efecto2); f32(...e.difusa, ...e.ambiente, ...e.dir, ...e.viento);
  }
  return Buffer.concat(partes);
}
