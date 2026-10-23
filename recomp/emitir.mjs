// Traduce una función x86 (ya desensamblada) a una función C sobre el runtime de runtime/cpu.h.
// Registros como variables locales (se vuelcan al contexto alrededor de cada llamada), banderas calculadas
// solo donde se leen (análisis de vida hacia atrás) y FPU x87 en el contexto.
import {
  Register, Mnemonic, OpKind, ConditionCode, MemorySizeExt, MemorySize, Formatter, FormatterSyntax,
  RflagsBits, FlowControl,
} from 'iced-x86';

const fmt = new Formatter(FormatterSyntax.Masm);
const OF = RflagsBits.OF, SF = RflagsBits.SF, ZF = RflagsBits.ZF, CF = RflagsBits.CF, PF = RflagsBits.PF;
const TODAS = OF | SF | ZF | CF | PF;
const M = Mnemonic;

const R = new Map();
['EAX', 'ECX', 'EDX', 'EBX', 'ESP', 'EBP', 'ESI', 'EDI'].forEach((n, i) => {
  const v = n.toLowerCase();
  R.set(Register[n], { v, tam: 32 });
  R.set(Register[n.slice(1)], { v, tam: 16 });
  if (i < 4) {
    R.set(Register[n[1] + 'L'], { v, tam: 8, alto: false });
    R.set(Register[n[1] + 'H'], { v, tam: 8, alto: true });
  }
});
const esST = r => r >= Register.ST0 && r <= Register.ST7;
const hex = n => `0x${(n >>> 0).toString(16)}u`;
const mascara = n => n === 32 ? '' : ` & ${hex(2 ** n - 1)}`;
const nombreFn = va => `f_${va.toString(16).padStart(8, '0')}`;
const etiqueta = va => `L_${va.toString(16)}`;

export class NoSoportada extends Error {}

const BANDERAS = [[CF, 'cf'], [ZF, 'zf'], [SF, 'sf'], [OF, 'of'], [PF, 'pf']];
const guardarB = m => BANDERAS.filter(([b]) => m & b).map(([, n]) => `c->${n} = ${n}; `).join('');
const cargarB = m => BANDERAS.filter(([b]) => m & b).map(([, n]) => `${n} = c->${n}; `).join('');

// Vida de banderas hacia atrás. Entre funciones casi nunca viajan, pero las rutinas en ensamblador del CRT
// (trigonometría x87) reciben banderas del que llama: info.entra[g] son las que g lee al entrar e
// info.devuelve[g] las que sus llamadores leen después de llamarla.
export function analizarBanderas(f, info) {
  const orden = [...f.instr.keys()].sort((a, b) => a - b);
  const uso = new Map(), def = new Map();
  for (const va of orden) {
    const ins = f.instr.get(va);
    let u = ins.rflagsRead & TODAS, d = (ins.rflagsWritten | ins.rflagsCleared | ins.rflagsSet) & TODAS;
    const fc = ins.flowControl;
    if (ins.isInvalid) { u = 0; d = 0; }
    else if (fc === FlowControl.Call) { u = info.entra.get(ins.nearBranch32) ?? 0; d = TODAS; }
    else if (fc === FlowControl.IndirectCall) { u = 0; d = TODAS; }
    else if (fc === FlowControl.Return) { u = info.devuelve.get(f.entrada) ?? 0; d = 0; }
    else if (fc === FlowControl.UnconditionalBranch && f.finales.has(ins.nearBranch32)) { u = info.entra.get(ins.nearBranch32) ?? 0; d = 0; }
    else if (esDesplazamiento(ins) && ins.op1Kind !== OpKind.Immediate8) d = 0; // por CL: con 0 no toca nada
    else if (esDesplazamiento(ins) && (ins.immediate8 & 31) === 0) { u = 0; d = 0; }
    else if ((ins.hasRepPrefix || ins.hasRepnePrefix) && esCadena(ins)) d = 0; // con ecx = 0 no toca nada
    uso.set(va, u); def.set(va, d);
  }
  const vivoAntes = new Map(orden.map(va => [va, 0])), vivoTras = new Map(orden.map(va => [va, 0]));
  const pred = new Map(orden.map(va => [va, []]));
  for (const va of orden) for (const s of f.suc.get(va) ?? []) pred.get(s)?.push(va);
  const cola = [...orden].reverse(), enCola = new Set(cola);
  while (cola.length) {
    const va = cola.shift(); enCola.delete(va);
    let tras = 0;
    for (const s of f.suc.get(va) ?? []) tras |= vivoAntes.get(s) ?? 0;
    const antes = (tras & ~def.get(va)) | uso.get(va);
    vivoTras.set(va, tras);
    if (antes !== vivoAntes.get(va)) {
      vivoAntes.set(va, antes);
      for (const p of pred.get(va)) if (!enCola.has(p)) { cola.push(p); enCola.add(p); }
    }
  }
  return { vivoAntes, vivoTras };
}

