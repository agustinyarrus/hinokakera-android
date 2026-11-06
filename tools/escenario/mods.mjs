// Escenarios nuevos: arma <scratch>/mods/vsobj2.dat (el juego lo busca antes que el resto) desde tu copia del juego.
//   node tools/escenario/mods.mjs [--png carpeta]   (--png guarda las texturas y una vista de cada escenario)
// - Outskirt Hill de noche (moonlit.mjs): reemplaza el escenario 3.
// - Sunlit Shore (playa.mjs), Lost Grave -Undead- (zombis.mjs) y Eclipse Caldera (caldera.mjs): agregan los escenarios
//   9, 10 y 11, en los lugares 7, 8 y 9 de la rueda de selección. Necesitan los parches y los ganchos de analisis/
//   (hay que retraducir el exe).
// - La letra del lobby de salas (tools/sala/letras.mjs, para d3d/sala.c).
import { leerBmt } from './bmt.mjs';
import { leerCampo, escribirCampo, escribirSuelo } from './campo.mjs';
import { sacar, salidas, texturaRGBA, escribirMods, rueda } from './comun.mjs';
import { noche } from './moonlit.mjs';
import { playa } from './playa.mjs';
import { undead } from './zombis.mjs';
import { eclipseCaldera } from './caldera.mjs';
import { archivosSala } from '../sala/letras.mjs';

const campo = leerCampo(sacar('dat/field.dat'));
const sys42 = leerBmt(sacar('obj/tex_sys42.bmt'));
noche(campo, sys42);
const [s42, s24] = rueda(sys42, leerBmt(sacar('obj/tex_sys24.bmt'))); // miniaturas y nombres con las filas nuevas
playa(campo, s42, s24);
undead(campo, s42, s24);
const suelo = eclipseCaldera(campo, s42, s24);
salidas.push({ nombre: 'dat/field.dat', datos: escribirCampo(campo) });
salidas.push({ nombre: 'dat/suelo.dat', datos: escribirSuelo([suelo]) });
texturaRGBA('tex_sys42', s42);
texturaRGBA('tex_sys24', s24);
salidas.push(...archivosSala());
escribirMods();
