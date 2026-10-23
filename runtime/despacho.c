// Llamadas indirectas del invitado, funciones del anfitrión con dirección de invitado (importaciones y
// métodos COM), retrollamadas al invitado y el informe de fallas.
#include "despacho.h"
#include "hilo.h"
#include "memoria.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>

#define BASE_HOST 0xf0000000u
#define MAX_HOST 0x10000u
static FuncionGen host_fn[MAX_HOST];
static const char *host_nombre[MAX_HOST];
static SDL_AtomicInt n_host;

uint32_t registrar_host(FuncionGen fn, const char *nombre) {
  uint32_t k = (uint32_t)SDL_AddAtomicInt(&n_host, 1);
  if (k >= MAX_HOST) { fprintf(stderr, "demasiadas funciones del anfitrión\n"); exit(1); }
  host_fn[k] = fn;
  host_nombre[k] = nombre;
  return BASE_HOST + 4 * k;
}

const char *nombre_host(uint32_t va) {
  uint32_t k = (va - BASE_HOST) / 4;
  return va >= BASE_HOST && k < (uint32_t)SDL_GetAtomicInt(&n_host) ? host_nombre[k] : NULL;
}

FuncionGen buscar_funcion(uint32_t t) {
  uint32_t lo = 0, hi = N_FUNCIONES;
  while (lo < hi) {
    uint32_t m = (lo + hi) / 2;
    if (TABLA_FUNCIONES[m].va < t) lo = m + 1; else hi = m;
  }
  return lo < N_FUNCIONES && TABLA_FUNCIONES[lo].va == t ? TABLA_FUNCIONES[lo].fn : NULL;
}

void despachar(Ctx *c, uint32_t t) {
  if (t >= BASE_HOST) {
    uint32_t k = (t - BASE_HOST) / 4;
    if (k < (uint32_t)SDL_GetAtomicInt(&n_host)) {
      if (host_fn[k]) { host_fn[k](c); return; }
      fprintf(stderr, "importación sin implementar: %s\n", host_nombre[k]);
      fallar(c, t, "importación sin implementar");
    }
    fallar(c, t, "dirección del anfitrión desconocida");
  }
  FuncionGen fn = buscar_funcion(t);
  if (fn) { fn(c); return; }
  // Función que el análisis estático no encontró: hay que sumarla a analisis/entradas-extra.txt.
  fprintf(stderr, "ENTRADA-EXTRA %08x\n", t);
  fallar(c, t, "salto a una dirección que no es función conocida");
}

uint32_t llamar_invitado(Ctx *c, uint32_t fn, int n, const uint32_t *args, int cdecl) {
  uint8_t *const mem = MEM;
  for (int i = n - 1; i >= 0; i--) { c->esp -= 4; M32(c->esp) = args[i]; }
  c->esp -= 4;
  M32(c->esp) = BASE_HOST - 4; // dirección de retorno que nadie usa: el ret del invitado vuelve acá
  despachar(c, fn);
  if (cdecl) c->esp += 4 * (uint32_t)n;
  return c->eax;
}

#ifdef RASTRO
Rastro rastro_entrar(Ctx *c, uint32_t va) {
  Hilo *h = c->hilo;
  if (h && h->n_rastro < (int)(sizeof h->rastro / sizeof h->rastro[0])) h->rastro[h->n_rastro] = va;
  if (h) h->n_rastro++;
  return (Rastro){c};
}
void rastro_salir(Rastro *r) { if (r->c->hilo) r->c->hilo->n_rastro--; }
void aviso_ret(Ctx *c, uint32_t va, uint32_t esperado, uint32_t visto) {
  static uint32_t vistos[256];
  static int n;
  for (int i = 0; i < n; i++) if (vistos[i] == va) return;
  if (n < 256) vistos[n++] = va;
  fprintf(stderr, "ret en %08x saca %08x pero el que llamó dejó %08x\n", va, visto, esperado);
  hilo_rastro(c->hilo);
}
#endif

void hilo_rastro(Hilo *h) {
  if (!h) return;
  int n = h->n_rastro < 1024 ? h->n_rastro : 1024;
  fprintf(stderr, "pila del invitado (%d):", h->n_rastro);
  for (int i = n - 1, k = 0; i >= 0 && k < 40; i--, k++) fprintf(stderr, " %08x", h->rastro[i]);
  fprintf(stderr, "\n");
}

_Noreturn void fallar(Ctx *c, uint32_t va, const char *motivo) {
  fprintf(stderr, "\n*** FALLA en %08x: %s\n", va, motivo);
  fprintf(stderr, "eax=%08x ecx=%08x edx=%08x ebx=%08x esp=%08x ebp=%08x esi=%08x edi=%08x\n",
          c->eax, c->ecx, c->edx, c->ebx, c->esp, c->ebp, c->esi, c->edi);
  if (mem_valida(c->esp, 64)) {
    uint8_t *const mem = MEM;
    fprintf(stderr, "pila:");
    for (int i = 0; i < 16; i++) fprintf(stderr, " %08x", M32(c->esp + 4 * i));
    fprintf(stderr, "\n");
  }
  hilo_rastro(c->hilo);
  fflush(stderr);
  exit(3);
}

void traza(Ctx *c, uint32_t va, uint32_t esp, uint32_t eax, uint32_t ecx, uint32_t edx) {
  uint8_t *const mem = MEM;
  fprintf(stderr, "  %08x esp=%08x [esp]=%08x eax=%08x ecx=%08x edx=%08x top=%u st0=%g\n", va, esp, M32(esp), eax, ecx, edx, c->top, c->st[c->top & 7]);
}