export function emitirFuncion(f, ctx) {
  const orden = [...f.instr.keys()].sort((a, b) => a - b);
  const sigTextual = new Map(orden.map((va, i) => [va, orden[i + 1]]));
  const { vivoAntes, vivoTras } = ctx.analisis.get(f.entrada);
  const avisos = [];
  const entra = ctx.entra.get(f.entrada) ?? 0;
  if (entra) avisos.push(`recibe banderas ${entra}`);
  for (const [va, ins] of f.instr) {
    if (ins.flowControl === FlowControl.IndirectCall && vivoTras.get(va)) avisos.push(`${va.toString(16)}: banderas vivas tras llamada indirecta`);
  }

  // ---- etiquetas ----
  const etiquetas = new Set();
  for (const va of orden) {
    const s = f.suc.get(va) ?? [];
    const ins = f.instr.get(va);
    if (f.tablas.has(va) || ins.flowControl === FlowControl.UnconditionalBranch || ins.flowControl === FlowControl.ConditionalBranch) {
      for (const d of s) etiquetas.add(d);
    }
    const sig = Number(ins.nextIP);
    if (s.includes(sig) && sigTextual.get(va) !== sig) etiquetas.add(sig);
  }

  // ---- emisión ----
  const out = [];
  out.push(`void ${nombreFn(f.entrada)}(Ctx *c) {`);
  out.push(`  ENTRADA_FUNCION(${hex(f.entrada)});`);
  if (entra) out.push(`  ${cargarB(entra)}`);
  // El código se emite por dirección: si la función incluye bloques ubicados antes de su entrada
  // (epílogos compartidos del CRT), hay que empezar saltando a la entrada.
  if (orden[0] !== f.entrada) { etiquetas.add(f.entrada); out.push(`  goto ${etiqueta(f.entrada)};`); }
  for (const va of orden) {
    const ins = f.instr.get(va);
    if (etiquetas.has(va)) out.push(`${etiqueta(va)}:;`);
    out.push(`  /* ${va.toString(16)}: ${ins.isInvalid ? '(inválida)' : fmt.format(ins).replaceAll('*/', '* /')} */`);
    // HINO_TRAZA=4cb3e2,... al traducir: esas funciones informan cada instrucción (depuración).
    if (ctx.trazar?.has(f.entrada)) out.push(`  traza(c, ${hex(va)}, esp, eax, ecx, edx);`);
    let cuerpo;
    try {
      cuerpo = instruccion(ins, va, vivoTras.get(va), f, ctx);
    } catch (e) {
      if (!(e instanceof NoSoportada)) throw e;
      ctx.noSoportadas.push(`${va.toString(16)} ${fmt.format(ins)}: ${e.message}`);
      cuerpo = [`fallar(c, ${hex(va)}, "no soportada: ${e.message.replaceAll('"', "'")}");`];
    }
    for (const l of cuerpo) out.push('  ' + l);
    // Caída al siguiente bloque cuando no es el siguiente en el texto.
    const s = f.suc.get(va) ?? [];
    const cae = s.length && s.includes(Number(ins.nextIP)) && ins.flowControl !== FlowControl.UnconditionalBranch && !f.tablas.has(va);
    if (cae && sigTextual.get(va) !== Number(ins.nextIP)) out.push(`  goto ${etiqueta(Number(ins.nextIP))};`);
    if (!s.length && !terminaSola(ins, f, va)) out.push(`  fallar(c, ${hex(va)}, "sin sucesor");`);
  }
  out.push('}');
  return { codigo: out.join('\n'), avisos };
}

// Instrucciones cuyo código C ya termina (return / goto / fallar).
function terminaSola(ins, f, va) {
  const fc = ins.flowControl;
  return fc === FlowControl.Return || fc === FlowControl.UnconditionalBranch || fc === FlowControl.IndirectBranch ||
    fc === FlowControl.Call || fc === FlowControl.IndirectCall || fc === FlowControl.Interrupt || ins.isInvalid;
}

function esDesplazamiento(ins) {
  return [M.Shl, M.Shr, M.Sar, M.Rol, M.Ror, M.Rcl, M.Rcr, M.Sal].includes(ins.mnemonic);
}
function esCadena(ins) {
  return [M.Movsb, M.Movsw, M.Movsd, M.Stosb, M.Stosw, M.Stosd, M.Scasb, M.Scasw, M.Scasd, M.Cmpsb, M.Cmpsw, M.Cmpsd, M.Lodsb, M.Lodsw, M.Lodsd].includes(ins.mnemonic) &&
    (ins.op0Kind === OpKind.MemoryESEDI || ins.op1Kind === OpKind.MemoryESEDI || ins.op0Kind === OpKind.MemorySegESI || ins.op1Kind === OpKind.MemorySegESI);
}

// ---------- operandos ----------
function direccion(ins) {
  const partes = [];
  if (ins.memoryBase !== Register.None) partes.push(R.get(ins.memoryBase).v);
  if (ins.memoryIndex !== Register.None) partes.push(ins.memoryIndexScale === 1 ? R.get(ins.memoryIndex).v : `${R.get(ins.memoryIndex).v}*${ins.memoryIndexScale}u`);
  const d = Number(BigInt.asUintN(32, ins.memoryDisplacement));
  if (d || !partes.length) partes.push(hex(d));
  let e = partes.join(' + ');
  if (ins.memorySegment === Register.FS) e = `c->fs + ${e}`;
  else if (ins.memorySegment === Register.GS) throw new NoSoportada('segmento gs');
  return `(uint32_t)(${e})`;
}

function tamOp(ins, i) {
  const k = ins.opKind(i);
  if (k === OpKind.Register) return R.get(ins.opRegister(i))?.tam ?? 0;
  if (k === OpKind.Memory) return MemorySizeExt.size(ins.memorySize) * 8;
  return 0;
}

function leer(ins, i, tam) {
  const k = ins.opKind(i);
  switch (k) {
    case OpKind.Register: {
      const r = R.get(ins.opRegister(i));
      if (!r) throw new NoSoportada(`registro ${ins.opRegister(i)}`);
      if (r.tam === 32) return r.v;
      if (r.tam === 16) return `(${r.v} & 0xffffu)`;
      return r.alto ? `((${r.v} >> 8) & 0xffu)` : `(${r.v} & 0xffu)`;
    }
    case OpKind.Memory: {
      const t = MemorySizeExt.size(ins.memorySize) * 8;
      return t === 32 ? `M32(ea)` : t === 16 ? `(uint32_t)M16(ea)` : t === 8 ? `(uint32_t)M8(ea)` : (() => { throw new NoSoportada(`memoria de ${t} bits`); })();
    }
    case OpKind.Immediate8: return hex(ins.immediate8);
    case OpKind.Immediate16: return hex(ins.immediate16);
    case OpKind.Immediate32: return hex(ins.immediate32);
    case OpKind.Immediate8to16: return hex(ins.immediate8to16 & 0xffff);
    case OpKind.Immediate8to32: return hex(ins.immediate8to32 >>> 0);
    default: throw new NoSoportada(`operando de tipo ${k}`);
  }
}

