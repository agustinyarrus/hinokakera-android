// KERNEL32: objetos del núcleo (handles), hilos, TLS, secciones críticas, eventos, mutex y esperas.
#include "win32.h"
#include <SDL3/SDL.h>
#include <setjmp.h>
#include <stdlib.h>

// ---- tabla de handles ----
#define MAX_OBJ 4096
static struct { TipoObjeto tipo; void *dato; } objetos[MAX_OBJ];
static SDL_Mutex *cerrojo_obj;
#define H(i) (0x1000u + 4u * (uint32_t)(i))

uint32_t objeto_nuevo(TipoObjeto tipo, void *dato) {
  SDL_LockMutex(cerrojo_obj);
  for (int i = 1; i < MAX_OBJ; i++) if (objetos[i].tipo == OBJ_LIBRE) {
    objetos[i].tipo = tipo; objetos[i].dato = dato;
    SDL_UnlockMutex(cerrojo_obj);
    return H(i);
  }
  SDL_UnlockMutex(cerrojo_obj);
  fprintf(stderr, "sin handles libres\n");
  exit(1);
}

static int indice(uint32_t h) {
  if (h < H(1) || (h - 0x1000u) % 4) return -1;
  int i = (int)((h - 0x1000u) / 4);
  return i < MAX_OBJ ? i : -1;
}

void *objeto_dato(uint32_t h, TipoObjeto tipo) {
  int i = indice(h);
  return i > 0 && objetos[i].tipo == tipo ? objetos[i].dato : NULL;
}

static TipoObjeto tipo_de(uint32_t h) { int i = indice(h); return i > 0 ? objetos[i].tipo : OBJ_LIBRE; }

void archivo_cerrar(void *dato); // k32_archivos.c
void busqueda_cerrar(void *dato);

int objeto_cerrar(uint32_t h) {
  int i = indice(h);
  if (i <= 0 || objetos[i].tipo == OBJ_LIBRE) return 0;
  // Eventos, mutex e hilos no se destruyen: otro hilo podría estar esperándolos.
  if (objetos[i].tipo == OBJ_ARCHIVO) archivo_cerrar(objetos[i].dato);
  if (objetos[i].tipo == OBJ_BUSQUEDA) busqueda_cerrar(objetos[i].dato);
  SDL_LockMutex(cerrojo_obj);
  objetos[i].tipo = OBJ_LIBRE; objetos[i].dato = NULL;
  SDL_UnlockMutex(cerrojo_obj);
  return 1;
}

void poner_error(uint32_t err) { if (hilo_actual) M32(hilo_actual->teb + TEB_ULTIMO_ERROR) = err; }
API(GetLastError) { VOLVER(0, M32(hilo_actual->teb + TEB_ULTIMO_ERROR)); }
API(SetLastError) { poner_error(ARG(0)); VOLVER(1, 0); }
API(CloseHandle) { VOLVER(1, objeto_cerrar(ARG(0))); }

// ---- eventos, mutex y semáforos (sobre un mutex y una condición de SDL) ----
typedef struct {
  SDL_Mutex *m; SDL_Condition *cond;
  int senal, manual;          // evento
  uint32_t dueno, cuenta;     // mutex (recursivo)
  int32_t valor, maximo;      // semáforo
} Sincro;

static Sincro *sincro_nuevo(void) {
  Sincro *s = calloc(1, sizeof *s);
  s->m = SDL_CreateMutex(); s->cond = SDL_CreateCondition();
  return s;
}

// Espera hasta que listo() sea cierto o venza el plazo (ms; 0xffffffff = infinito). 0 = listo, 1 = vencido.
static int esperar(Sincro *s, int (*listo)(Sincro *), uint32_t ms) {
  uint64_t fin = SDL_GetTicks() + ms;
  SDL_LockMutex(s->m);
  while (!listo(s)) {
    if (ms == 0) { SDL_UnlockMutex(s->m); return 1; }
    if (ms == 0xffffffffu) SDL_WaitCondition(s->cond, s->m);
    else {
      uint64_t ahora = SDL_GetTicks();
      if (ahora >= fin) { SDL_UnlockMutex(s->m); return 1; }
      SDL_WaitConditionTimeout(s->cond, s->m, (Sint32)(fin - ahora));
    }
  }
  return 0; // vuelve con el mutex tomado
}

