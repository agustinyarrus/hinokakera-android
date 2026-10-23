// Panorama del código del exe para diseñar el recompilador: mnemónicos, saltos indirectos, fs:, SSE/MMX.
// Uso: node tools/estadisticas.mjs <hinokakera.exe>
import { readFileSync } from 'node:fs';
import { Mnemonic, Register, FlowControl, OpKind, Formatter, FormatterSyntax } from 'iced-x86';
import { leerPE } from '../recomp/pe.mjs';
import { crearDesensamblador } from '../recomp/desensamblar.mjs';

const pe = leerPE(process.argv[2]);
const ds = crearDesensamblador(pe);
const { funciones } = JSON.parse(readFileSync(new URL('../analisis/ghidra-funciones.json', import.meta.url)));
const entradas = funciones.map(([a]) => parseInt(a, 16)).filter(ds.enTexto);
const nombreMnem = Object.fromEntries(Object.entries(Mnemonic).filter(([, v]) => typeof v === 'number').map(([k, v]) => [v, k]));
const fmt = new Formatter(FormatterSyntax.Masm);

const mnem = new Map(), cubiertas = new Set(), ejemplos = { fs: [], simd: [], indirJmp: [], raras: [] };
let tablas = 0, tablasSinCota = 0, llamadasIndirectas = 0, errores = 0;
const esSimd = r => (r >= Register.MM0 && r <= Register.MM7) || (r >= Register.XMM0 && r <= Register.XMM31);
for (const e of entradas) {
  const f = ds.funcion(e);
  errores += f.errores.length;
  tablas += f.tablas.length;
  tablasSinCota += f.tablas.filter(t => !t.acotada).length;
  for (const [va, ins] of f.instr) {
    if (cubiertas.has(va)) continue;
    cubiertas.add(va);
    const m = nombreMnem[ins.mnemonic];
    mnem.set(m, (mnem.get(m) ?? 0) + 1);
    const txt = () => `${va.toString(16)}: ${fmt.format(ins)}`;
    if (ins.segmentPrefix === Register.FS || ins.memorySegment === Register.FS) ejemplos.fs.length < 6 && ejemplos.fs.push(txt());
    let simd = false;
    for (let i = 0; i < ins.opCount; i++) if (ins.opKind(i) === OpKind.Register && esSimd(ins.opRegister(i))) simd = true;
    if (simd) ejemplos.simd.push(txt());
    if (ins.flowControl === FlowControl.IndirectBranch && !f.tablas.some(t => t.va === va)) ejemplos.indirJmp.push(txt());
    if (ins.flowControl === FlowControl.IndirectCall) llamadasIndirectas++;
  }
}
const texto = ds.texto;
console.log(`funciones: ${entradas.length}  instrucciones: ${cubiertas.size}  errores de decodificación: ${errores}`);
console.log(`tablas de salto: ${tablas} (sin cota: ${tablasSinCota})  llamadas indirectas: ${llamadasIndirectas}`);
console.log(`saltos indirectos que no son tabla: ${ejemplos.indirJmp.length}`, ejemplos.indirJmp.slice(0, 12));
console.log(`fs: ${ejemplos.fs.length}`, ejemplos.fs);
console.log(`SIMD (MMX/SSE): ${ejemplos.simd.length}`, ejemplos.simd.slice(0, 10));
console.log(`mnemónicos distintos: ${mnem.size}`);
console.log([...mnem].sort((a, b) => b[1] - a[1]).map(([k, v]) => `${k}:${v}`).join(' '));
console.log(`.text: ${texto.tamVirtual} bytes`);