function escribir(ins, i, valor) {
  const k = ins.opKind(i);
  if (k === OpKind.Register) {
    const r = R.get(ins.opRegister(i));
    if (!r) throw new NoSoportada(`registro ${ins.opRegister(i)}`);
    if (r.tam === 32) return `${r.v} = ${valor};`;
    if (r.tam === 16) return `${r.v} = (${r.v} & 0xffff0000u) | ((${valor}) & 0xffffu);`;
    return r.alto ? `${r.v} = (${r.v} & 0xffff00ffu) | (((${valor}) & 0xffu) << 8);` : `${r.v} = (${r.v} & 0xffffff00u) | ((${valor}) & 0xffu);`;
  }
  if (k === OpKind.Memory) {
    const t = MemorySizeExt.size(ins.memorySize) * 8;
    if (t === 32) return `M32(ea) = ${valor};`;
    if (t === 16) return `M16(ea) = (uint16_t)(${valor});`;
    if (t === 8) return `M8(ea) = (uint8_t)(${valor});`;
    throw new NoSoportada(`escritura de ${t} bits`);
  }
  throw new NoSoportada(`destino de tipo ${k}`);
}

const usaMemoria = ins => { for (let i = 0; i < ins.opCount; i++) if (ins.opKind(i) === OpKind.Memory) return true; return false; };
const bloque = (ins, lineas) => usaMemoria(ins) ? [`{ uint32_t ea = ${direccion(ins)};`, ...lineas.map(l => '  ' + l), '}'] : lineas.length > 1 ? ['{', ...lineas.map(l => '  ' + l), '}'] : lineas;

// Banderas a partir de r (ya enmascarado a n bits).
function banderasRes(viv, n) {
  const l = [];
  if (viv & ZF) l.push(`zf = (r == 0);`);
  if (viv & SF) l.push(`sf = (r >> ${n - 1}) & 1;`);
  if (viv & PF) l.push(`pf = PARIDAD(r);`);
  return l;
}

function condicion(cc) {
  switch (cc) {
    case ConditionCode.o: return 'of';
    case ConditionCode.no: return '!of';
    case ConditionCode.b: return 'cf';
    case ConditionCode.ae: return '!cf';
    case ConditionCode.e: return 'zf';
    case ConditionCode.ne: return '!zf';
    case ConditionCode.be: return '(cf | zf)';
    case ConditionCode.a: return '!(cf | zf)';
    case ConditionCode.s: return 'sf';
    case ConditionCode.ns: return '!sf';
    case ConditionCode.p: return 'pf';
    case ConditionCode.np: return '!pf';
    case ConditionCode.l: return '(sf != of)';
    case ConditionCode.ge: return '(sf == of)';
    case ConditionCode.le: return '(zf | (sf != of))';
    case ConditionCode.g: return '(!zf & (sf == of))';
    default: throw new NoSoportada(`condición ${cc}`);
  }
}

