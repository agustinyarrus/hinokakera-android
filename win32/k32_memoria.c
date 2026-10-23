// KERNEL32: heaps, VirtualAlloc, Global*/Local* e IsBad*Ptr, sobre la memoria del invitado.
// Asignador: clases de tamaño con listas libres para bloques chicos (de arenas de 1 MB) y una reserva
// propia para cada bloque grande. Cabecera de 8 bytes: tamaño pedido y clase.
#include "win32.h"
#include <SDL3/SDL.h>

#define CLASES 40
#define GRANDE 0xffffu
#define MAGIA 0x48450000u // "HE" en la mitad alta de la palabra de clase
#define CABECERA 8u

static SDL_Mutex *cerrojo;
static uint32_t libres[CLASES];      // cabeza de la lista libre de cada clase (va de la cabecera)
static uint32_t arena, arena_fin;
static uint32_t heap_principal;

static uint32_t capacidad(uint32_t k) { return k < 32 ? 16 * (k + 1) : 512u << (k - 31); } // 16..512, 1K..256K
static int clase_de(uint32_t tam) {
  if (tam <= 512) return tam ? (int)((tam - 1) / 16) : 0;
  for (int k = 32; k < CLASES; k++) if (tam <= capacidad((uint32_t)k)) return k;
  return -1;
}

static void iniciar(void) {
  if (!cerrojo) cerrojo = SDL_CreateMutex();
}

uint32_t heap_alloc(uint32_t heap, uint32_t tam, int cero) {
  (void)heap;
  iniciar();
  int k = clase_de(tam);
  uint32_t b;
  if (k < 0) {
    b = mem_reservar(0, tam + CABECERA, 1);
    if (!b) return 0;
    M32(b + 4) = MAGIA | GRANDE;
  } else {
    uint32_t cap = capacidad((uint32_t)k) + CABECERA;
    SDL_LockMutex(cerrojo);
    if (libres[k]) {
      b = libres[k];
      libres[k] = M32(b);
    } else {
      if (arena + cap > arena_fin) {
        arena = mem_reservar(0, 0x100000, 1);
        if (!arena) { SDL_UnlockMutex(cerrojo); return 0; }
        arena_fin = arena + 0x100000;
      }
      b = arena;
      arena += cap;
    }
    SDL_UnlockMutex(cerrojo);
    M32(b + 4) = MAGIA | (uint32_t)k;
    if (cero) memset(MEM + b + CABECERA, 0, capacidad((uint32_t)k));
  }
  M32(b) = tam;
  return b + CABECERA;
}

static int valido(uint32_t va) { return va > CABECERA && mem_valida(va - CABECERA, CABECERA) && (M32(va - 4) & 0xffff0000u) == MAGIA; }

void heap_free(uint32_t heap, uint32_t va) {
  (void)heap;
  if (!va || !valido(va)) return;
  uint32_t b = va - CABECERA, k = M32(b + 4) & 0xffffu;
  M32(b + 4) = 0; // así un doble free no la vuelve a encadenar
  if (k == GRANDE) { mem_liberar(b); return; }
  SDL_LockMutex(cerrojo);
  M32(b) = libres[k];
  libres[k] = b;
  SDL_UnlockMutex(cerrojo);
}

uint32_t heap_tam(uint32_t va) { return va && valido(va) ? M32(va - CABECERA) : 0xffffffffu; }

static uint32_t heap_realloc(uint32_t heap, uint32_t va, uint32_t tam, int cero, int en_lugar) {
  if (!va) return heap_alloc(heap, tam, cero);
  if (!valido(va)) return 0;
  uint32_t viejo = M32(va - CABECERA), k = M32(va - 4) & 0xffffu;
  uint32_t cap = k == GRANDE ? 0 : capacidad(k);
  if (tam <= cap || (k == GRANDE && tam <= viejo)) {
    if (cero && tam > viejo) memset(MEM + va + viejo, 0, tam - viejo);
    M32(va - CABECERA) = tam;
    return va;
  }
  if (en_lugar) return 0;
  uint32_t n = heap_alloc(heap, tam, 0);
  if (!n) return 0;
  memcpy(MEM + n, MEM + va, viejo < tam ? viejo : tam);
  if (cero && tam > viejo) memset(MEM + n + viejo, 0, tam - viejo);
  heap_free(heap, va);
  return n;
}

