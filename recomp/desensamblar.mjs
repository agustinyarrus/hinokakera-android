// Descenso recursivo por función: instrucciones alcanzables, sucesores y tablas de salto de VC6.
import { Decoder, DecoderOptions, FlowControl, OpKind, Register, Mnemonic, MemorySizeExt } from 'iced-x86';

export function crearDesensamblador(pe) {
  const texto = pe.secciones.find(s => s.nombre === '.text');
  const ini = texto.va, fin = texto.va + texto.tamVirtual;
  const bytes = pe.imagen.subarray(ini - pe.base, fin - pe.base);
  const dec = new Decoder(32, bytes, DecoderOptions.None);
  const enTexto = va => va >= ini && va < fin;
  const cache = new Map();

  function decodificarEn(va) {
    let ins = cache.get(va);
    if (!ins) {
      dec.position = va - ini;
      dec.ip = BigInt(va);
      ins = dec.decode();
      cache.set(va, ins);
    }
    return ins;
  }

  const inm = p => p.op1Kind === OpKind.Immediate8to32 ? p.immediate8to32 >>> 0 : p.op1Kind === OpKind.Immediate32 ? p.immediate32 >>> 0 : null;

  // "jmp [idx*4+tabla]". VC6 acota el índice con "cmp x,N / ja default"; en los switch de dos niveles el
  // índice sale de "mov r8,[x+tablaBytes]" y la tabla de dwords tiene max(tablaBytes)+1 entradas.
  function leerTabla(ins, previas, entrada) {
    if (ins.memoryBase !== Register.None || ins.memoryIndexScale !== 4 || ins.op0Kind !== OpKind.Memory) return null;
    const tabla = Number(BigInt.asUintN(32, ins.memoryDisplacement));
    let cota = null, n = null;
    for (let i = previas.length - 1; i >= Math.max(0, previas.length - 10); i--) {
      const p = previas[i];
      if (cota === null && p.mnemonic === Mnemonic.Cmp && inm(p) !== null) cota = inm(p) + 1;
      // memcpy/memset de VC6: "and ecx,3 / jmp [ecx*4+tabla]", con la tabla en medio de la función.
      if (cota === null && p.mnemonic === Mnemonic.And && p.op0Kind === OpKind.Register && p.op0Register === ins.memoryIndex && inm(p) !== null) cota = inm(p) + 1;
      if (n === null && (p.mnemonic === Mnemonic.Mov || p.mnemonic === Mnemonic.Movzx) && p.op1Kind === OpKind.Memory &&
          MemorySizeExt.size(p.memorySize) === 1 && p.memoryBase !== Register.None && p.memoryIndex === Register.None) {
        const tb = Number(BigInt.asUintN(32, p.memoryDisplacement));
        n = { tb };
      }
    }
    let cuantas = 1024;
    if (n && cota !== null) {
      let max = 0;
      for (let k = 0; k < cota; k++) max = Math.max(max, pe.imagen[tb(n.tb) + k]);
      cuantas = max + 1;
    } else if (cota !== null) cuantas = cota;
    const destinos = [];
    for (let k = 0; k < cuantas; k++) {
      const d = pe.leer32(tabla + k * 4);
      // Sin cota, VC6 deja la tabla justo después del código de la función: se corta al salir de ese rango.
      if (!enTexto(d) || (cota === null && (d < entrada || d >= tabla)) || (cota !== null && Math.abs(d - tabla) > 0x10000)) break;
      destinos.push(d);
    }
    return { tabla, destinos, acotada: cota !== null, dosNiveles: !!n };
  }
  const tb = va => va - pe.base;

  // opciones.entradas: direcciones de funciones conocidas (un jmp a una de ellas es llamada final).
  // opciones.noRetorna: funciones o ranuras de la IAT que no vuelven.
  function funcion(entrada, { entradas = new Set(), noRetorna = new Set() } = {}) {
    const instr = new Map(), suc = new Map(), pendientes = [entrada], tablas = new Map(), errores = [];
    const llamadas = new Set(), finales = new Set();
    let tieneRet = false, despachoFinal = false;
    while (pendientes.length) {
      let va = pendientes.pop();
      const previas = [];
      while (!instr.has(va)) {
        if (!enTexto(va)) { errores.push(`fuera de .text en ${va.toString(16)}`); break; }
        const ins = decodificarEn(va);
        instr.set(va, ins);
        if (ins.isInvalid) { errores.push(`inválida en ${va.toString(16)}`); suc.set(va, []); break; }
        previas.push(ins);
        const sig = Number(ins.nextIP), fc = ins.flowControl;
        let s = [sig];
        if (fc === FlowControl.Return) { tieneRet = true; s = []; }
        else if (fc === FlowControl.Interrupt || fc === FlowControl.Exception) s = [];
        else if (fc === FlowControl.UnconditionalBranch) {
          const d = ins.nearBranch32;
          if (entradas.has(d) && d !== entrada) { finales.add(d); s = []; }
          else s = [d];
        } else if (fc === FlowControl.ConditionalBranch) s = [ins.nearBranch32, sig];
        else if (fc === FlowControl.IndirectBranch) {
          const t = leerTabla(ins, previas, entrada);
          const absoluta = ins.op0Kind === OpKind.Memory && ins.memoryBase === Register.None && ins.memoryIndex === Register.None;
          if (t && t.destinos.length) { tablas.set(va, t); s = [...new Set(t.destinos)]; }
          else if (absoluta && noRetorna.has(Number(BigInt.asUintN(32, ins.memoryDisplacement)))) s = [];
          else { despachoFinal = true; s = []; }
        } else if (fc === FlowControl.Call || fc === FlowControl.IndirectCall) {
          if (ins.op0Kind === OpKind.NearBranch32) {
            llamadas.add(ins.nearBranch32);
            if (noRetorna.has(ins.nearBranch32)) s = [];
          } else if (ins.op0Kind === OpKind.Memory && ins.memoryBase === Register.None && ins.memoryIndex === Register.None) {
            if (noRetorna.has(Number(BigInt.asUintN(32, ins.memoryDisplacement)))) s = [];
          }
        }
        suc.set(va, s);
        for (const d of s.slice(1)) pendientes.push(d);
        if (!s.length || s[0] !== sig) { if (s.length) pendientes.push(s[0]); break; }
        va = sig;
      }
    }
    return { entrada, instr, suc, tablas, errores, llamadas, finales, tieneRet, despachoFinal };
  }

  return { funcion, decodificarEn, enTexto, texto };
}
