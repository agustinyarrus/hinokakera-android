// Arranque: memoria del invitado, imagen del exe en 0x400000, importaciones, hilo principal y entrada.
// Uso: hinokakera [--datos DIR] [--escritura DIR] [--imagen imagen.bin]
#include "cpu.h"
#include "despacho.h"
#include "hilo.h"
#include "memoria.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <stdlib.h>

// Archivos del port (imagen.bin, glifos.bin): junto al exe en Windows, en los assets del APK en Android.
const char *ruta_recurso(const char *nombre, char *buf, size_t tam) {
#ifdef SDL_PLATFORM_ANDROID
  snprintf(buf, tam, "%s", nombre);
#else
  snprintf(buf, tam, "%s%s", SDL_GetBasePath(), nombre);
#endif
  return buf;
}

void resolver_importaciones(void);
void hilos_iniciar(Hilo *principal);
void vigilante_iniciar(void);
void registrar_salida(void);
void rutas_iniciar(const char *datos, const char *escritura);

// Accesos inválidos: dónde (en memoria del invitado si cae ahí), en qué parte del anfitrión y con qué pila
// del invitado (esta última con -DRASTRO). La RVA se resuelve con llvm-symbolizer sobre el .exe y su .pdb.
static void informar_acceso(uintptr_t dir, uintptr_t pc, uintptr_t base_modulo) {
  fprintf(stderr, "\n*** acceso inválido a %p", (void *)dir);
  if (dir >= (uintptr_t)MEM && dir < (uintptr_t)MEM + (1ull << 32)) fprintf(stderr, " (invitado %08x)", (uint32_t)(dir - (uintptr_t)MEM));
  fprintf(stderr, " desde %p (RVA %llx)\n", (void *)pc, (unsigned long long)(pc - base_modulo));
  hilo_rastro(hilo_actual);
  fflush(stderr);
}
#ifdef _WIN32
#include <windows.h>
static LONG WINAPI ante_excepcion(EXCEPTION_POINTERS *e) {
  if (e->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
  informar_acceso((uintptr_t)e->ExceptionRecord->ExceptionInformation[1], (uintptr_t)e->ExceptionRecord->ExceptionAddress, (uintptr_t)GetModuleHandleA(NULL));
  TerminateProcess(GetCurrentProcess(), 4);
  return EXCEPTION_CONTINUE_SEARCH;
}
#else
#include <signal.h>
#include <unistd.h>
static struct sigaction previa[2];
static void ante_senal(int s, siginfo_t *i, void *ctx) {
  (void)ctx;
  informar_acceso((uintptr_t)i->si_addr, 0, 0);
#ifdef SDL_PLATFORM_ANDROID
  usleep(300000); // que el hilo de logcat alcance a escribirlo
#endif
  sigaction(s, &previa[s == SIGBUS], NULL); // al volver, la falla se repite con el manejador de antes (en Android, el tombstone)
}
#endif

#ifdef SDL_PLATFORM_ANDROID
#include <android/log.h>
// En Android stderr no llega a ningún lado: se pasa a logcat (etiqueta "hino") línea por línea.
static int tubo[2];
static int a_logcat(void *p) {
  (void)p;
  char linea[2048];
  FILE *f = fdopen(tubo[0], "r");
  while (f && fgets(linea, sizeof linea, f)) __android_log_write(ANDROID_LOG_INFO, "hino", linea);
  return 0;
}
// Variables de prueba (HINO_CAPTURAS, HINO_TECLAS...): <almacenamiento externo>/entorno.txt, NOMBRE=valor por línea.
static void leer_entorno(const char *dir) {
  char ruta[1024], *estado = NULL;
  snprintf(ruta, sizeof ruta, "%s/entorno.txt", dir);
  char *t = SDL_LoadFile(ruta, NULL);
  for (char *l = t ? SDL_strtok_r(t, "\r\n", &estado) : NULL; l; l = SDL_strtok_r(NULL, "\r\n", &estado)) {
    char *ig = strchr(l, '=');
    if (ig) { *ig = 0; setenv(l, ig + 1, 1); fprintf(stderr, "entorno: %s=%s\n", l, ig + 1); }
  }
  SDL_free(t);
}
#endif

static int juego(int argc, char **argv) {
  const char *datos = getenv("HINO_DATOS"), *escritura = getenv("HINO_ESCRITURA"), *imagen = NULL;
  for (int i = 1; i + 1 < argc; i += 2) {
    if (!strcmp(argv[i], "--datos")) datos = argv[i + 1];
    else if (!strcmp(argv[i], "--escritura")) escritura = argv[i + 1];
    else if (!strcmp(argv[i], "--imagen")) imagen = argv[i + 1];
  }
  char ruta_imagen[1024];
  if (!imagen) imagen = ruta_recurso("imagen.bin", ruta_imagen, sizeof ruta_imagen);
#ifdef SDL_PLATFORM_ANDROID
  // Datos: los assets del APK (rutas relativas). Escritura: el almacenamiento interno de la app.
  if (!datos) datos = "";
  if (!escritura) escritura = SDL_GetPrefPath("Reddish Region", "Hinokakera CE");
  fprintf(stderr, "datos: %s / escritura: %s\n", datos[0] ? datos : "(assets del APK)", escritura);
#endif
  if (!datos || !escritura) { fprintf(stderr, "faltan --datos y --escritura (o HINO_DATOS / HINO_ESCRITURA)\n"); return 2; }
  SDL_CreateDirectory(escritura);
  rutas_iniciar(datos, escritura);
#ifdef SDL_PLATFORM_ANDROID
  SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1"); // Atrás no cierra el juego: es Pausa (tactil.c)
  SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");       // los toques los reparte tactil.c (controles o mouse)
#endif
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 2; }

  mem_iniciar();
#ifdef _WIN32
  AddVectoredExceptionHandler(0, ante_excepcion);
#else
  struct sigaction sa = {0};
  sa.sa_sigaction = ante_senal;
  sa.sa_flags = SA_SIGINFO;
  sigaction(SIGSEGV, &sa, &previa[0]);
  sigaction(SIGBUS, &sa, &previa[1]);
#endif
  size_t tam;
  void *img = SDL_LoadFile(imagen, &tam);
  if (!img || tam != TAM_IMAGEN) { fprintf(stderr, "imagen %s inválida (%zu bytes, se esperaban %u)\n", imagen, tam, TAM_IMAGEN); return 2; }
  if (mem_reservar(0x400000u, TAM_IMAGEN, 1) != 0x400000u) { fprintf(stderr, "no se pudo ubicar la imagen\n"); return 2; }
  memcpy(MEM + 0x400000u, img, tam);
  SDL_free(img);

  Hilo *h = hilo_nuevo(0x100000);
  hilos_iniciar(h);
  vigilante_iniciar();
  registrar_salida();
  resolver_importaciones();
  fprintf(stderr, "entrando en %08x\n", ENTRADA_PE);
  invitado_tomar(); // el hilo principal corre con el turno del invitado (ver hilo.h)
  uint32_t r = llamar_invitado(&h->ctx, ENTRADA_PE, 0, NULL, 0);
  fprintf(stderr, "el invitado volvió de la entrada con %u\n", r);
  return (int)r;
}