uint32_t heap_proceso(void) { return heap_principal ? heap_principal : (heap_principal = 0x00140000u); }

uint32_t gstrdup(const char *s) {
  uint32_t n = (uint32_t)strlen(s) + 1, va = heap_alloc(heap_proceso(), n, 0);
  memcpy(MEM + va, s, n);
  return va;
}

#define HEAP_ZERO_MEMORY 0x8
#define HEAP_REALLOC_IN_PLACE_ONLY 0x10

API(HeapCreate) { static uint32_t siguiente = 0x00150000u; siguiente += 0x10000; VOLVER(3, siguiente); }
API(HeapDestroy) { VOLVER(1, 1); }
API(HeapAlloc) {
  uint32_t va = heap_alloc(ARG(0), ARG(2), ARG(1) & HEAP_ZERO_MEMORY);
  VOLVER(3, va);
}
API(HeapFree) { heap_free(ARG(0), ARG(2)); VOLVER(3, 1); }
API(HeapReAlloc) {
  uint32_t f = ARG(1);
  VOLVER(4, heap_realloc(ARG(0), ARG(2), ARG(3), f & HEAP_ZERO_MEMORY, f & HEAP_REALLOC_IN_PLACE_ONLY));
}
API(HeapSize) { VOLVER(3, heap_tam(ARG(2))); }
API(HeapValidate) { VOLVER(3, 1); }
API(GetProcessHeap) { VOLVER(0, heap_proceso()); }

#define MEM_COMMIT 0x1000
#define MEM_RESERVE 0x2000
#define MEM_DECOMMIT 0x4000
#define MEM_RELEASE 0x8000
API(VirtualAlloc) {
  uint32_t va = ARG(0), tam = ARG(1), tipo = ARG(2);
  if (tipo & MEM_RESERVE || !va) {
    uint32_t r = mem_reservar(va & ~(GRANO - 1), tam + (va & (GRANO - 1)), (tipo & MEM_COMMIT) != 0);
    if (!r) { poner_error(ERROR_NOT_ENOUGH_MEMORY); VOLVER(4, 0); }
    VOLVER(4, va ? va & ~(PAGINA - 1) : r);
  }
  if (!mem_reserva_de(va, NULL) || !mem_comprometer(va, tam)) { poner_error(ERROR_INVALID_PARAMETER); VOLVER(4, 0); }
  VOLVER(4, va & ~(PAGINA - 1));
}
API(VirtualFree) {
  uint32_t va = ARG(0), tam = ARG(1), tipo = ARG(2);
  if (tipo & MEM_RELEASE) VOLVER(3, mem_liberar(va));
  if (tipo & MEM_DECOMMIT) {
    uint32_t total;
    if (!tam) { uint32_t ini = mem_reserva_de(va, &total); tam = ini + total - va; }
    mem_descomprometer(va, tam);
    VOLVER(3, 1);
  }
  VOLVER(3, 0);
}
API(VirtualProtect) { if (ARG(3)) M32(ARG(3)) = 4; VOLVER(4, 1); }
API(VirtualQuery) {
  uint32_t va = ARG(0), info = ARG(1), total = 0, ini = mem_reserva_de(va, &total);
  memset(MEM + info, 0, 28);
  M32(info) = va & ~(PAGINA - 1);
  M32(info + 4) = ini;
  M32(info + 8) = 4;
  M32(info + 12) = ini ? ini + total - (va & ~(PAGINA - 1)) : PAGINA;
  M32(info + 16) = !ini ? 0x10000 : mem_valida(va, 1) ? MEM_COMMIT : MEM_RESERVE;
  M32(info + 20) = mem_valida(va, 1) ? 4 : 0;
  M32(info + 24) = ini ? 0x20000 : 0;
  VOLVER(3, 28);
}

