// Traductor exe -> C. Uso: node recomp/traducir.mjs <hinokakera.exe> [carpeta-salida]
// Genera: fn_NNN.c (funciones), funciones.h, tabla.c (despacho + importaciones), imagen.bin, informe.txt.
import { readFileSync, writeFileSync, mkdirSync, existsSync, readdirSync, rmSync } from 'node:fs';
import { join } from 'node:path';
import { leerPE } from './pe.mjs';
import { crearDesensamblador } from './desensamblar.mjs';
import { emitirFuncion, analizarBanderas, nombreFn } from './emitir.mjs';
import { aplicarParches } from './parches.mjs';
import { FlowControl, OpKind, Mnemonic } from 'iced-x86';

const [exe, salida = new URL('../gen', import.meta.url).pathname.replace(/^\/(\w:)/, '$1')] = process.argv.slice(2);
const POR_ARCHIVO = 250;
const hex = n => `0x${(n >>> 0).toString(16)}u`;

const pe = leerPE(exe);
const raiz = new URL('../analisis/', import.meta.url);

// Parches (analisis/parches.txt): bytes del exe que se cambian antes de desensamblar y código C que se
// inserta antes de una instrucción. La imagen parcheada es también la de imagen.bin.
const inyecciones = aplicarParches(readFileSync(new URL('parches.txt', raiz), 'utf8'), pe.imagen, pe.base);

const ds = crearDesensamblador(pe);
const { funciones } = JSON.parse(readFileSync(new URL('ghidra-funciones.json', raiz)));
const entradas = new Set([pe.entrada, ...funciones.map(([a]) => parseInt(a, 16)).filter(ds.enTexto)]);
// Entradas que aparecieron en tiempo de ejecución (despachar() las informa).
const extra = new URL('entradas-extra.txt', raiz);
if (existsSync(extra)) for (const l of readFileSync(extra, 'utf8').split(/\s+/)) if (l) entradas.add(parseInt(l, 16));

// Reemplazos nativos: dirección -> función del runtime.
const reemplazos = new Map();
for (const l of readFileSync(new URL('reemplazos.txt', raiz), 'utf8').split('\n')) {
  const m = l.match(/^([0-9a-f]+)\s+(\w+)/i);
  if (m) reemplazos.set(parseInt(m[1], 16), m[2]);
}

const NO_VUELVEN = new Set(['KERNEL32.DLL!ExitProcess', 'KERNEL32.DLL!ExitThread', 'KERNEL32.DLL!FatalAppExitA', 'KERNEL32.DLL!FatalExit']);
const noRetorna = new Set([...pe.importaciones].filter(([, n]) => NO_VUELVEN.has(n)).map(([a]) => a));
for (const l of readFileSync(new URL('sin-retorno.txt', raiz), 'utf8').split('\n')) {
  const m = l.match(/^([0-9a-f]+)\s/i);
  if (m) noRetorna.add(parseInt(m[1], 16));
}

// Punto fijo: las funciones sin retorno cortan el descenso, y eso puede cambiar qué se alcanza.
let res;
for (let vuelta = 1; ; vuelta++) {
  res = new Map();
  for (const e of entradas) res.set(e, ds.funcion(e, { entradas, noRetorna }));
  let cambio = false;
  for (const [e, f] of res) {
    if (!noRetorna.has(e) && !f.tieneRet && !f.despachoFinal && !f.errores.length && [...f.finales].every(x => noRetorna.has(x))) {
      noRetorna.add(e); cambio = true;
    }
    for (const d of [...f.llamadas, ...f.finales]) if (ds.enTexto(d) && !entradas.has(d)) { entradas.add(d); cambio = true; }
  }
  console.log(`vuelta ${vuelta}: ${entradas.size} funciones, ${[...noRetorna].filter(ds.enTexto).length} sin retorno`);
  if (!cambio) break;
}

// Código que solo se referencia como dato (WndProc, vtables, manejadores): inmediatos de 32 bits en el
// código y dwords de .rdata/.data que apuntan a .text. Se agregan como funciones para el despacho
// indirecto, pero no cambian cómo se traducen las demás (un jmp hacia ellas sigue siendo un goto).
// Es un punto fijo: lo que se agrega puede referenciar (o llamar) a más código.
const pendientes = [], rechazados = new Set();
const escanear = f => {
  for (const ins of f.instr.values()) {
    for (let i = 0; i < ins.opCount; i++) if (ins.opKind(i) === OpKind.Immediate32 && ds.enTexto(ins.immediate32 >>> 0)) pendientes.push([ins.immediate32 >>> 0, false]);
  }
  for (const d of [...f.llamadas, ...f.finales]) if (ds.enTexto(d)) pendientes.push([d, true]);
};
for (const f of res.values()) escanear(f);
for (const s of pe.secciones.filter(s => s.nombre === '.rdata' || s.nombre === '.data')) {
  for (let va = s.va; va + 4 <= s.va + Math.min(s.tamCrudo, s.tamVirtual); va += 4) {
    const d = pe.leer32(va);
    if (ds.enTexto(d)) pendientes.push([d, false]);
  }
}
let extras = 0;
while (pendientes.length) {
  const [d, llamada] = pendientes.pop();
  if (res.has(d) || rechazados.has(d)) continue;
  const f = ds.funcion(d, { entradas, noRetorna });
  const primera = f.instr.get(d);
  // Un destino de call se acepta siempre; un puntero suelto, solo si decodifica bien.
  if (!llamada && (f.errores.length || !primera || primera.mnemonic === Mnemonic.Int3)) { rechazados.add(d); continue; }
  res.set(d, f);
  extras++;
  escanear(f);
}
console.log(`funciones referenciadas solo como dato: ${extras}`);

