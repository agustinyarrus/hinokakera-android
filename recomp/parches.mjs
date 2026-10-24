// Parches al exe antes de traducir (formato en analisis/parches.txt): cambia los bytes en `imagen` (verificando los
// originales) y devuelve el código C que se inserta antes de cada instrucción (va -> código).
export function aplicarParches(texto, imagen, base) {
  const inyecciones = new Map();
  for (const l of texto.split('\n')) {
    const m = l.match(/^([0-9a-f]+)\s+(.+?)\s*$/i);
    if (!m) continue;
    const va = parseInt(m[1], 16);
    if (m[2].startsWith('{')) { inyecciones.set(va, m[2]); continue; }
    const [antes, despues] = m[2].split('=>').map(s => Buffer.from(s.replace(/\s+/g, ''), 'hex'));
    const p = va - base;
    if (!despues || antes.length !== despues.length || !imagen.subarray(p, p + antes.length).equals(antes)) throw new Error(`parche en ${m[1]}: no coinciden los bytes originales`);
    despues.copy(imagen, p);
  }
  return inyecciones;
}