#ifdef SDL_PLATFORM_ANDROID
// El hilo donde Android corre SDL_main trae ~1 MB de pila y el código recompilado anida mucho (en Windows se
// linkea con 64 MB): el juego va en un hilo propio, que pasa a ser el principal de SDL al iniciar el video.
static int correr_juego(void *p) { (void)p; return juego(0, NULL); }
int main(int argc, char **argv) {
  (void)argc; (void)argv;
  if (!pipe(tubo)) { dup2(tubo[1], 2); dup2(tubo[1], 1); SDL_DetachThread(SDL_CreateThread(a_logcat, "logcat", NULL)); }
  const char *ext = SDL_GetAndroidExternalStoragePath();
  if (ext) leer_entorno(ext);
  SDL_PropertiesID pr = SDL_CreateProperties();
  SDL_SetPointerProperty(pr, SDL_PROP_THREAD_CREATE_ENTRY_FUNCTION_POINTER, (void *)correr_juego);
  SDL_SetStringProperty(pr, SDL_PROP_THREAD_CREATE_NAME_STRING, "juego");
  SDL_SetNumberProperty(pr, SDL_PROP_THREAD_CREATE_STACKSIZE_NUMBER, 64 << 20);
  int r = 2;
  SDL_WaitThread(SDL_CreateThreadWithProperties(pr), &r);
  return r;
}
#else
int main(int argc, char **argv) { return juego(argc, argv); }
#endif

// Para saber por dónde termina el proceso cuando nadie lo avisa.
static void al_salir(void) { fprintf(stderr, "fin del proceso (atexit del anfitrión)\n"); hilo_rastro(hilo_actual); fflush(stderr); }
void registrar_salida(void) { atexit(al_salir); }
