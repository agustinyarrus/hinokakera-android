#include "hilo.h"
#include "memoria.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>

_Thread_local Hilo *hilo_actual;
uint32_t PEB;
static SDL_AtomicInt ultimo_id;
#define MAX_HILOS 64
static Hilo *todos_los_hilos[MAX_HILOS];
static int n_hilos;

// Candado del invitado (ver hilo.h): turnos por orden de llegada, para que un hilo que se despierta no espere a
// que el principal lo suelte por casualidad.
static SDL_Mutex *turno_m;
static SDL_Condition *turno_c;
static uint32_t turno_actual, turno_siguiente;

void invitado_tomar(void) {
  if (!turno_m) { turno_m = SDL_CreateMutex(); turno_c = SDL_CreateCondition(); } // el primero es el hilo principal
  SDL_LockMutex(turno_m);
  uint32_t mio = turno_siguiente++;
  while (mio != turno_actual) SDL_WaitCondition(turno_c, turno_m);
  SDL_UnlockMutex(turno_m);
}
void invitado_soltar(void) {
  SDL_LockMutex(turno_m);
  turno_actual++;
  SDL_BroadcastCondition(turno_c);
  SDL_UnlockMutex(turno_m);
}
void invitado_ceder(void) {
  SDL_LockMutex(turno_m);
  int esperan = turno_siguiente != turno_actual + 1;
  SDL_UnlockMutex(turno_m);
  if (esperan) { invitado_soltar(); invitado_tomar(); }
}

// Vigilante (con -DRASTRO): si el juego deja de presentar cuadros, muestra dónde está cada hilo del invitado.
uint32_t cuadros_presentados(void); // dibujo.c
static int vigilar(void *p) {
  (void)p;
  uint32_t visto = 0, quieto = 0;
  for (;;) {
    SDL_Delay(1000);
    uint32_t c = cuadros_presentados();
    if (c != visto) { visto = c; quieto = 0; continue; }
    if (++quieto != 10) continue;
    fprintf(stderr, "\n*** sin cuadros nuevos hace 10 s (cuadro %u). Hilos:\n", c);
    for (int i = 0; i < n_hilos; i++) {
      Hilo *h = todos_los_hilos[i];
      fprintf(stderr, "hilo %u: ", h->id);
      hilo_rastro(h);
    }
    fflush(stderr);
  }
  return 0;
}
void vigilante_iniciar(void) {
#ifdef RASTRO
  SDL_DetachThread(SDL_CreateThread(vigilar, "vigilante", NULL));
#endif
}

Hilo *hilo_nuevo(uint32_t tam_pila) {
  uint8_t *const mem = MEM;
  if (!PEB) {
    PEB = mem_bloque(0x1000);
    M32(PEB + 8) = 0x400000; // ImageBaseAddress
  }
  Hilo *h = calloc(1, sizeof *h);
  if (n_hilos < MAX_HILOS) todos_los_hilos[n_hilos++] = h;
  h->pila_tam = ((tam_pila ? tam_pila : 0x100000) + GRANO - 1) & ~(GRANO - 1);
  h->pila_base = mem_bloque(h->pila_tam);
  h->teb = mem_bloque(0x1000);
  h->id = 0x100 + 4 * (uint32_t)SDL_AddAtomicInt(&ultimo_id, 1);
  M32(h->teb + TEB_EXCEPCIONES) = 0xffffffffu;
  M32(h->teb + TEB_PILA_TOPE) = h->pila_base + h->pila_tam;
  M32(h->teb + TEB_PILA_LIMITE) = h->pila_base;
  M32(h->teb + TEB_SELF) = h->teb;
  M32(h->teb + TEB_PROCESO) = 0x10;
  M32(h->teb + TEB_HILO) = h->id;
  M32(h->teb + TEB_PEB) = PEB;
  Ctx *c = &h->ctx;
  c->hilo = h;
  c->fs = h->teb;
  c->esp = h->pila_base + h->pila_tam - 16;
  fninit(c);
  fldcw(c, 0x27f); // lo que Windows deja en un hilo nuevo: precisión de 53 bits
  return h;
}
