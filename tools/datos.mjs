// Datos del juego para Android: hardlinks (no ocupan lugar) con lo que el juego lee, en minúsculas como los
// busca k32_archivos.c. Afuera quedan los exe, el manual y los .txt de ejemplo con nombres en Shift-JIS.
import { mkdirSync, existsSync, readdirSync, linkSync, statSync, copyFileSync } from 'node:fs';
import { join } from 'node:path';

// El repo vive al lado de la colección del juego y de _scratch; HINO_BOX y HINO_JUEGO lo cambian.
const RAIZ = new URL('..', import.meta.url).pathname.replace(/^\/(\w:)/, '$1');
export const BOX = process.env.HINO_BOX || join(RAIZ, '..');
export const SCRATCH = join(BOX, '_scratch/hino-android');
export const JUEGO = process.env.HINO_JUEGO || join(BOX, 'hinokakera/04_Hinokakera_Chaotic_Eclipse_2010/Game_Chaotic_Eclipse_v101_Unpacked');

export function prepararDatos() {
  const destino = join(SCRATCH, 'datos');
  const quiere = (rel) => /^[^/]+\.dat$|^net\.txt$|^bgm\/[^/]+\.ogg$|^savedata\/[^/]+\.dat$|^replay\/[^/]+\.rep$/.test(rel);
  const recorrer = (dir, rel = '') => readdirSync(join(dir, rel), { withFileTypes: true }).flatMap((e) => {
    const r = rel ? `${rel}/${e.name}` : e.name;
    return e.isDirectory() ? recorrer(dir, r) : [r];
  });
  let n = 0;
  for (const rel of recorrer(JUEGO).filter((r) => quiere(r.toLowerCase()))) {
    const a = join(destino, rel.toLowerCase());
    mkdirSync(join(a, '..'), { recursive: true });
    if (!existsSync(a)) { try { linkSync(join(JUEGO, rel), a); } catch { copyFileSync(join(JUEGO, rel), a); } }
    n++;
  }
  console.log(`datos: ${n} archivos en ${destino}`);
  return destino;
}