static int evento_listo(Sincro *s) { return s->senal; }
static int mutex_listo(Sincro *s) { return s->cuenta == 0 || s->dueno == hilo_actual->id; }
static int semaforo_listo(Sincro *s) { return s->valor > 0; }

uint32_t evento_nuevo(int manual) { Sincro *s = sincro_nuevo(); s->manual = manual; return objeto_nuevo(OBJ_EVENTO, s); }
int socket_listo(uint32_t evento, uint32_t ms); // ws2_32.c

API(CreateEventA) {
  Sincro *s = sincro_nuevo();
  s->manual = ARG(1) != 0; s->senal = ARG(2) != 0;
  VOLVER(4, objeto_nuevo(OBJ_EVENTO, s));
}
static void senalar(uint32_t h, int valor) {
  Sincro *s = objeto_dato(h, OBJ_EVENTO);
  if (!s) return;
  SDL_LockMutex(s->m);
  s->senal = valor;
  if (valor) SDL_BroadcastCondition(s->cond);
  SDL_UnlockMutex(s->m);
}
API(SetEvent) { senalar(ARG(0), 1); VOLVER(1, 1); }
void evento_senalar(uint32_t h) { senalar(h, 1); } // para el mezclador de DirectSound
API(ResetEvent) { senalar(ARG(0), 0); VOLVER(1, 1); }
API(PulseEvent) { senalar(ARG(0), 1); senalar(ARG(0), 0); VOLVER(1, 1); }

API(CreateMutexA) {
  Sincro *s = sincro_nuevo();
  if (ARG(1)) { s->dueno = hilo_actual->id; s->cuenta = 1; }
  VOLVER(3, objeto_nuevo(OBJ_MUTEX, s));
}
API(ReleaseMutex) {
  Sincro *s = objeto_dato(ARG(0), OBJ_MUTEX);
  if (!s) VOLVER(1, 0);
  SDL_LockMutex(s->m);
  int ok = s->cuenta && s->dueno == hilo_actual->id;
  if (ok && --s->cuenta == 0) { s->dueno = 0; SDL_BroadcastCondition(s->cond); }
  SDL_UnlockMutex(s->m);
  VOLVER(1, ok);
}
API(CreateSemaphoreA) {
  Sincro *s = sincro_nuevo();
  s->valor = (int32_t)ARG(1); s->maximo = (int32_t)ARG(2);
  VOLVER(4, objeto_nuevo(OBJ_SEMAFORO, s));
}
API(ReleaseSemaphore) {
  Sincro *s = objeto_dato(ARG(0), OBJ_SEMAFORO);
  if (!s) VOLVER(3, 0);
  SDL_LockMutex(s->m);
  if (ARG(2)) M32(ARG(2)) = (uint32_t)s->valor;
  s->valor += (int32_t)ARG(1);
  SDL_BroadcastCondition(s->cond);
  SDL_UnlockMutex(s->m);
  VOLVER(3, 1);
}

// ---- hilos ----
typedef struct {
  Hilo *h;
  uint32_t inicio, param, codigo, prioridad;
  int terminado, suspendido;
  Sincro *fin;      // se señala al terminar (para WaitForSingleObject)
  jmp_buf salida;   // ExitThread vuelve acá
} DatosHilo;

static int hilo_listo(Sincro *s) { return s->senal; }

// El juego suele cerrar el handle del hilo apenas lo crea: los datos se buscan por hilo, no por handle.
static _Thread_local DatosHilo *datos_hilo;

static int correr_hilo(void *p) {
  DatosHilo *d = p;
  hilo_actual = d->h;
  datos_hilo = d;
  SDL_LockMutex(d->fin->m);
  while (d->suspendido) SDL_WaitCondition(d->fin->cond, d->fin->m);
  SDL_UnlockMutex(d->fin->m);
  invitado_tomar();
  if (!setjmp(d->salida)) {
    uint32_t args[1] = {d->param};
    d->codigo = llamar_invitado(&d->h->ctx, d->inicio, 1, args, 0);
  }
  invitado_soltar();
  SDL_LockMutex(d->fin->m);
  d->terminado = 1; d->fin->senal = 1;
  SDL_BroadcastCondition(d->fin->cond);
  SDL_UnlockMutex(d->fin->m);
  return 0;
}