// ---------- instrucciones ----------
function instruccion(ins, va, viv, f, ctx) {
  if (ins.isInvalid) return [`fallar(c, ${hex(va)}, "instrucción inválida");`];
  const sig = Number(ins.nextIP);
  const m = ins.mnemonic;
  const v = viv & ((ins.rflagsWritten | ins.rflagsCleared | ins.rflagsSet) & TODAS);

  // Aritmética y lógica binaria.
  const binaria = { [M.Add]: 1, [M.Adc]: 1, [M.Sub]: 1, [M.Sbb]: 1, [M.Cmp]: 1, [M.And]: 1, [M.Or]: 1, [M.Xor]: 1, [M.Test]: 1 };
  if (binaria[m]) {
    const n = tamOp(ins, 0) || tamOp(ins, 1);
    const a = leer(ins, 0, n), b = leer(ins, 1, n), mk = mascara(n), s = n - 1;
    const l = [`uint32_t a = ${a}, b = ${b}, r;`];
    switch (m) {
      case M.Add:
        l.push(`r = (a + b)${mk};`);
        if (v & CF) l.push(`cf = (r < a);`);
        if (v & OF) l.push(`of = (((a ^ r) & (b ^ r)) >> ${s}) & 1;`);
        break;
      case M.Adc:
        l.push(`r = (a + b + cf)${mk};`);
        if (v & OF) l.push(`of = (((a ^ r) & (b ^ r)) >> ${s}) & 1;`);
        if (v & CF) l.push(`cf = cf ? (r <= a) : (r < a);`);
        break;
      case M.Sub: case M.Cmp:
        l.push(`r = (a - b)${mk};`);
        if (v & CF) l.push(`cf = (a < b);`);
        if (v & OF) l.push(`of = (((a ^ b) & (a ^ r)) >> ${s}) & 1;`);
        break;
      case M.Sbb:
        l.push(`r = (a - b - cf)${mk};`);
        if (v & OF) l.push(`of = (((a ^ b) & (a ^ r)) >> ${s}) & 1;`);
        if (v & CF) l.push(`cf = cf ? (a <= b) : (a < b);`);
        break;
      case M.And: case M.Test: l.push(`r = a & b;`); break;
      case M.Or: l.push(`r = a | b;`); break;
      case M.Xor: l.push(`r = a ^ b;`); break;
    }
    if ([M.And, M.Test, M.Or, M.Xor].includes(m)) { if (v & CF) l.push('cf = 0;'); if (v & OF) l.push('of = 0;'); }
    l.push(...banderasRes(v, n));
    if (m !== M.Cmp && m !== M.Test) l.push(escribir(ins, 0, 'r'));
    // "xor r,r" y similares: el compilador simplifica.
    return bloque(ins, l);
  }

  switch (m) {
    case M.Nop: case M.Wait: return [];
    case M.Mov: {
      if (ins.op0Kind === OpKind.Register && !R.get(ins.op0Register) || ins.op1Kind === OpKind.Register && !R.get(ins.op1Register)) throw new NoSoportada('mov de segmento');
      const n = tamOp(ins, 0) || tamOp(ins, 1);
      return bloque(ins, [escribir(ins, 0, leer(ins, 1, n))]);
    }
    case M.Movzx: return bloque(ins, [escribir(ins, 0, leer(ins, 1))]);
    case M.Movsx: {
      const n = tamOp(ins, 1);
      const t = n === 8 ? 'int8_t' : 'int16_t';
      return bloque(ins, [escribir(ins, 0, `(uint32_t)(int32_t)(${t})${leer(ins, 1)}`)]);
    }
    case M.Lea: return [escribir(ins, 0, direccion({ ...propsMem(ins), memorySegment: Register.DS }))];
    case M.Xchg: {
      return bloque(ins, [`uint32_t a = ${leer(ins, 0)}, b = ${leer(ins, 1)};`, escribir(ins, 0, 'b'), escribir(ins, 1, 'a')]);
    }
    case M.Push: {
      const t = -ins.stackPointerIncrement;
      const val = leer(ins, 0);
      if (t === 4) return bloque(ins, [`uint32_t t = ${val};`, `esp -= 4; M32(esp) = t;`]);
      if (t === 2) return bloque(ins, [`uint32_t t = ${val};`, `esp -= 2; M16(esp) = (uint16_t)t;`]);
      throw new NoSoportada('push de tamaño raro');
    }
    case M.Pop: {
      const t = ins.stackPointerIncrement;
      const lect = t === 4 ? 'M32(esp)' : t === 2 ? '(uint32_t)M16(esp)' : null;
      if (!lect) throw new NoSoportada('pop de tamaño raro');
      // La dirección del destino se calcula con esp ya incrementado.
      if (ins.op0Kind === OpKind.Memory) return [`{ uint32_t t = ${lect}; esp += ${t}; uint32_t ea = ${direccion(ins)}; ${escribir(ins, 0, 't')} }`];
      return [`{ uint32_t t = ${lect}; esp += ${t}; ${escribir(ins, 0, 't')} }`];
    }
    case M.Leave: return ['esp = ebp; ebp = M32(esp); esp += 4;'];
    case M.Cdq: return ['edx = (uint32_t)((int32_t)eax >> 31);'];
    case M.Cwde: return ['eax = (uint32_t)(int32_t)(int16_t)eax;'];
    case M.Cwd: return ['edx = (edx & 0xffff0000u) | (((int16_t)eax < 0) ? 0xffffu : 0);'];
    case M.Cbw: return ['eax = (eax & 0xffff0000u) | ((uint32_t)(int16_t)(int8_t)eax & 0xffffu);'];
    case M.Cld: return ['c->df = 0;'];
    case M.Std: return ['c->df = 1;'];
    case M.Clc: return ['cf = 0;'];
    case M.Stc: return ['cf = 1;'];
    case M.Cmc: return ['cf = !cf;'];
    case M.Sahf: return ['sf = (eax >> 15) & 1; zf = (eax >> 14) & 1; pf = (eax >> 10) & 1; cf = (eax >> 8) & 1;'];
    case M.Lahf: return ['eax = (eax & 0xffff00ffu) | ((sf << 15) | (zf << 14) | (pf << 10) | 0x200u | (cf << 8));'];
    case M.Seta: case M.Setae: case M.Setb: case M.Setbe: case M.Sete: case M.Setne: case M.Setg: case M.Setge:
    case M.Setl: case M.Setle: case M.Seto: case M.Setno: case M.Setp: case M.Setnp: case M.Sets: case M.Setns:
      return bloque(ins, [escribir(ins, 0, `(uint32_t)(${condicion(ins.conditionCode)})`)]);
    case M.Cmova: case M.Cmovae: case M.Cmovb: case M.Cmovbe: case M.Cmove: case M.Cmovne: case M.Cmovg: case M.Cmovge:
    case M.Cmovl: case M.Cmovle: case M.Cmovs: case M.Cmovns:
      return bloque(ins, [`if (${condicion(ins.conditionCode)}) ${escribir(ins, 0, leer(ins, 1))}`]);

    case M.Inc: case M.Dec: case M.Neg: case M.Not: {
      const n = tamOp(ins, 0), mk = mascara(n), sb = hex(2 ** (n - 1));
      const l = [`uint32_t a = ${leer(ins, 0)}, r;`];
      if (m === M.Not) { l.push(`r = (~a)${mk};`); l.push(escribir(ins, 0, 'r')); return bloque(ins, l); }
      if (m === M.Inc) { l.push(`r = (a + 1)${mk};`); if (v & OF) l.push(`of = (r == ${sb});`); }
      if (m === M.Dec) { l.push(`r = (a - 1)${mk};`); if (v & OF) l.push(`of = (a == ${sb});`); }
      if (m === M.Neg) { l.push(`r = (0u - a)${mk};`); if (v & CF) l.push('cf = (a != 0);'); if (v & OF) l.push(`of = (a == ${sb});`); }
      l.push(...banderasRes(v, n));
      l.push(escribir(ins, 0, 'r'));
      return bloque(ins, l);
    }

    case M.Shl: case M.Sal: case M.Shr: case M.Sar: case M.Rol: case M.Ror: case M.Rcr: case M.Rcl: {
      const n = tamOp(ins, 0), mk = mascara(n), s = n - 1;
      const k = ins.op1Kind === OpKind.Immediate8 ? `${ins.immediate8 & 31}u` : ins.op1Kind === OpKind.Register ? '(ecx & 31u)' : null;
      if (!k) throw new NoSoportada('cuenta de desplazamiento');
      if (k === '0u') return [];
      const l = [`uint32_t a = ${leer(ins, 0)}, k = ${k}, r;`, 'if (k) {'];
      const sx = n === 32 ? '(int32_t)a' : n === 16 ? '(int32_t)(int16_t)a' : '(int32_t)(int8_t)a';
      switch (m) {
        case M.Shl: case M.Sal:
          l.push(`  r = (uint32_t)((uint64_t)a << k)${mk};`);
          if (v & (CF | OF)) l.push(`  cf = (uint32_t)(((uint64_t)a << k) >> ${n}) & 1;`);
          if (v & OF) l.push(`  of = ((r >> ${s}) ^ cf) & 1;`);
          break;
        case M.Shr:
          l.push(`  r = a >> k;`);
          if (v & CF) l.push(`  cf = (a >> (k - 1)) & 1;`);
          if (v & OF) l.push(`  of = (a >> ${s}) & 1;`);
          break;
        case M.Sar:
          l.push(`  r = (uint32_t)(${sx} >> (k > ${s} ? ${s} : k))${mk};`);
          if (v & CF) l.push(`  cf = (uint32_t)(${sx} >> (k - 1 > ${s} ? ${s} : k - 1)) & 1;`);
          if (v & OF) l.push('  of = 0;');
          break;
        case M.Rol:
          l.push(`  { uint32_t kk = k % ${n}; r = kk ? ((a << kk) | (a >> (${n} - kk)))${mk} : a; }`);
          if (v & (CF | OF)) l.push('  cf = r & 1;');
          if (v & OF) l.push(`  of = ((r >> ${s}) ^ cf) & 1;`);
          break;
        case M.Ror:
          l.push(`  { uint32_t kk = k % ${n}; r = kk ? ((a >> kk) | (a << (${n} - kk)))${mk} : a; }`);
          if (v & CF) l.push(`  cf = (r >> ${s}) & 1;`);
          if (v & OF) l.push(`  of = ((r >> ${s}) ^ (r >> ${s - 1})) & 1;`);
          break;
        case M.Rcr:
          l.push(`  { uint32_t kk = k % ${n + 1}; r = a; while (kk--) { uint32_t nc = r & 1; r = (r >> 1) | (cf << ${s}); cf = nc; } }`);
          if (v & OF) l.push(`  of = ((r >> ${s}) ^ (r >> ${s - 1})) & 1;`);
          break;
        case M.Rcl:
          l.push(`  { uint32_t kk = k % ${n + 1}; r = a; while (kk--) { uint32_t nc = (r >> ${s}) & 1; r = ((r << 1)${mk}) | cf; cf = nc; } }`);
          if (v & OF) l.push(`  of = ((r >> ${s}) ^ cf) & 1;`);
          break;
      }
      if (m !== M.Rol && m !== M.Ror && m !== M.Rcr && m !== M.Rcl) l.push(...banderasRes(v, n).map(x => '  ' + x));
      l.push('  ' + escribir(ins, 0, 'r'), '}');
      return bloque(ins, l);
    }
    case M.Shld: case M.Shrd: {
      const k = ins.op2Kind === OpKind.Immediate8 ? `${ins.immediate8 & 31}u` : '(ecx & 31u)';
      const l = [`uint32_t a = ${leer(ins, 0)}, b = ${leer(ins, 1)}, k = ${k}, r;`, 'if (k) {'];
      if (m === M.Shld) {
        l.push('  r = (a << k) | (b >> (32 - k));');
        if (v & CF) l.push('  cf = (a >> (32 - k)) & 1;');
      } else {
        l.push('  r = (a >> k) | (b << (32 - k));');
        if (v & CF) l.push('  cf = (a >> (k - 1)) & 1;');
      }
      if (v & OF) l.push('  of = ((r ^ a) >> 31) & 1;');
      l.push(...banderasRes(v, 32).map(x => '  ' + x), '  ' + escribir(ins, 0, 'r'), '}');
      return bloque(ins, l);
    }
    case M.Bt: case M.Bts: case M.Btr: case M.Btc: {
      if (ins.op0Kind === OpKind.Memory && ins.op1Kind === OpKind.Register) {
        // Cadena de bits: el índice puede salirse del dword.
        const l = [`uint32_t k = ${leer(ins, 1)}; uint32_t ea2 = ea + (uint32_t)(((int32_t)k >> 5) * 4); uint32_t a = M32(ea2), bit = 1u << (k & 31);`, 'cf = (a & bit) != 0;'];
        if (m === M.Bts) l.push('M32(ea2) = a | bit;');
        if (m === M.Btr) l.push('M32(ea2) = a & ~bit;');
        if (m === M.Btc) l.push('M32(ea2) = a ^ bit;');
        return bloque(ins, l);
      }
      const n = tamOp(ins, 0);
      const l = [`uint32_t a = ${leer(ins, 0)}, bit = 1u << (${leer(ins, 1)} % ${n}u);`, 'cf = (a & bit) != 0;'];
      if (m === M.Bts) l.push(escribir(ins, 0, 'a | bit'));
      if (m === M.Btr) l.push(escribir(ins, 0, 'a & ~bit'));
      if (m === M.Btc) l.push(escribir(ins, 0, 'a ^ bit'));
      return bloque(ins, l);
    }

    case M.Imul: {
      if (ins.opCount === 1) {
        const n = tamOp(ins, 0);
        if (n === 32) return bloque(ins, [`int64_t p = (int64_t)(int32_t)eax * (int32_t)${leer(ins, 0)};`, 'eax = (uint32_t)p; edx = (uint32_t)((uint64_t)p >> 32);', (v & (CF | OF)) ? 'cf = of = (p != (int32_t)p);' : '']);
        if (n === 8) return bloque(ins, [`int32_t p = (int8_t)eax * (int8_t)${leer(ins, 0)};`, 'eax = (eax & 0xffff0000u) | ((uint32_t)p & 0xffffu);', (v & (CF | OF)) ? 'cf = of = (p != (int8_t)p);' : '']);
        if (n === 16) return bloque(ins, [`int32_t p = (int16_t)eax * (int16_t)${leer(ins, 0)};`, 'eax = (eax & 0xffff0000u) | ((uint32_t)p & 0xffffu); edx = (edx & 0xffff0000u) | (((uint32_t)p >> 16) & 0xffffu);', (v & (CF | OF)) ? 'cf = of = (p != (int16_t)p);' : '']);
      }
      const n = tamOp(ins, 0);
      const a = ins.opCount === 3 ? leer(ins, 1) : leer(ins, 0);
      const b = ins.opCount === 3 ? leer(ins, 2) : leer(ins, 1);
      const t = n === 32 ? 'int32_t' : 'int16_t';
      const l = [`int64_t p = (int64_t)(${t})${a} * (${t})${b}; uint32_t r = (uint32_t)p${mascara(n)};`];
      if (v & (CF | OF)) l.push(`cf = of = (p != (${t})p);`);
      l.push(...banderasRes(v, n), escribir(ins, 0, 'r'));
      return bloque(ins, l);
    }
    case M.Mul: {
      const n = tamOp(ins, 0);
      if (n === 32) return bloque(ins, [`uint64_t p = (uint64_t)eax * ${leer(ins, 0)};`, 'eax = (uint32_t)p; edx = (uint32_t)(p >> 32);', (v & (CF | OF)) ? 'cf = of = (edx != 0);' : '']);
      if (n === 8) return bloque(ins, [`uint32_t p = (eax & 0xffu) * ${leer(ins, 0)};`, 'eax = (eax & 0xffff0000u) | (p & 0xffffu);', (v & (CF | OF)) ? 'cf = of = (p > 0xffu);' : '']);
      return bloque(ins, [`uint32_t p = (eax & 0xffffu) * ${leer(ins, 0)};`, 'eax = (eax & 0xffff0000u) | (p & 0xffffu); edx = (edx & 0xffff0000u) | (p >> 16);', (v & (CF | OF)) ? 'cf = of = (p > 0xffffu);' : '']);
    }
    case M.Div: case M.Idiv: {
      const n = tamOp(ins, 0), d = leer(ins, 0);
      const err = `fallar(c, ${hex(va)}, "división por cero o desborde");`;
      if (n === 32 && m === M.Div) return bloque(ins, [`uint32_t d = ${d}; uint64_t x = ((uint64_t)edx << 32) | eax;`, `if (!d || x / d > 0xffffffffu) ${err}`, 'eax = (uint32_t)(x / d); edx = (uint32_t)(x % d);']);
      if (n === 32) return bloque(ins, [`int32_t d = (int32_t)${d}; int64_t x = (int64_t)(((uint64_t)edx << 32) | eax);`, `if (!d || (d == -1 && x == INT64_MIN)) ${err}`, `{ int64_t q = x / d; if (q > INT32_MAX || q < INT32_MIN) ${err} eax = (uint32_t)q; edx = (uint32_t)(x % d); }`]);
      if (n === 8 && m === M.Div) return bloque(ins, [`uint32_t d = ${d}, x = eax & 0xffffu;`, `if (!d || x / d > 0xffu) ${err}`, 'eax = (eax & 0xffff0000u) | ((x % d) << 8) | (x / d);']);
      if (n === 8) return bloque(ins, [`int32_t d = (int8_t)${d}, x = (int16_t)eax;`, `if (!d) ${err}`, `{ int32_t q = x / d, rr = x % d; if (q > 127 || q < -128) ${err} eax = (eax & 0xffff0000u) | (((uint32_t)rr & 0xffu) << 8) | ((uint32_t)q & 0xffu); }`]);
      if (n === 16 && m === M.Div) return bloque(ins, [`uint32_t d = ${d}, x = ((edx & 0xffffu) << 16) | (eax & 0xffffu);`, `if (!d || x / d > 0xffffu) ${err}`, 'eax = (eax & 0xffff0000u) | (x / d); edx = (edx & 0xffff0000u) | (x % d);']);
      return bloque(ins, [`int32_t d = (int16_t)${d}, x = (int32_t)(((edx & 0xffffu) << 16) | (eax & 0xffffu));`, `if (!d) ${err}`, `{ int32_t q = x / d, rr = x % d; if (q > 32767 || q < -32768) ${err} eax = (eax & 0xffff0000u) | ((uint32_t)q & 0xffffu); edx = (edx & 0xffff0000u) | ((uint32_t)rr & 0xffffu); }`]);
    }

    // ---- cadenas ----
    case M.Movsb: case M.Movsw: case M.Movsd: case M.Stosb: case M.Stosw: case M.Stosd: case M.Lodsb: case M.Lodsw: case M.Lodsd:
    case M.Scasb: case M.Scasw: case M.Scasd: case M.Cmpsb: case M.Cmpsw: case M.Cmpsd:
      if (!esCadena(ins)) throw new NoSoportada('SSE');
      return cadena(ins, v);

    // ---- control ----
    case M.Jmp: {
      if (ins.op0Kind === OpKind.NearBranch32) {
        const d = ins.nearBranch32;
        if (f.finales.has(d)) return [`${guardarB(ctx.entra.get(d) ?? 0)}SALIDA; ${ctx.llamarDirecto(d)} return;`];
        return [`goto ${etiqueta(d)};`];
      }
      const t = f.tablas.get(va);
      if (t) {
        const casos = [...new Set(t.destinos)].map(d => `case ${hex(d)}: goto ${etiqueta(d)};`);
        return bloque(ins, [`switch (${leer(ins, 0)}) {`, ...casos.map(x => '  ' + x), `  default: fallar(c, ${hex(va)}, "tabla de salto fuera de rango");`, '}']);
      }
      return bloque(ins, [`uint32_t t = ${leer(ins, 0)};`, 'SALIDA; despachar(c, t); return;']);
    }
    case M.Ja: case M.Jae: case M.Jb: case M.Jbe: case M.Je: case M.Jne: case M.Jg: case M.Jge: case M.Jl: case M.Jle:
    case M.Jo: case M.Jno: case M.Js: case M.Jns: case M.Jp: case M.Jnp:
      return [`if (${condicion(ins.conditionCode)}) goto ${etiqueta(ins.nearBranch32)};`];
    case M.Jecxz: return [`if (!ecx) goto ${etiqueta(ins.nearBranch32)};`];
    case M.Loop: return [`if (--ecx) goto ${etiqueta(ins.nearBranch32)};`];
    case M.Call: {
      const sinRetorno = (f.suc.get(va) ?? []).length === 0;
      const fin = sinRetorno ? ` fallar(c, ${hex(va)}, "volvió una función sin retorno");` : ' ENTRADA;';
      if (ins.op0Kind === OpKind.NearBranch32) {
        const g = ins.nearBranch32;
        const vuelta = sinRetorno ? '' : ` ${cargarB((ctx.devuelve.get(g) ?? 0) & viv)}`;
        return [`esp -= 4; M32(esp) = ${hex(sig)}; ${guardarB(ctx.entra.get(g) ?? 0)}SALIDA; ${ctx.llamarDirecto(g)}${fin}${vuelta}`];
      }
      return bloque(ins, [`uint32_t t = ${leer(ins, 0)};`, `esp -= 4; M32(esp) = ${hex(sig)}; SALIDA; despachar(c, t);${fin}`]);
    }
    case M.Ret: {
      const n = ins.opCount ? ins.immediate16 : 0;
      return [`VERIFICAR_RET(${hex(va)}); esp += ${4 + n}; ${guardarB(ctx.devuelve.get(f.entrada) ?? 0)}SALIDA; return;`];
    }
    case M.Int3: return [`fallar(c, ${hex(va)}, "int3");`];
  }

  if (esFPU(m)) return fpu(ins, va);
  throw new NoSoportada(`mnemónico ${Object.keys(M).find(k => M[k] === m)}`);
}

