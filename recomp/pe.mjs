// Lectura mínima de un PE32 (sin relocaciones: el exe de VC6 va fijo en 0x400000).
import { readFileSync } from 'node:fs';

export function leerPE(ruta) {
  const b = readFileSync(ruta);
  const pe = b.readUInt32LE(0x3c);
  if (b.toString('latin1', pe, pe + 4) !== 'PE\0\0') throw new Error('no es PE');
  const nSec = b.readUInt16LE(pe + 6), tamOpt = b.readUInt16LE(pe + 20);
  const opt = pe + 24;
  if (b.readUInt16LE(opt) !== 0x10b) throw new Error('no es PE32');
  const base = b.readUInt32LE(opt + 28), entrada = base + b.readUInt32LE(opt + 16);
  const tamImagen = b.readUInt32LE(opt + 56);
  const dir = i => ({ rva: b.readUInt32LE(opt + 96 + i * 8), tam: b.readUInt32LE(opt + 100 + i * 8) });
  const secciones = [];
  for (let i = 0, s = opt + tamOpt; i < nSec; i++, s += 40) {
    secciones.push({
      nombre: b.toString('latin1', s, s + 8).replace(/\0+$/, ''),
      va: base + b.readUInt32LE(s + 12), tamVirtual: b.readUInt32LE(s + 8),
      tamCrudo: b.readUInt32LE(s + 16), offCrudo: b.readUInt32LE(s + 20),
      caract: b.readUInt32LE(s + 36),
    });
  }
  // Imagen plana tal como la deja el cargador de Windows (lo no inicializado queda en cero).
  const imagen = Buffer.alloc(tamImagen);
  b.copy(imagen, 0, 0, Math.min(b.length, secciones[0]?.offCrudo ?? 0x1000));
  for (const s of secciones) b.copy(imagen, s.va - base, s.offCrudo, s.offCrudo + Math.min(s.tamCrudo, s.tamVirtual || s.tamCrudo));
  const rvaStr = rva => { let f = rva; while (imagen[f]) f++; return imagen.toString('latin1', rva, f); };
  // Importaciones: cada ranura de la IAT (dirección absoluta) -> "DLL!Función" u "DLL!#ordinal".
  const importaciones = new Map();
  const di = dir(1);
  for (let d = di.rva; imagen.readUInt32LE(d + 12); d += 20) {
    const dll = rvaStr(imagen.readUInt32LE(d + 12)).toUpperCase();
    const oft = imagen.readUInt32LE(d) || imagen.readUInt32LE(d + 16);
    const ft = imagen.readUInt32LE(d + 16);
    for (let k = 0; ; k++) {
      const t = imagen.readUInt32LE(oft + k * 4);
      if (!t) break;
      const nombre = t & 0x80000000 ? `#${t & 0xffff}` : rvaStr(t + 2);
      importaciones.set(base + ft + k * 4, `${dll}!${nombre}`);
    }
  }
  const leer32 = va => imagen.readUInt32LE(va - base);
  const enSeccion = (va, nombre) => secciones.some(s => s.nombre === nombre && va >= s.va && va < s.va + Math.max(s.tamVirtual, s.tamCrudo));
  return { base, entrada, tamImagen, secciones, imagen, importaciones, leer32, enSeccion, relocs: dir(5) };
}