static DatosHilo *datos_principal;

API(CreateThread) {
  DatosHilo *d = calloc(1, sizeof *d);
  d->h = hilo_nuevo(ARG(1));
  d->inicio = ARG(2); d->param = ARG(3);
  d->suspendido = (ARG(4) & 4) != 0; // CREATE_SUSPENDED
  d->fin = sincro_nuevo(); d->fin->manual = 1;
  uint32_t h = objeto_nuevo(OBJ_HILO, d);
  d->h->manejador = h;
  if (ARG(5)) M32(ARG(5)) = d->h->id;
  SDL_PropertiesID pr = SDL_CreateProperties();
  SDL_SetPointerProperty(pr, SDL_PROP_THREAD_CREATE_ENTRY_FUNCTION_POINTER, (void *)correr_hilo);
  SDL_SetStringProperty(pr, SDL_PROP_THREAD_CREATE_NAME_STRING, "invitado");
  SDL_SetPointerProperty(pr, SDL_PROP_THREAD_CREATE_USERDATA_POINTER, d);
  SDL_SetNumberProperty(pr, SDL_PROP_THREAD_CREATE_STACKSIZE_NUMBER, 16 << 20);
  d->h->sdl = SDL_CreateThreadWithProperties(pr);
  SDL_DestroyProperties(pr);
  if (!d->h->sdl) { fprintf(stderr, "CreateThread: %s\n", SDL_GetError()); VOLVER(6, 0); }
  SDL_DetachThread(d->h->sdl);
  VOLVER(6, h);
}

static DatosHilo *datos_actual(void) { return datos_hilo ? datos_hilo : datos_principal; }
int es_hilo_principal(void) { return datos_hilo == NULL; }

API(ExitThread) {
  DatosHilo *d = datos_actual();
  if (!d || d == datos_principal) {
    fprintf(stderr, "ExitThread(%u) en el hilo principal\n", ARG(0));
    hilo_rastro(hilo_actual);
    fflush(NULL);
    exit((int)ARG(0));
  }
  d->codigo = ARG(0);
  longjmp(d->salida, 1);
}
API(ResumeThread) {
  DatosHilo *d = objeto_dato(ARG(0), OBJ_HILO);
  if (!d) VOLVER(1, 0xffffffffu);
  SDL_LockMutex(d->fin->m);
  uint32_t antes = (uint32_t)d->suspendido;
  d->suspendido = 0;
  SDL_BroadcastCondition(d->fin->cond);
  SDL_UnlockMutex(d->fin->m);
  VOLVER(1, antes);
}
API(SuspendThread) { fprintf(stderr, "SuspendThread ignorado\n"); VOLVER(1, 0); }
API(GetThreadPriority) { DatosHilo *d = objeto_dato(ARG(0), OBJ_HILO); VOLVER(1, d ? d->prioridad : 0); }
API(SetThreadPriority) { DatosHilo *d = objeto_dato(ARG(0), OBJ_HILO); if (d) d->prioridad = ARG(1); VOLVER(2, 1); }
API(GetCurrentThreadId) { VOLVER(0, hilo_actual->id); }
API(GetCurrentThread) { VOLVER(0, 0xfffffffeu); }
API(GetCurrentProcess) { VOLVER(0, 0xffffffffu); }
API(GetCurrentProcessId) { VOLVER(0, 0x10); }
API(GetExitCodeThread) {
  DatosHilo *d = objeto_dato(ARG(0), OBJ_HILO);
  if (!d) VOLVER(2, 0);
  M32(ARG(1)) = d->terminado ? d->codigo : 259; // STILL_ACTIVE
  VOLVER(2, 1);
}
API(DuplicateHandle) {
  uint32_t h = ARG(1);
  if (h == 0xfffffffeu) { DatosHilo *d = datos_actual(); h = d ? d->h->manejador : h; }
  if (ARG(3)) M32(ARG(3)) = h; // el mismo objeto: los objetos no se destruyen al cerrar
  VOLVER(7, 1);
}

