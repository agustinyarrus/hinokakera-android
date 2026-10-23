// Desensambla una función (descenso recursivo) en sintaxis MASM: node tools/dis.mjs <exe> <va-hex> [max]
import { Formatter, FormatterSyntax } from 'iced-x86';
import { leerPE } from '../recomp/pe.mjs';
import { crearDesensamblador } from '../recomp/desensamblar.mjs';
const [exe, vaHex, max = '400'] = process.argv.slice(2);
const pe = leerPE(exe), ds = crearDesensamblador(pe), fmt = new Formatter(FormatterSyntax.Masm);
const f = ds.funcion(parseInt(vaHex, 16));
const orden = [...f.instr.keys()].sort((a, b) => a - b).slice(0, +max);
for (const va of orden) {
  const ins = f.instr.get(va);
  const imp = pe.importaciones.get(Number(ins.memoryDisplacement) >>> 0);
  console.log(`${va.toString(16)}  ${fmt.format(ins)}${imp ? `   ; ${imp}` : ''}`);
}
if (f.tablas.length) console.log('tablas:', f.tablas.map(t => `${t.va.toString(16)}->${t.destinos.length}${t.acotada ? '' : '?'}`).join(' '));
if (f.errores.length) console.log('errores:', f.errores);