function propsMem(ins) {
  return { memoryBase: ins.memoryBase, memoryIndex: ins.memoryIndex, memoryIndexScale: ins.memoryIndexScale, memoryDisplacement: ins.memoryDisplacement };
}

function cadena(ins, v) {
  const m = ins.mnemonic;
  const tam = [M.Movsb, M.Stosb, M.Lodsb, M.Scasb, M.Cmpsb].includes(m) ? 1 : [M.Movsw, M.Stosw, M.Lodsw, M.Scasw, M.Cmpsw].includes(m) ? 2 : 4;
  const MM = tam === 1 ? 'M8' : tam === 2 ? 'M16' : 'M32';
  const acc = tam === 4 ? 'eax' : `(eax & ${hex(2 ** (tam * 8) - 1)})`;
  const ponAcc = tam === 4 ? 'eax = x;' : `eax = (eax & ${hex(~(2 ** (tam * 8) - 1))}) | x;`;
  const rep = ins.hasRepPrefix || ins.hasRepnePrefix;
  const d = `uint32_t d = c->df ? ${hex(-tam)} : ${tam}u;`;
  let paso, compara = false;
  switch (m) {
    case M.Movsb: case M.Movsw: case M.Movsd: paso = `${MM}(edi) = ${MM}(esi); esi += d; edi += d;`; break;
    case M.Stosb: case M.Stosw: case M.Stosd: paso = `${MM}(edi) = (${tam === 4 ? 'uint32_t' : tam === 2 ? 'uint16_t' : 'uint8_t'})eax; edi += d;`; break;
    case M.Lodsb: case M.Lodsw: case M.Lodsd: paso = `{ uint32_t x = ${MM}(esi); ${ponAcc} } esi += d;`; break;
    case M.Scasb: case M.Scasw: case M.Scasd: paso = `uint32_t a = ${acc}, b = ${MM}(edi), r = (a - b)${mascara(tam * 8)}; edi += d;`; compara = true; break;
    case M.Cmpsb: case M.Cmpsw: case M.Cmpsd: paso = `uint32_t a = ${MM}(esi), b = ${MM}(edi), r = (a - b)${mascara(tam * 8)}; esi += d; edi += d;`; compara = true; break;
  }
  const n = tam * 8;
  const fl = [];
  if (compara) {
    const vv = rep ? (v | ZF) : v;
    if (vv & ZF) fl.push('zf = (r == 0);');
    if (vv & CF) fl.push('cf = (a < b);');
    if (vv & SF) fl.push(`sf = (r >> ${n - 1}) & 1;`);
    if (vv & OF) fl.push(`of = (((a ^ b) & (a ^ r)) >> ${n - 1}) & 1;`);
    if (vv & PF) fl.push('pf = PARIDAD(r);');
  }
  if (!rep) return [`{ ${d} ${paso} ${fl.join(' ')} }`];
  const corte = !compara ? '' : ins.hasRepnePrefix ? ' if (zf) break;' : ' if (!zf) break;';
  return [`{ ${d} while (ecx) { ${paso} ${fl.join(' ')} ecx--;${corte} } }`];
}

