#include "com.h"
#include <SDL3/SDL.h>
#include <stdlib.h>

#define MAX_OBJ 65536
static void *anfitrion[MAX_OBJ];
static uint32_t libres[MAX_OBJ], n_libres, siguiente = 1;
static SDL_Mutex *cerrojo;

uint32_t com_vtabla(const char *interfaz, const Metodo *metodos, int n) {
  uint32_t vt = heap_alloc(heap_proceso(), 4u * (uint32_t)n, 0);
  for (int i = 0; i < n; i++) {
    char *nombre = malloc(strlen(interfaz) + strlen(metodos[i].nombre) + 3);
    sprintf(nombre, "%s::%s", interfaz, metodos[i].nombre);
    M32(vt + 4u * (uint32_t)i) = registrar_host(metodos[i].fn, nombre);
  }
  return vt;
}

uint32_t com_objeto(uint32_t vtabla, void *host) {
  if (!cerrojo) cerrojo = SDL_CreateMutex();
  SDL_LockMutex(cerrojo);
  uint32_t id = n_libres ? libres[--n_libres] : siguiente++;
  SDL_UnlockMutex(cerrojo);
  if (id >= MAX_OBJ) { fprintf(stderr, "demasiados objetos COM\n"); exit(1); }
  anfitrion[id] = host;
  uint32_t o = heap_alloc(heap_proceso(), 16, 1);
  M32(o) = vtabla;
  M32(o + 4) = id;
  M32(o + 8) = 0x434f4d21u; // "!MOC"
  return o;
}

void *com_host(uint32_t obj) {
  if (!obj || !mem_valida(obj, 12) || M32(obj + 8) != 0x434f4d21u) return NULL;
  uint32_t id = M32(obj + 4);
  return id < MAX_OBJ ? anfitrion[id] : NULL;
}

void com_borrar(uint32_t obj) {
  uint32_t id = M32(obj + 4);
  M32(obj + 8) = 0;
  SDL_LockMutex(cerrojo);
  anfitrion[id] = NULL;
  libres[n_libres++] = id;
  SDL_UnlockMutex(cerrojo);
  heap_free(heap_proceso(), obj);
}
