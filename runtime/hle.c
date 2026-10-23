// Reemplazos nativos de rutinas del CRT (ver analisis/reemplazos.txt). Convención cdecl: el que llama saca
// los argumentos; acá solo se saca la dirección de retorno.
#include "cpu.h"
#include <string.h>

void hle_memmove(Ctx *c) {
  uint8_t *const mem = MEM;
  uint32_t dst = M32(c->esp + 4), src = M32(c->esp + 8), n = M32(c->esp + 12);
  memmove(mem + dst, mem + src, n);
  c->eax = dst;
  c->esp += 4;
}
