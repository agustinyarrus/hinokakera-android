#include "memoria.h"
#include "cpu.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

uint8_t *MEM;

#define N_GRANOS 0x10000u
#define DESDE (0x01000000u / GRANO) // las reservas libres van de 16 MB...
#define HASTA (0xef000000u / GRANO) // ...hasta debajo de las direcciones de funciones del anfitrión
static uint32_t reserva[N_GRANOS];  // 0 = libre; si no, grano de inicio + 1
static uint32_t granos_de[N_GRANOS]; // largo en granos (en el grano de inicio)
static uint8_t comprometida[(1u << 20) / 8];
static uint32_t siguiente = DESDE;
static SDL_Mutex *cerrojo;

static int host_comprometer(uint32_t va, uint32_t tam) {
#ifdef _WIN32
  return VirtualAlloc(MEM + va, tam, MEM_COMMIT, PAGE_READWRITE) != NULL;
#else
  return mprotect(MEM + va, tam, PROT_READ | PROT_WRITE) == 0;
#endif
}

static void host_descomprometer(uint32_t va, uint32_t tam) {
#ifdef _WIN32
  VirtualFree(MEM + va, tam, MEM_DECOMMIT);
#else
  madvise(MEM + va, tam, MADV_DONTNEED);
  mprotect(MEM + va, tam, PROT_NONE);
#endif
}

void mem_iniciar(void) {
#ifdef _WIN32
  MEM = VirtualAlloc(NULL, 1ull << 32, MEM_RESERVE, PAGE_NOACCESS);
#else
  MEM = mmap(NULL, 1ull << 32, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
  if (MEM == MAP_FAILED) MEM = NULL;
#endif
  if (!MEM) { fprintf(stderr, "no se pudieron reservar los 4 GB del invitado\n"); exit(1); }
  cerrojo = SDL_CreateMutex();
}

static void marcar(uint32_t va, uint32_t tam, int si) {
  for (uint32_t p = va / PAGINA, f = (uint32_t)(((uint64_t)va + tam + PAGINA - 1) / PAGINA); p < f; p++) {
    if (si) comprometida[p >> 3] |= (uint8_t)(1u << (p & 7));
    else comprometida[p >> 3] &= (uint8_t)~(1u << (p & 7));
  }
}

int mem_comprometer(uint32_t va, uint32_t tam) {
  uint32_t ini = va & ~(PAGINA - 1), fin = (uint32_t)(((uint64_t)va + tam + PAGINA - 1) & ~(uint64_t)(PAGINA - 1));
  if (!host_comprometer(ini, fin - ini)) return 0;
  SDL_LockMutex(cerrojo);
  marcar(ini, fin - ini, 1);
  SDL_UnlockMutex(cerrojo);
  return 1;
}

void mem_descomprometer(uint32_t va, uint32_t tam) {
  uint32_t ini = va & ~(PAGINA - 1), fin = (uint32_t)(((uint64_t)va + tam + PAGINA - 1) & ~(uint64_t)(PAGINA - 1));
  host_descomprometer(ini, fin - ini);
  SDL_LockMutex(cerrojo);
  marcar(ini, fin - ini, 0);
  SDL_UnlockMutex(cerrojo);
}

uint32_t mem_reservar(uint32_t va, uint32_t tam, int comprometer) {
  uint32_t n = (tam + GRANO - 1) / GRANO, g0 = 0;
  if (!n) return 0;
  SDL_LockMutex(cerrojo);
  if (va) {
    g0 = va / GRANO;
    for (uint32_t g = g0; g < g0 + n; g++) if (g >= N_GRANOS || reserva[g]) { g0 = 0; break; }
    if (!g0 && va) { SDL_UnlockMutex(cerrojo); return 0; }
  } else {
    // Primer hueco desde la última reserva (da la vuelta una vez).
    for (int vuelta = 0; vuelta < 2 && !g0; vuelta++) {
      uint32_t corrido = 0;
      for (uint32_t g = vuelta ? DESDE : siguiente; g < HASTA; g++) {
        corrido = reserva[g] ? 0 : corrido + 1;
        if (corrido == n) { g0 = g + 1 - n; break; }
      }
    }
    if (!g0) { SDL_UnlockMutex(cerrojo); return 0; }
    siguiente = g0 + n;
  }
  for (uint32_t g = g0; g < g0 + n; g++) reserva[g] = g0 + 1;
  granos_de[g0] = n;
  SDL_UnlockMutex(cerrojo);
  uint32_t base = g0 * GRANO;
  if (comprometer && !mem_comprometer(base, n * GRANO)) { mem_liberar(base); return 0; }
  return base;
}

int mem_liberar(uint32_t va) {
  uint32_t g0 = va / GRANO;
  SDL_LockMutex(cerrojo);
  if (va % GRANO || reserva[g0] != g0 + 1) { SDL_UnlockMutex(cerrojo); return 0; }
  uint32_t n = granos_de[g0];
  for (uint32_t g = g0; g < g0 + n; g++) reserva[g] = 0;
  SDL_UnlockMutex(cerrojo);
  mem_descomprometer(va, n * GRANO);
  return 1;
}

int mem_valida(uint32_t va, uint32_t tam) {
  if (!tam) return 1;
  for (uint32_t p = va / PAGINA, f = (uint32_t)(((uint64_t)va + tam - 1) / PAGINA); p <= f; p++)
    if (!(comprometida[p >> 3] & (1u << (p & 7)))) return 0;
  return 1;
}

uint32_t mem_reserva_de(uint32_t va, uint32_t *tam) {
  uint32_t r = reserva[va / GRANO];
  if (!r) return 0;
  if (tam) *tam = granos_de[r - 1] * GRANO;
  return (r - 1) * GRANO;
}

uint32_t mem_bloque(uint32_t tam) {
  uint32_t va = mem_reservar(0, tam, 1);
  if (!va) { fprintf(stderr, "sin memoria del invitado (%u bytes)\n", tam); exit(1); }
  return va;
}