// Banderas entre funciones (punto fijo sobre el grafo de llamadas).
const entra = new Map(), devuelve = new Map(), analisis = new Map();
for (let vuelta = 1, estable = false; !estable; vuelta++) {
  estable = true;
  for (const [e, f] of res) {
    const a = analizarBanderas(f, { entra, devuelve });
    analisis.set(e, a);
    const ent = a.vivoAntes.get(e) ?? 0;
    if (ent !== (entra.get(e) ?? 0)) { entra.set(e, ent); estable = false; }
    const pide = (g, m) => { if (m & ~(devuelve.get(g) ?? 0)) { devuelve.set(g, (devuelve.get(g) ?? 0) | m); estable = false; } };
    for (const [va, ins] of f.instr) {
      if (ins.flowControl === FlowControl.Call && res.has(ins.nearBranch32)) pide(ins.nearBranch32, a.vivoTras.get(va));
      if (ins.flowControl === FlowControl.UnconditionalBranch && f.finales.has(ins.nearBranch32)) pide(ins.nearBranch32, devuelve.get(e) ?? 0);
    }
  }
  console.log(`banderas, vuelta ${vuelta}: ${[...entra.values()].filter(Boolean).length} reciben, ${[...devuelve.values()].filter(Boolean).length} devuelven`);
}

const ctx = {
  noSoportadas: [], analisis, entra, devuelve, inyecciones,
  trazar: new Set((process.env.HINO_TRAZA ?? '').split(',').filter(Boolean).map(x => parseInt(x, 16))),
  llamarDirecto: d => res.has(d) ? `${nombreFn(d)}(c);` : `despachar(c, ${hex(d)});`,
};
if (existsSync(salida)) for (const a of readdirSync(salida)) if (/^fn_\d+\.c$/.test(a)) rmSync(join(salida, a));
mkdirSync(salida, { recursive: true });

const orden = [...res.keys()].sort((a, b) => a - b);
const avisos = [], errores = [];
let instrucciones = 0;
for (let i = 0, n = 0; i < orden.length; i += POR_ARCHIVO, n++) {
  const partes = ['#include "../runtime/cpu.h"', '#include "funciones.h"', ''];
  for (const e of orden.slice(i, i + POR_ARCHIVO)) {
    const f = res.get(e);
    instrucciones += f.instr.size;
    if (f.errores.length) errores.push(`${e.toString(16)}: ${f.errores.join(', ')}`);
    if (reemplazos.has(e)) { partes.push(`void ${reemplazos.get(e)}(Ctx *c);
void ${nombreFn(e)}(Ctx *c) { ${reemplazos.get(e)}(c); }`, ''); continue; }
    const { codigo, avisos: av } = emitirFuncion(f, ctx);
    for (const a of av) avisos.push(`${e.toString(16)}: ${a}`);
    partes.push(codigo, '');
  }
  writeFileSync(join(salida, `fn_${String(n).padStart(3, '0')}.c`), partes.join('\n'));
}

writeFileSync(join(salida, 'funciones.h'), ['#pragma once', ...orden.map(e => `void ${nombreFn(e)}(Ctx *c);`)].join('\n') + '\n');
const imps = [...pe.importaciones].sort((a, b) => a[0] - b[0]);
writeFileSync(join(salida, 'tabla.c'), [
  '#include "../runtime/cpu.h"', '#include "funciones.h"', '',
  `const uint32_t ENTRADA_PE = ${hex(pe.entrada)};`,
  `const uint32_t TAM_IMAGEN = ${hex(pe.tamImagen)};`,
  'const EntradaTabla TABLA_FUNCIONES[] = {',
  ...orden.map(e => `  {${hex(e)}, ${nombreFn(e)}},`),
  '};',
  `const uint32_t N_FUNCIONES = ${orden.length};`,
  'const Importacion IMPORTACIONES[] = {',
  ...imps.map(([a, n]) => { const [dll, fn] = n.split('!'); return `  {${hex(a)}, "${dll}", "${fn}"},`; }),
  '};',
  `const uint32_t N_IMPORTACIONES = ${imps.length};`,
].join('\n') + '\n');
writeFileSync(join(salida, 'imagen.bin'), pe.imagen);

const informe = [
  `funciones: ${orden.length}  instrucciones: ${instrucciones}  archivos: ${Math.ceil(orden.length / POR_ARCHIVO)}`,
  `sin retorno: ${[...noRetorna].filter(ds.enTexto).map(x => x.toString(16)).join(' ')}`,
  `\n# no soportadas (${ctx.noSoportadas.length})`, ...ctx.noSoportadas,
  `\n# errores de decodificación (${errores.length})`, ...errores,
  `\n# avisos (${avisos.length})`, ...avisos,
].join('\n');
writeFileSync(join(salida, 'informe.txt'), informe + '\n');
console.log(informe.split('\n').slice(0, 3).join('\n'));
console.log(`no soportadas: ${ctx.noSoportadas.length}  errores: ${errores.length}  avisos: ${avisos.length}`);
