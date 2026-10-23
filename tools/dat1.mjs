// Contenedor "dat1" de Hinokakera CE (vsobj.dat, vsdat.dat, vswav.dat, nv*.dat):
//   "dat1" u32 cantidad, y por entrada: u32 largoNombre (con NUL), nombre, u32 offset, u32 tamaño.
// Uso: node tools/dat1.mjs list <archivo.dat>
//      node tools/dat1.mjs extract <archivo.dat> <carpeta-destino>
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { dirname, join, resolve, sep } from 'node:path';

export function leerDat1(buf) {
  if (buf.toString('latin1', 0, 4) !== 'dat1') throw new Error('no es un dat1');
  const n = buf.readUInt32LE(4);
  const entradas = [];
  let p = 8;
  for (let i = 0; i < n; i++) {
    const largo = buf.readUInt32LE(p);
    const nombre = buf.toString('latin1', p + 4, p + 4 + largo).replace(/\0.*$/s, '');
    p += 4 + largo;
    const offset = buf.readUInt32LE(p), tam = buf.readUInt32LE(p + 4);
    p += 8;
    if (offset + tam > buf.length) throw new Error(`${nombre}: se sale del archivo`);
    entradas.push({ nombre, offset, tam });
  }
  return entradas;
}

if (import.meta.url === `file:///${process.argv[1].replaceAll('\\', '/')}`) {
  const [cmd, archivo, destino] = process.argv.slice(2);
  const buf = readFileSync(archivo);
  const entradas = leerDat1(buf);
  if (cmd === 'list') {
    for (const e of entradas) console.log(`${e.offset.toString(16).padStart(8, '0')} ${String(e.tam).padStart(10)} ${e.nombre}`);
  } else if (cmd === 'extract') {
    const raiz = resolve(destino);
    for (const e of entradas) {
      const ruta = resolve(raiz, e.nombre);
      if (!ruta.startsWith(raiz + sep)) throw new Error(`nombre inseguro: ${e.nombre}`);
      mkdirSync(dirname(ruta), { recursive: true });
      writeFileSync(ruta, buf.subarray(e.offset, e.offset + e.tam));
    }
    console.log(`${entradas.length} archivos en ${join(raiz)}`);
  } else {
    console.error('uso: dat1.mjs list|extract <archivo.dat> [destino]');
    process.exit(2);
  }
}