// ---------- FPU x87 ----------
const FPU = new Set(['Fld', 'Fild', 'Fst', 'Fstp', 'Fist', 'Fistp', 'Fadd', 'Faddp', 'Fiadd', 'Fsub', 'Fsubp', 'Fisub', 'Fsubr', 'Fsubrp', 'Fisubr',
  'Fmul', 'Fmulp', 'Fimul', 'Fdiv', 'Fdivp', 'Fidiv', 'Fdivr', 'Fdivrp', 'Fidivr', 'Fcom', 'Fcomp', 'Fcompp', 'Fucom', 'Fucomp', 'Fucompp',
  'Ficom', 'Ficomp', 'Ftst', 'Fxam', 'Fchs', 'Fabs', 'Fsqrt', 'Fsin', 'Fcos', 'Fsincos', 'Fptan', 'Fpatan', 'Fyl2x', 'Fyl2xp1', 'F2xm1',
  'Fscale', 'Frndint', 'Fprem', 'Fprem1', 'Fxch', 'Fld1', 'Fldz', 'Fldpi', 'Fldl2e', 'Fldln2', 'Fldl2t', 'Fldlg2', 'Fnstsw', 'Fnstcw',
  'Fldcw', 'Fnclex', 'Fninit', 'Fnsave', 'Frstor', 'Fnstenv', 'Fldenv', 'Ffree', 'Fincstp', 'Fdecstp', 'Fxtract'].map(n => M[n]));
