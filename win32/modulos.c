// DLL simuladas: resuelve la tabla de importaciones del exe, GetModuleHandle/LoadLibrary y GetProcAddress.
#include "win32.h"
#include <SDL3/SDL.h>
#include <ctype.h>
#include <stdlib.h>

extern const Exportada EXP_K32_MEMORIA[], EXP_K32_HILOS[], EXP_K32_ARCHIVOS[], EXP_K32_VARIOS[];
extern const Exportada EXP_USER32[], EXP_GDI32[], EXP_WINMM[], EXP_ADVAPI32[], EXP_D3D9[], EXP_D3DX9[];
extern const Exportada EXP_DSOUND[], EXP_DINPUT8[], EXP_WS2_32[], EXP_VACIA[];

typedef struct { const char *nombre; uint32_t base; const Exportada *tablas[5]; } Dll;
static const Dll DLLS[] = {
  {"KERNEL32.DLL", 0x7c800000u, {EXP_K32_MEMORIA, EXP_K32_HILOS, EXP_K32_ARCHIVOS, EXP_K32_VARIOS}},
  {"USER32.DLL", 0x7e410000u, {EXP_USER32}},
  {"GDI32.DLL", 0x77f10000u, {EXP_GDI32}},
  {"WINMM.DLL", 0x76b40000u, {EXP_WINMM}},
  {"ADVAPI32.DLL", 0x77dd0000u, {EXP_ADVAPI32}},
  {"D3D9.DLL", 0x4fdd0000u, {EXP_D3D9}},
  {"D3DX9_37.DLL", 0x4fe00000u, {EXP_D3DX9}},
  {"DSOUND.DLL", 0x73f10000u, {EXP_DSOUND}},
  {"DINPUT8.DLL", 0x6b200000u, {EXP_DINPUT8}},
  {"WS2_32.DLL", 0x71ab0000u, {EXP_WS2_32}},
  {"COMCTL32.DLL", 0x5d090000u, {EXP_VACIA}},
  {"COMDLG32.DLL", 0x763b0000u, {EXP_VACIA}},
  {"SHELL32.DLL", 0x7c9c0000u, {EXP_VACIA}},
  {"WINSPOOL.DRV", 0x73000000u, {EXP_VACIA}},
};
#define N_DLLS (sizeof DLLS / sizeof DLLS[0])

static FuncionGen buscar(const Dll *d, const char *nombre) {
  for (int t = 0; t < 5 && d->tablas[t]; t++)
    for (const Exportada *e = d->tablas[t]; e->nombre; e++) if (!strcmp(e->nombre, nombre)) return e->fn;
  return NULL;
}

uint32_t modulo_por_nombre(const char *dll) {
  char n[64];
  size_t k = 0;
  const char *b = strrchr(dll, '\\');
  for (dll = b ? b + 1 : dll; *dll && k < sizeof n - 5; dll++) n[k++] = (char)toupper((unsigned char)*dll);
  n[k] = 0;
  if (!strchr(n, '.')) strcat(n, ".DLL");
  for (size_t i = 0; i < N_DLLS; i++) if (!strcmp(DLLS[i].nombre, n)) return DLLS[i].base;
  return 0;
}

// Una dirección por función: se recuerda para que GetProcAddress devuelva siempre la misma.
typedef struct { const Dll *d; const char *nombre; uint32_t va; } Registrada;
static Registrada *registradas;
static int n_registradas;
static SDL_Mutex *cerrojo;

static uint32_t registrar(const Dll *d, const char *nombre, int aunque_falte) {
  SDL_LockMutex(cerrojo);
  for (int i = 0; i < n_registradas; i++)
    if (registradas[i].d == d && !strcmp(registradas[i].nombre, nombre)) { SDL_UnlockMutex(cerrojo); return registradas[i].va; }
  FuncionGen fn = buscar(d, nombre);
  uint32_t va = 0;
  if (fn || aunque_falte) {
    char *completo = malloc(strlen(d->nombre) + strlen(nombre) + 2);
    sprintf(completo, "%s!%s", d->nombre, nombre);
    va = registrar_host(fn, completo);
    registradas = realloc(registradas, sizeof *registradas * (size_t)(n_registradas + 1));
    registradas[n_registradas++] = (Registrada){d, SDL_strdup(nombre), va};
  }
  SDL_UnlockMutex(cerrojo);
  return va;
}

uint32_t direccion_exportada(uint32_t modulo, const char *nombre) {
  for (size_t i = 0; i < N_DLLS; i++) if (DLLS[i].base == modulo) return registrar(&DLLS[i], nombre, 0);
  return 0;
}

void resolver_importaciones(void) {
  cerrojo = SDL_CreateMutex();
  int faltan = 0;
  for (uint32_t i = 0; i < N_IMPORTACIONES; i++) {
    const Importacion *im = &IMPORTACIONES[i];
    const Dll *d = NULL;
    for (size_t k = 0; k < N_DLLS; k++) if (!strcmp(DLLS[k].nombre, im->dll)) d = &DLLS[k];
    if (!d) { fprintf(stderr, "DLL desconocida: %s\n", im->dll); exit(1); }
    M32(im->iat) = registrar(d, im->nombre, 1);
    if (!buscar(d, im->nombre)) faltan++;
  }
  fprintf(stderr, "importaciones: %u (%d sin implementar todavía)\n", N_IMPORTACIONES, faltan);
}
