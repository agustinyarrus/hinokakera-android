// x87 sobre double: comparaciones, redondeo según la palabra de control, formato de 80 bits y FNSAVE.
#include "cpu.h"
#include <float.h>
#include <stdlib.h>

void fcomparar(Ctx *c, double a, double b) {
  uint16_t s = c->fsw & ~0x4500u; // C3, C2, C0
  if (isnan(a) || isnan(b)) s |= 0x4500u;
  else if (a < b) s |= 0x100u;
  else if (a == b) s |= 0x4000u;
  c->fsw = s;
}

void fxam(Ctx *c) {
  double v = ST(0);
  uint16_t s = c->fsw & ~0x4700u;
  if (signbit(v)) s |= 0x200u; // C1 = signo
  if (isnan(v)) s |= 0x100u;
  else if (isinf(v)) s |= 0x500u;
  else if (v == 0) s |= 0x4000u;
  else if (fabs(v) < DBL_MIN) s |= 0x4400u;
  else s |= 0x400u;
  c->fsw = s;
}

#include <stdio.h>
void fldcw(Ctx *c, uint16_t cw) {
  if ((cw & 0x3f) != 0x3f && getenv("HINO_FPU")) {
    extern void hilo_rastro(struct Hilo *h);
    uint8_t *const mem = MEM;
    fprintf(stderr, "fldcw en %08x: %04x (antes %04x) esp=%08x ebp=%08x [esp..]=%08x %08x %08x %08x\n", c->eip_dbg, cw, c->fcw, c->esp, c->ebp,
            M32(c->esp), M32(c->esp + 4), M32(c->esp + 8), M32(c->esp + 12));
    hilo_rastro(c->hilo);
  }
  c->fcw = cw;
  c->pc24 = ((cw >> 8) & 3) == 0;
}

void fninit(Ctx *c) {
  fldcw(c, 0x37f);
  c->fsw = 0;
  c->top = 0;
}

static double redondear(Ctx *c, double v) {
  switch ((c->fcw >> 10) & 3) {
    case 0: return nearbyint(v); // el anfitrión siempre redondea al par más cercano
    case 1: return floor(v);
    case 2: return ceil(v);
    default: return trunc(v);
  }
}

// Fuera de rango, el x87 guarda el "entero indefinido" (el mínimo negativo).
int16_t fist16(Ctx *c, double v) { double r = redondear(c, v); return r >= -32768.0 && r <= 32767.0 ? (int16_t)r : INT16_MIN; }
int32_t fist32(Ctx *c, double v) { double r = redondear(c, v); return r >= -2147483648.0 && r <= 2147483647.0 ? (int32_t)r : INT32_MIN; }
int64_t fist64(Ctx *c, double v) { double r = redondear(c, v); return r >= -9223372036854775808.0 && r < 9223372036854775808.0 ? (int64_t)r : INT64_MIN; }
double frndint(Ctx *c, double v) { return isfinite(v) ? redondear(c, v) : v; }

void fprem(Ctx *c, int ieee) {
  double a = ST(0), b = ST(1);
  double q = ieee ? nearbyint(a / b) : trunc(a / b);
  ST(0) = ieee ? remainder(a, b) : fmod(a, b);
  uint64_t qi = (uint64_t)(int64_t)fabs(q);
  uint16_t s = c->fsw & ~0x4700u; // C2 = 0: reducción completa
  if (qi & 1) s |= 0x200u;        // C1 = Q0
  if (qi & 2) s |= 0x4000u;       // C3 = Q1
  if (qi & 4) s |= 0x100u;        // C0 = Q2
  c->fsw = s;
}

double leer_f80(uint32_t ea) {
  uint8_t *const mem = MEM;
  uint64_t man = M64(ea);
  uint16_t se = M16(ea + 8);
  int exp = se & 0x7fff;
  double v;
  if (exp == 0 && man == 0) v = 0.0;
  else if (exp == 0x7fff) v = (man << 1) ? NAN : INFINITY;
  else v = ldexp((double)man, exp - 16383 - 63);
  return (se & 0x8000) ? -v : v;
}

void escribir_f80(uint32_t ea, double v) {
  uint8_t *const mem = MEM;
  uint64_t man;
  uint16_t se = signbit(v) ? 0x8000 : 0;
  if (v == 0) man = 0;
  else if (isinf(v)) { man = 0x8000000000000000ull; se |= 0x7fff; }
  else if (isnan(v)) { man = 0xc000000000000000ull; se = 0xffff; }
  else {
    int e;
    double m = frexp(fabs(v), &e); // [0.5, 1)
    man = (uint64_t)ldexp(m, 64);
    se |= (uint16_t)(e - 1 + 16383);
  }
  M64(ea) = man;
  M16(ea + 8) = se;
}

// Entorno de 28 bytes (modo protegido de 32 bits). Las etiquetas no se siguen: se informan todas válidas.
void fnstenv(Ctx *c, uint32_t ea) {
  uint8_t *const mem = MEM;
  memset(mem + ea, 0, 28);
  M16(ea) = c->fcw;
  M16(ea + 4) = fsw(c);
  M16(ea + 8) = 0;
  fldcw(c, c->fcw | 0x3f); // FNSTENV enmascara las excepciones
}

void fldenv(Ctx *c, uint32_t ea) {
  uint8_t *const mem = MEM;
  fldcw(c, M16(ea));
  uint16_t s = M16(ea + 4);
  c->fsw = s & ~0x3800u;
  c->top = (s >> 11) & 7;
}

void fnsave(Ctx *c, uint32_t ea) {
  uint16_t cw = c->fcw;
  fnstenv(c, ea);
  uint8_t *const mem = MEM;
  M16(ea) = cw;
  for (int i = 0; i < 8; i++) escribir_f80(ea + 28 + 10 * i, ST(i));
  fninit(c);
}

void frstor(Ctx *c, uint32_t ea) {
  fldenv(c, ea);
  for (int i = 0; i < 8; i++) ST(i) = leer_f80(ea + 28 + 10 * i);
}