const esFPU = m => FPU.has(m);
const st = r => `ST(${r - Register.ST0})`;

function memFloat(ins) {
  const ms = ins.memorySize;
  if (ms === MemorySize.Float32) return 'MF32(ea)';
  if (ms === MemorySize.Float64) return 'MF64(ea)';
  if (ms === MemorySize.Float80) return 'leer_f80(ea)';
  if (ms === MemorySize.Int16) return '(double)(int16_t)M16(ea)';
  if (ms === MemorySize.Int32) return '(double)(int32_t)M32(ea)';
  if (ms === MemorySize.Int64) return '(double)(int64_t)M64(ea)';
  throw new NoSoportada(`memoria FPU ${ms}`);
}

function fpu(ins, va) {
  const m = ins.mnemonic;
  const nombre = Object.keys(M).find(k => M[k] === m);
  const memoria = ins.opCount >= 1 && ins.op0Kind === OpKind.Memory;
  const b = l => memoria ? [`{ uint32_t ea = ${direccion(ins)}; ${l} }`] : [l];
  const regs = () => [ins.op0Register, ins.opCount > 1 ? ins.op1Register : Register.ST0];

  // Aritmética: dst = dst op src. La forma "r" invierte los operandos.
  const arit = { Fadd: '+', Faddp: '+', Fiadd: '+', Fsub: '-', Fsubp: '-', Fisub: '-', Fsubr: 'r-', Fsubrp: 'r-', Fisubr: 'r-',
    Fmul: '*', Fmulp: '*', Fimul: '*', Fdiv: '/', Fdivp: '/', Fidiv: '/', Fdivr: 'r/', Fdivrp: 'r/', Fidivr: 'r/' }[nombre];
  if (arit) {
    const op = arit.at(-1), rev = arit.length === 2, pop = nombre.endsWith('p');
    let dst, src;
    if (memoria) { dst = 'ST(0)'; src = memFloat(ins); }
    else if (ins.opCount === 0) { dst = 'ST(1)'; src = 'ST(0)'; }
    else { const [d, s] = regs(); dst = st(d); src = st(s); }
    const expr = rev ? `${src} ${op} ${dst}` : `${dst} ${op} ${src}`;
    return b(`${dst} = fr(c, ${expr});${pop ? ' fpop(c);' : ''}`);
  }
  switch (nombre) {
    case 'Fld':
      if (memoria) return b(`fpush(c, ${memFloat(ins)});`);
      return [`{ double t = ${st(ins.op0Register)}; fpush(c, t); }`];
    case 'Fild': return b(`fpush(c, ${memFloat(ins)});`);
    case 'Fst': case 'Fstp': {
      const pop = nombre === 'Fstp' ? ' fpop(c);' : '';
      if (!memoria) return [`${st(ins.op0Register)} = ST(0);${pop}`];
      const ms = ins.memorySize;
      if (ms === MemorySize.Float32) return b(`MF32(ea) = (float)ST(0);${pop}`);
      if (ms === MemorySize.Float64) return b(`MF64(ea) = ST(0);${pop}`);
      if (ms === MemorySize.Float80) return b(`escribir_f80(ea, ST(0));${pop}`);
      throw new NoSoportada('fst raro');
    }
    case 'Fist': case 'Fistp': {
      const pop = nombre === 'Fistp' ? ' fpop(c);' : '';
      const ms = ins.memorySize;
      if (ms === MemorySize.Int16) return b(`M16(ea) = (uint16_t)fist16(c, ST(0));${pop}`);
      if (ms === MemorySize.Int32) return b(`M32(ea) = (uint32_t)fist32(c, ST(0));${pop}`);
      if (ms === MemorySize.Int64) return b(`M64(ea) = (uint64_t)fist64(c, ST(0));${pop}`);
      throw new NoSoportada('fist raro');
    }
    case 'Fcom': case 'Fcomp': case 'Fucom': case 'Fucomp': case 'Ficom': case 'Ficomp': {
      const pop = nombre.endsWith('p') ? ' fpop(c);' : '';
      const src = memoria ? memFloat(ins) : ins.opCount === 2 ? st(ins.op1Register) : ins.opCount === 1 ? st(ins.op0Register) : 'ST(1)';
      return b(`fcomparar(c, ST(0), ${src});${pop}`);
    }
    case 'Fcompp': case 'Fucompp': return ['fcomparar(c, ST(0), ST(1)); fpop(c); fpop(c);'];
    case 'Ftst': return ['fcomparar(c, ST(0), 0.0);'];
    case 'Fxam': return ['fxam(c);'];
    case 'Fchs': return ['ST(0) = -ST(0);'];
    case 'Fabs': return ['ST(0) = fabs(ST(0));'];
    case 'Fsqrt': return ['ST(0) = fr(c, sqrt(ST(0)));'];
    case 'Fsin': return ['ST(0) = sin(ST(0)); c->fsw &= ~0x400u;'];
    case 'Fcos': return ['ST(0) = cos(ST(0)); c->fsw &= ~0x400u;'];
    case 'Fsincos': return ['{ double x = ST(0); ST(0) = sin(x); fpush(c, cos(x)); c->fsw &= ~0x400u; }'];
    case 'Fptan': return ['ST(0) = tan(ST(0)); fpush(c, 1.0); c->fsw &= ~0x400u;'];
    case 'Fpatan': return ['ST(1) = atan2(ST(1), ST(0)); fpop(c);'];
    case 'Fyl2x': return ['ST(1) = ST(1) * log2(ST(0)); fpop(c);'];
    case 'Fyl2xp1': return ['ST(1) = ST(1) * log2(1.0 + ST(0)); fpop(c);'];
    case 'F2xm1': return ['ST(0) = exp2(ST(0)) - 1.0;'];
    case 'Fscale': return ['ST(0) = ldexp(ST(0), (int)trunc(ST(1)));'];
    case 'Frndint': return ['ST(0) = frndint(c, ST(0));'];
    case 'Fprem': return ['fprem(c, 0);'];
    case 'Fprem1': return ['fprem(c, 1);'];
    case 'Fxtract': return ['{ int e; double mm = frexp(ST(0), &e); ST(0) = (double)(e - 1); fpush(c, mm * 2.0); }'];
    case 'Fxch': {
      const [a, bb] = regs();
      const otro = a === Register.ST0 ? bb : a;
      return [`{ double t = ST(0); ST(0) = ${st(otro)}; ${st(otro)} = t; }`];
    }
    case 'Fld1': return ['fpush(c, 1.0);'];
    case 'Fldz': return ['fpush(c, 0.0);'];
    case 'Fldpi': return ['fpush(c, 3.14159265358979323846);'];
    case 'Fldl2e': return ['fpush(c, 1.44269504088896340736);'];
    case 'Fldln2': return ['fpush(c, 0.693147180559945309417);'];
    case 'Fldl2t': return ['fpush(c, 3.32192809488736234787);'];
    case 'Fldlg2': return ['fpush(c, 0.301029995663981195214);'];
    case 'Fnstsw':
      if (memoria) return b('M16(ea) = fsw(c);');
      return ['eax = (eax & 0xffff0000u) | fsw(c);'];
    case 'Fnstcw': return b('M16(ea) = c->fcw;');
    case 'Fldcw': return b(`EIP_DBG(${hex(va)}); fldcw(c, M16(ea));`);
    case 'Fnclex': return ['c->fsw &= 0x7f00u;'];
    case 'Fninit': return ['fninit(c);'];
    case 'Fnsave': return b('fnsave(c, ea);');
    case 'Frstor': return b('frstor(c, ea);');
    case 'Fnstenv': return b('fnstenv(c, ea);');
    case 'Fldenv': return b('fldenv(c, ea);');
    case 'Ffree': return [];
    case 'Fincstp': return ['c->top = (c->top + 1) & 7;'];
    case 'Fdecstp': return ['c->top = (c->top - 1) & 7;'];
  }
  throw new NoSoportada(`FPU ${nombre}`);
}

export { nombreFn };