// Global*/Local*: el "handle" es el mismo puntero (también para GMEM_MOVEABLE).
#define GMEM_ZEROINIT 0x40
API(GlobalAlloc) { VOLVER(2, heap_alloc(0, ARG(1) ? ARG(1) : 1, ARG(0) & GMEM_ZEROINIT)); }
API(GlobalReAlloc) { VOLVER(3, heap_realloc(0, ARG(0), ARG(1) ? ARG(1) : 1, ARG(2) & GMEM_ZEROINIT, 0)); }
API(GlobalLock) { VOLVER(1, ARG(0)); }
API(GlobalUnlock) { VOLVER(1, 0); }
API(GlobalHandle) { VOLVER(1, ARG(0)); }
API(GlobalFlags) { VOLVER(1, 0); }
API(GlobalSize) { uint32_t t = heap_tam(ARG(0)); VOLVER(1, t == 0xffffffffu ? 0 : t); }
API(GlobalFree) { heap_free(0, ARG(0)); VOLVER(1, 0); }
API(LocalAlloc) { VOLVER(2, heap_alloc(0, ARG(1) ? ARG(1) : 1, ARG(0) & GMEM_ZEROINIT)); }
API(LocalReAlloc) { VOLVER(3, heap_realloc(0, ARG(0), ARG(1) ? ARG(1) : 1, ARG(2) & GMEM_ZEROINIT, 0)); }
API(LocalFree) { heap_free(0, ARG(0)); VOLVER(1, 0); }

API(IsBadReadPtr) { VOLVER(2, !mem_valida(ARG(0), ARG(1))); }
API(IsBadWritePtr) { VOLVER(2, !mem_valida(ARG(0), ARG(1))); }
API(IsBadCodePtr) { uint32_t va = ARG(0); VOLVER(1, !(buscar_funcion(va) || nombre_host(va))); }
API(IsBadStringPtrA) {
  uint32_t va = ARG(0), max = ARG(1);
  for (uint32_t i = 0; i < max; i++) { if (!mem_valida(va + i, 1)) VOLVER(2, 1); if (!M8(va + i)) break; }
  VOLVER(2, 0);
}
API(IsBadStringPtrW) {
  uint32_t va = ARG(0), max = ARG(1);
  for (uint32_t i = 0; i < max; i++) { if (!mem_valida(va + 2 * i, 2)) VOLVER(2, 1); if (!M16(va + 2 * i)) break; }
  VOLVER(2, 0);
}

const Exportada EXP_K32_MEMORIA[] = {
  {"HeapCreate", HeapCreate}, {"HeapDestroy", HeapDestroy}, {"HeapAlloc", HeapAlloc}, {"HeapFree", HeapFree},
  {"HeapReAlloc", HeapReAlloc}, {"HeapSize", HeapSize}, {"HeapValidate", HeapValidate}, {"GetProcessHeap", GetProcessHeap},
  {"VirtualAlloc", VirtualAlloc}, {"VirtualFree", VirtualFree}, {"VirtualProtect", VirtualProtect}, {"VirtualQuery", VirtualQuery},
  {"GlobalAlloc", GlobalAlloc}, {"GlobalReAlloc", GlobalReAlloc}, {"GlobalLock", GlobalLock}, {"GlobalUnlock", GlobalUnlock},
  {"GlobalHandle", GlobalHandle}, {"GlobalFlags", GlobalFlags}, {"GlobalSize", GlobalSize}, {"GlobalFree", GlobalFree},
  {"LocalAlloc", LocalAlloc}, {"LocalReAlloc", LocalReAlloc}, {"LocalFree", LocalFree},
  {"IsBadReadPtr", IsBadReadPtr}, {"IsBadWritePtr", IsBadWritePtr}, {"IsBadCodePtr", IsBadCodePtr},
  {"IsBadStringPtrA", IsBadStringPtrA}, {"IsBadStringPtrW", IsBadStringPtrW},
  {0, 0},
};