void hilos_iniciar(Hilo *principal) {
  cerrojo_obj = SDL_CreateMutex();
  hilo_actual = principal;
  datos_principal = calloc(1, sizeof *datos_principal);
  datos_principal->h = principal;
  datos_principal->fin = sincro_nuevo();
  principal->manejador = objeto_nuevo(OBJ_HILO, datos_principal);
}

// La espera en sí, sin el turno del invitado (no toca memoria del juego).
static uint32_t esperar_objeto(uint32_t h, uint32_t ms) {
  Sincro *s;
  switch (tipo_de(h)) {
    case OBJ_EVENTO:
      s = objeto_dato(h, OBJ_EVENTO);
      if (!s->senal) { int r = socket_listo(h, ms); if (r == 1) return 0; if (r == 0) return 0x102; }
      if (esperar(s, evento_listo, ms)) return 0x102; // WAIT_TIMEOUT
      if (!s->manual) s->senal = 0;
      SDL_UnlockMutex(s->m);
      return 0;
    case OBJ_MUTEX:
      s = objeto_dato(h, OBJ_MUTEX);
      if (esperar(s, mutex_listo, ms)) return 0x102;
      s->dueno = hilo_actual->id; s->cuenta++;
      SDL_UnlockMutex(s->m);
      return 0;
    case OBJ_SEMAFORO:
      s = objeto_dato(h, OBJ_SEMAFORO);
      if (esperar(s, semaforo_listo, ms)) return 0x102;
      s->valor--;
      SDL_UnlockMutex(s->m);
      return 0;
    case OBJ_HILO: {
      DatosHilo *d = objeto_dato(h, OBJ_HILO);
      if (esperar(d->fin, hilo_listo, ms)) return 0x102;
      SDL_UnlockMutex(d->fin->m);
      return 0;
    }
    default:
      fprintf(stderr, "WaitForSingleObject sobre handle %08x de tipo %d\n", h, tipo_de(h));
      return 0xffffffffu;
  }
}
API(WaitForSingleObject) {
  uint32_t h = ARG(0), ms = ARG(1);
  invitado_soltar();
  uint32_t r = esperar_objeto(h, ms);
  invitado_tomar();
  VOLVER(2, r);
}

// ---- TLS: las ranuras viven en el TEB, como en Windows ----
static uint64_t tls_usadas;
API(TlsAlloc) {
  SDL_LockMutex(cerrojo_obj);
  for (uint32_t i = 0; i < N_TLS; i++) if (!(tls_usadas & (1ull << i))) {
    tls_usadas |= 1ull << i;
    SDL_UnlockMutex(cerrojo_obj);
    VOLVER(0, i);
  }
  SDL_UnlockMutex(cerrojo_obj);
  VOLVER(0, 0xffffffffu);
}
API(TlsFree) { tls_usadas &= ~(1ull << (ARG(0) % N_TLS)); VOLVER(1, 1); }
API(TlsGetValue) {
  uint32_t i = ARG(0);
  if (i >= N_TLS) { poner_error(ERROR_INVALID_PARAMETER); VOLVER(1, 0); }
  poner_error(0);
  VOLVER(1, M32(hilo_actual->teb + TEB_TLS + 4 * i));
}
API(TlsSetValue) {
  uint32_t i = ARG(0);
  if (i >= N_TLS) VOLVER(2, 0);
  M32(hilo_actual->teb + TEB_TLS + 4 * i) = ARG(1);
  VOLVER(2, 1);
}

// ---- secciones críticas: el mutex del anfitrión va en LockSemaphore (offset 16) ----
#define MAX_CS 2048
static SDL_Mutex *secciones[MAX_CS];
static int n_secciones;
static SDL_Mutex *seccion(uint32_t cs) {
  uint32_t k = M32(cs + 16);
  if (k && k <= (uint32_t)n_secciones && M32(cs) == 0x43530000u) return secciones[k - 1];
  // Sin inicializar: se inicializa ahora (alguna sección estática del CRT).
  SDL_LockMutex(cerrojo_obj);
  k = M32(cs + 16);
  if (!(k && k <= (uint32_t)n_secciones && M32(cs) == 0x43530000u)) {
    if (n_secciones >= MAX_CS) { fprintf(stderr, "demasiadas secciones críticas\n"); exit(1); }
    secciones[n_secciones++] = SDL_CreateMutex();
    M32(cs) = 0x43530000u; M32(cs + 4) = 0xffffffffu; M32(cs + 8) = 0; M32(cs + 12) = 0;
    M32(cs + 16) = k = (uint32_t)n_secciones; M32(cs + 20) = 0;
  }
  SDL_UnlockMutex(cerrojo_obj);
  return secciones[k - 1];
}
API(InitializeCriticalSection) { M32(ARG(0)) = 0; M32(ARG(0) + 16) = 0; seccion(ARG(0)); VOLVER(1, 0); }
API(DeleteCriticalSection) { VOLVER(1, 0); }
API(EnterCriticalSection) {
  uint32_t cs = ARG(0);
  // Ocupada: se espera sin el turno del invitado (si no, el dueño no podría seguir para soltarla).
  SDL_Mutex *m = seccion(cs);
  if (!SDL_TryLockMutex(m)) { invitado_soltar(); SDL_LockMutex(m); invitado_tomar(); }
  M32(cs + 12) = hilo_actual->id; M32(cs + 8) += 1;
  VOLVER(1, 0);
}
API(LeaveCriticalSection) {
  uint32_t cs = ARG(0);
  if (--M32(cs + 8) == 0) M32(cs + 12) = 0;
  SDL_UnlockMutex(seccion(cs));
  VOLVER(1, 0);
}

API(InterlockedIncrement) { VOLVER(1, __atomic_add_fetch((int32_t *)(MEM + ARG(0)), 1, __ATOMIC_SEQ_CST)); }
API(InterlockedDecrement) { VOLVER(1, __atomic_sub_fetch((int32_t *)(MEM + ARG(0)), 1, __ATOMIC_SEQ_CST)); }
API(InterlockedExchange) { VOLVER(2, __atomic_exchange_n((int32_t *)(MEM + ARG(0)), (int32_t)ARG(1), __ATOMIC_SEQ_CST)); }
API(Sleep) {
  uint32_t ms = ARG(0);
  if (!ms) invitado_ceder();
  else { invitado_soltar(); SDL_Delay(ms); invitado_tomar(); }
  VOLVER(1, 0);
}

const Exportada EXP_K32_HILOS[] = {
  {"GetLastError", GetLastError}, {"SetLastError", SetLastError}, {"CloseHandle", CloseHandle},
  {"CreateEventA", CreateEventA}, {"SetEvent", SetEvent}, {"ResetEvent", ResetEvent}, {"PulseEvent", PulseEvent},
  {"CreateMutexA", CreateMutexA}, {"ReleaseMutex", ReleaseMutex}, {"CreateSemaphoreA", CreateSemaphoreA},
  {"ReleaseSemaphore", ReleaseSemaphore}, {"CreateThread", CreateThread}, {"ExitThread", ExitThread},
  {"ResumeThread", ResumeThread}, {"SuspendThread", SuspendThread}, {"GetThreadPriority", GetThreadPriority},
  {"SetThreadPriority", SetThreadPriority}, {"GetCurrentThreadId", GetCurrentThreadId},
  {"GetCurrentThread", GetCurrentThread}, {"GetCurrentProcess", GetCurrentProcess},
  {"GetCurrentProcessId", GetCurrentProcessId}, {"GetExitCodeThread", GetExitCodeThread},
  {"DuplicateHandle", DuplicateHandle}, {"WaitForSingleObject", WaitForSingleObject},
  {"TlsAlloc", TlsAlloc}, {"TlsFree", TlsFree}, {"TlsGetValue", TlsGetValue}, {"TlsSetValue", TlsSetValue},
  {"InitializeCriticalSection", InitializeCriticalSection}, {"DeleteCriticalSection", DeleteCriticalSection},
  {"EnterCriticalSection", EnterCriticalSection}, {"LeaveCriticalSection", LeaveCriticalSection},
  {"InterlockedIncrement", InterlockedIncrement}, {"InterlockedDecrement", InterlockedDecrement},
  {"InterlockedExchange", InterlockedExchange}, {"Sleep", Sleep},
  {0, 0},
};
