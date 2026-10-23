// USER32: clases y ventanas (las de nivel superior son ventanas de SDL), cola de mensajes alimentada por los
// eventos de SDL y entregada al WndProc del juego, teclado, métricas, cadenas y MessageBoxA.
#include "win32.h"
#include "teclas.h"
#include "user32.h"
#include <SDL3/SDL.h>
#include <ctype.h>
#include <stdlib.h>

#define WS_CAPTION 0x00c00000u
#define WS_VISIBLE 0x10000000u
#define WS_CHILD 0x40000000u
#define WM_CREATE 0x01
#define WM_DESTROY 0x02
#define WM_MOVE 0x03
#define WM_SIZE 0x05
#define WM_ACTIVATE 0x06
#define WM_SETFOCUS 0x07
#define WM_KILLFOCUS 0x08
#define WM_CLOSE 0x10
#define WM_QUIT 0x12
#define WM_ERASEBKGND 0x14
#define WM_SHOWWINDOW 0x18
#define WM_ACTIVATEAPP 0x1c
#define WM_SETCURSOR 0x20
#define WM_NCCREATE 0x81
#define WM_NCDESTROY 0x82
#define WM_NCHITTEST 0x84
#define WM_KEYDOWN 0x100
#define WM_KEYUP 0x101
#define WM_CHAR 0x102
#define WM_SYSKEYDOWN 0x104
#define WM_SYSKEYUP 0x105
#define WM_SYSCOMMAND 0x112
#define WM_MOUSEMOVE 0x200

typedef struct { char nombre[128]; uint32_t atomo, wndproc, estilo, icono, cursor, fondo, instancia, extra; } Clase;
#define MAX_CLASES 64
static Clase clases[MAX_CLASES];
static int n_clases;

#define MAX_VENTANAS 64
static Ventana ventanas[MAX_VENTANAS];
static int n_ventanas;
#define HWND_BASE 0x00020000u
#define ESCRITORIO 0x00010010u

Ventana *ventana(uint32_t hwnd) {
  uint32_t k = (hwnd - HWND_BASE) / 0x10;
  if (hwnd < HWND_BASE || (hwnd - HWND_BASE) % 0x10 || k >= (uint32_t)n_ventanas || ventanas[k].destruida) return NULL;
  return &ventanas[k];
}
Ventana *ventana_principal(void) {
  for (int i = 0; i < n_ventanas; i++) if (!ventanas[i].destruida && !ventanas[i].padre) return &ventanas[i];
  return NULL;
}

// Marco: con barra de título, 3 px a los lados y abajo y 22 arriba (título de 19 + borde); el juego calcula
// el tamaño de ventana con GetSystemMetrics y estos números hacen que el área cliente salga exacta.
static void marco(uint32_t estilo, int *iz, int *ar, int *de, int *ab) {
  if ((estilo & WS_CAPTION) == WS_CAPTION) { *iz = *de = *ab = 3; *ar = 22; }
  else if (estilo & 0x00800000u) { *iz = *de = *ab = *ar = 1; } // WS_BORDER
  else *iz = *de = *ab = *ar = 0;
}
static void recalcular_cliente(Ventana *v) {
  int iz, ar, de, ab;
  marco(v->estilo, &iz, &ar, &de, &ab);
  v->cw = v->w - iz - de; v->ch = v->h - ar - ab;
  if (v->cw < 1) v->cw = 1;
  if (v->ch < 1) v->ch = 1;
}

// UTF-8 a partir de Shift-JIS (para títulos y mensajes).
uint16_t sjis_a_unicode(uint32_t c);
void sjis_a_utf8(const char *s, char *out, size_t tam) {
  size_t k = 0;
  for (const uint8_t *p = (const uint8_t *)s; *p && k + 4 < tam; p++) {
    uint32_t u = *p;
    if (*p >= 0xa1 && *p <= 0xdf) u = 0xff61 + (*p - 0xa1u);
    else if (((*p >= 0x81 && *p <= 0x9f) || (*p >= 0xe0 && *p <= 0xfc)) && p[1]) { u = sjis_a_unicode(((uint32_t)p[0] << 8) | p[1]); p++; }
    if (u < 0x80) out[k++] = (char)u;
    else if (u < 0x800) { out[k++] = (char)(0xc0 | u >> 6); out[k++] = (char)(0x80 | (u & 0x3f)); }
    else { out[k++] = (char)(0xe0 | u >> 12); out[k++] = (char)(0x80 | ((u >> 6) & 0x3f)); out[k++] = (char)(0x80 | (u & 0x3f)); }
  }
  out[k] = 0;
}

uint32_t enviar(Ctx *c, uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp) {
  Ventana *v = ventana(hwnd);
  if (!v || !v->wndproc) return 0;
  uint32_t args[4] = {hwnd, msg, wp, lp};
  uint32_t eax = c->eax, ecx = c->ecx, edx = c->edx;
  uint32_t r = llamar_invitado(c, v->wndproc, 4, args, 0);
  c->eax = eax; c->ecx = ecx; c->edx = edx;
  return r;
}

// ---- cola de mensajes ----
typedef struct { uint32_t hwnd, msg, wp, lp, tiempo, x, y; } Mensaje;
#define COLA 1024
static Mensaje cola[COLA];
static int cola_ini, cola_n;
static SDL_Mutex *cerrojo;
static uint32_t ultimo_tiempo, ultimo_x, ultimo_y;

// Mouse en coordenadas del juego (las del backbuffer): lo mueven el mouse de verdad, deshaciendo la escala de
// presentar(), y en Android tocar la imagen del juego (tactil.c). Botones con las máscaras de SDL.
int raton_x, raton_y, raton_dx, raton_dy;
uint32_t raton_botones;

void encolar(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp) {
  if (!cerrojo) cerrojo = SDL_CreateMutex();
  SDL_LockMutex(cerrojo);
  if (cola_n < COLA) cola[(cola_ini + cola_n++) % COLA] = (Mensaje){hwnd, msg, wp, lp, (uint32_t)SDL_GetTicks(), (uint32_t)raton_x, (uint32_t)raton_y};
  SDL_UnlockMutex(cerrojo);
}

void raton_evento(uint32_t ofs, uint32_t dato); // dinput8.c: el buffer de DirectInput
void raton_juego(int x, int y, uint32_t botones) {
  Ventana *v = ventana_principal();
  if (!v) return;
  uint32_t lp = ((uint32_t)y << 16) | ((uint32_t)x & 0xffff);
  raton_dx += x - raton_x; raton_dy += y - raton_y;
  if (x != raton_x) raton_evento(0, (uint32_t)(x - raton_x)); // DIMOFS_X
  if (y != raton_y) raton_evento(4, (uint32_t)(y - raton_y)); // DIMOFS_Y
  if (x != raton_x || y != raton_y) { raton_x = x; raton_y = y; encolar(v->hwnd, WM_MOUSEMOVE, 0, lp); }
  // Botón i de DirectInput = DIMOFS_BUTTON0 + i (izquierdo, derecho, medio).
  static const struct { uint32_t mascara, msg; } B[] = {{SDL_BUTTON_LMASK, 0x201}, {SDL_BUTTON_RMASK, 0x204}, {SDL_BUTTON_MMASK, 0x207}};
  for (int i = 0; i < 3; i++)
    if ((botones ^ raton_botones) & B[i].mascara) {
      encolar(v->hwnd, B[i].msg + !(botones & B[i].mascara), 0, lp);
      raton_evento(12u + (uint32_t)i, botones & B[i].mascara ? 0x80u : 0);
    }
  raton_botones = botones;
}

extern int imagen_juego[7]; // dibujo.c
static void a_juego(SDL_WindowID id, float *x, float *y) {
  if (!imagen_juego[2] || !imagen_juego[3]) return;
  SDL_Window *w = SDL_GetWindowFromID(id);
  float dens = w ? SDL_GetWindowPixelDensity(w) : 1;
  *x = (*x * dens - (float)imagen_juego[0]) * (float)imagen_juego[4] / (float)imagen_juego[2] - (float)imagen_juego[6];
  *y = (*y * dens - (float)imagen_juego[1]) * (float)imagen_juego[5] / (float)imagen_juego[3];
}

static Ventana *ventana_sdl(SDL_WindowID id) {
  for (int i = 0; i < n_ventanas; i++) if (ventanas[i].sdl && SDL_GetWindowID(ventanas[i].sdl) == id && !ventanas[i].destruida) return &ventanas[i];
  return ventana_principal();
}

void (*oyente_eventos)(const SDL_Event *e); // DirectInput y los controles táctiles miran los eventos

static void bombear(void) {
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    if (oyente_eventos) oyente_eventos(&e);
    Ventana *v = NULL;
    switch (e.type) {
      case SDL_EVENT_QUIT:
        if ((v = ventana_principal())) encolar(v->hwnd, WM_CLOSE, 0, 0);
        break;
      case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        if ((v = ventana_sdl(e.window.windowID))) encolar(v->hwnd, WM_CLOSE, 0, 0);
        break;
      case SDL_EVENT_WINDOW_FOCUS_GAINED:
      case SDL_EVENT_WINDOW_FOCUS_LOST:
        if ((v = ventana_sdl(e.window.windowID))) {
          int si = e.type == SDL_EVENT_WINDOW_FOCUS_GAINED;
          encolar(v->hwnd, WM_ACTIVATEAPP, (uint32_t)si, 0);
          encolar(v->hwnd, WM_ACTIVATE, (uint32_t)si, 0);
          encolar(v->hwnd, si ? WM_SETFOCUS : WM_KILLFOCUS, 0, 0);
        }
        break;
      case SDL_EVENT_WINDOW_MINIMIZED:
      case SDL_EVENT_WINDOW_RESTORED:
        if ((v = ventana_sdl(e.window.windowID)))
          encolar(v->hwnd, WM_SIZE, e.type == SDL_EVENT_WINDOW_MINIMIZED ? 1u : 0u, ((uint32_t)v->ch << 16) | (uint32_t)v->cw);
        break;
      case SDL_EVENT_KEY_DOWN:
      case SDL_EVENT_KEY_UP: {
        const Tecla *t = tecla_sdl(e.key.scancode);
        if (!t || !(v = ventana_sdl(e.key.windowID))) break;
        int baja = e.type == SDL_EVENT_KEY_DOWN, alt = (e.key.mod & SDL_KMOD_ALT) != 0;
        uint32_t lp = 1u | ((uint32_t)(t->dik & 0x7f) << 16) | (t->dik & 0x80 ? 1u << 24 : 0) | (alt ? 1u << 29 : 0);
        if (!baja) lp |= 3u << 30;
        else if (e.key.repeat) lp |= 1u << 30;
        uint32_t msg = alt || t->vk == 0x12 || t->vk == 0x79 ? (baja ? WM_SYSKEYDOWN : WM_SYSKEYUP) : (baja ? WM_KEYDOWN : WM_KEYUP);
        encolar(v->hwnd, msg, t->vk, lp);
        break;
      }
      case SDL_EVENT_MOUSE_MOTION: {
        float x = e.motion.x, y = e.motion.y;
        a_juego(e.motion.windowID, &x, &y);
        raton_juego((int)x, (int)y, raton_botones);
        break;
      }
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
      case SDL_EVENT_MOUSE_BUTTON_UP: {
        float x = e.button.x, y = e.button.y;
        a_juego(e.button.windowID, &x, &y);
        uint32_t m = SDL_BUTTON_MASK(e.button.button);
        raton_juego((int)x, (int)y, e.button.down ? raton_botones | m : raton_botones & ~m);
        break;
      }
    }
  }
}

static int sacar(Mensaje *m, uint32_t hwnd, uint32_t min, uint32_t max, int quitar) {
  if (!cerrojo) cerrojo = SDL_CreateMutex();
  SDL_LockMutex(cerrojo);
  for (int i = 0; i < cola_n; i++) {
    Mensaje *x = &cola[(cola_ini + i) % COLA];
    if (hwnd && x->hwnd != hwnd && x->msg != WM_QUIT) continue;
    if ((min || max) && (x->msg < min || x->msg > max)) continue;
    *m = *x;
    if (quitar) {
      for (int k = i; k > 0; k--) cola[(cola_ini + k) % COLA] = cola[(cola_ini + k - 1) % COLA];
      cola_ini = (cola_ini + 1) % COLA; cola_n--;
    }
    SDL_UnlockMutex(cerrojo);
    return 1;
  }
  SDL_UnlockMutex(cerrojo);
  return 0;
}

static void a_msg(uint32_t p, const Mensaje *m) {
  M32(p) = m->hwnd; M32(p + 4) = m->msg; M32(p + 8) = m->wp; M32(p + 12) = m->lp;
  M32(p + 16) = m->tiempo; M32(p + 20) = m->x; M32(p + 24) = m->y;
  ultimo_tiempo = m->tiempo; ultimo_x = m->x; ultimo_y = m->y;
}

API(PeekMessageA) {
  Mensaje m;
  invitado_ceder(); // el juego la llama en cada vuelta: los otros hilos del juego corren acá
  bombear();
  if (!sacar(&m, ARG(1), ARG(2), ARG(3), ARG(4) & 1)) VOLVER(5, 0);
  a_msg(ARG(0), &m);
  VOLVER(5, 1);
}
API(GetMessageA) {
  Mensaje m;
  for (;;) {
    bombear();
    if (sacar(&m, ARG(1), ARG(2), ARG(3), 1)) break;
    invitado_soltar();
    SDL_WaitEventTimeout(NULL, 10);
    invitado_tomar();
  }
  a_msg(ARG(0), &m);
  VOLVER(4, m.msg != WM_QUIT);
}
API(TranslateMessage) {
  uint32_t p = ARG(0), msg = M32(p + 4), vk = M32(p + 8);
  if (msg != WM_KEYDOWN) VOLVER(1, 0);
  const bool *k = SDL_GetKeyboardState(NULL);
  int mayus = k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT];
  uint32_t ch = 0;
  if (vk >= 'A' && vk <= 'Z') ch = mayus ? vk : vk + 32;
  else if ((vk >= '0' && vk <= '9') || vk == ' ' || vk == 0x0d || vk == 0x08 || vk == 0x1b || vk == 0x09) ch = vk;
  if (ch) encolar(M32(p), WM_CHAR, ch, M32(p + 12));
  VOLVER(1, ch != 0);
}
API(DispatchMessageA) {
  uint32_t p = ARG(0);
  VOLVER(1, enviar(c, M32(p), M32(p + 4), M32(p + 8), M32(p + 12)));
}
API(SendMessageA) { VOLVER(4, enviar(c, ARG(0), ARG(1), ARG(2), ARG(3))); }
API(PostMessageA) { encolar(ARG(0), ARG(1), ARG(2), ARG(3)); VOLVER(4, 1); }
API(PostThreadMessageA) { encolar(0, ARG(1), ARG(2), ARG(3)); VOLVER(4, 1); }
API(SendNotifyMessageA) { VOLVER(4, enviar(c, ARG(0), ARG(1), ARG(2), ARG(3))); }
API(PostQuitMessage) { encolar(0, WM_QUIT, ARG(0), 0); VOLVER(1, 0); }
API(GetMessageTime) { VOLVER(0, ultimo_tiempo); }
API(GetMessagePos) { VOLVER(0, (ultimo_y << 16) | (ultimo_x & 0xffff)); }

static void destruir(Ctx *c, Ventana *v);
API(DefWindowProcA) {
  uint32_t hwnd = ARG(0), msg = ARG(1), wp = ARG(2), lp = ARG(3);
  Ventana *v = ventana(hwnd);
  switch (msg) {
    case WM_CLOSE: if (v) destruir(c, v); VOLVER(4, 0);
    case WM_NCCREATE: VOLVER(4, 1);
    case WM_ERASEBKGND: VOLVER(4, 1);
    case WM_NCHITTEST: VOLVER(4, 1);
    case WM_SETCURSOR: VOLVER(4, 0);
    case WM_SYSCOMMAND:
      if ((wp & 0xfff0) == 0xf060) { enviar(c, hwnd, WM_CLOSE, 0, 0); VOLVER(4, 0); } // SC_CLOSE
      VOLVER(4, 0);
    case 0x0c: // WM_SETTEXT
      if (v && lp) { char u[512]; snprintf(v->titulo, sizeof v->titulo, "%s", gstr(lp)); sjis_a_utf8(v->titulo, u, sizeof u); if (v->sdl) SDL_SetWindowTitle(v->sdl, u); }
      VOLVER(4, 1);
    default: (void)lp; VOLVER(4, 0);
  }
}

// ---- clases ----
static Clase *buscar_clase(uint32_t nombre) {
  for (int i = 0; i < n_clases; i++)
    if (nombre < 0x10000 ? clases[i].atomo == nombre : !SDL_strcasecmp(clases[i].nombre, gstr(nombre))) return &clases[i];
  return NULL;
}
static uint32_t registrar_clase(uint32_t nombre, uint32_t wndproc, uint32_t estilo, uint32_t icono, uint32_t cursor, uint32_t fondo, uint32_t instancia, uint32_t extra) {
  Clase *k = buscar_clase(nombre);
  if (!k) { if (n_clases >= MAX_CLASES) return 0; k = &clases[n_clases++]; }
  snprintf(k->nombre, sizeof k->nombre, "%s", nombre < 0x10000 ? "" : gstr(nombre));
  k->atomo = 0xc100u + (uint32_t)(k - clases);
  k->wndproc = wndproc; k->estilo = estilo; k->icono = icono; k->cursor = cursor; k->fondo = fondo; k->instancia = instancia; k->extra = extra;
  return k->atomo;
}
API(RegisterClassExA) {
  uint32_t w = ARG(0);
  VOLVER(1, registrar_clase(M32(w + 40), M32(w + 8), M32(w + 4), M32(w + 24), M32(w + 28), M32(w + 32), M32(w + 20), M32(w + 16)));
}
API(RegisterClassA) {
  uint32_t w = ARG(0);
  VOLVER(1, registrar_clase(M32(w + 36), M32(w + 4), M32(w), M32(w + 20), M32(w + 24), M32(w + 28), M32(w + 16), M32(w + 12)));
}
API(GetClassInfoA) {
  Clase *k = buscar_clase(ARG(1));
  if (!k) VOLVER(3, 0);
  uint32_t w = ARG(2);
  memset(MEM + w, 0, 40);
  M32(w) = k->estilo; M32(w + 4) = k->wndproc; M32(w + 12) = k->extra; M32(w + 16) = k->instancia;
  M32(w + 20) = k->icono; M32(w + 24) = k->cursor; M32(w + 28) = k->fondo; M32(w + 36) = ARG(1);
  VOLVER(3, 1);
}
API(UnregisterClassA) { VOLVER(2, 1); }

// ---- ventanas ----
API(CreateWindowExA) {
  uint32_t ex = ARG(0), nombre_clase = ARG(1), titulo = ARG(2), estilo = ARG(3);
  int32_t x = (int32_t)ARG(4), y = (int32_t)ARG(5), w = (int32_t)ARG(6), h = (int32_t)ARG(7);
  uint32_t padre = ARG(8), menu = ARG(9), instancia = ARG(10), param = ARG(11);
  Clase *k = buscar_clase(nombre_clase);
  if (!k || n_ventanas >= MAX_VENTANAS) { fprintf(stderr, "CreateWindowExA: clase desconocida\n"); poner_error(1407); VOLVER(12, 0); }
  if (x == (int32_t)0x80000000) { x = 64; y = 64; }
  if (w == (int32_t)0x80000000) { w = 640; h = 480; }
  Ventana *v = &ventanas[n_ventanas];
  memset(v, 0, sizeof *v);
  v->hwnd = HWND_BASE + 0x10u * (uint32_t)n_ventanas++;
  v->wndproc = k->wndproc; v->estilo = estilo; v->estilo_ex = ex; v->padre = padre; v->menu = menu; v->instancia = instancia;
  v->x = x; v->y = y; v->w = w; v->h = h;
  recalcular_cliente(v);
  snprintf(v->titulo, sizeof v->titulo, "%s", titulo ? gstr(titulo) : "");
  if (!(estilo & WS_CHILD)) {
    char u[512];
    sjis_a_utf8(v->titulo, u, sizeof u);
    v->sdl = SDL_CreateWindow(u, v->cw, v->ch, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!v->sdl) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); v->destruida = 1; VOLVER(12, 0); }
  }
  // CREATESTRUCTA en la pila del invitado, debajo de lo que ya hay.
  c->esp -= 48;
  uint32_t cs = c->esp;
  uint32_t vals[12] = {param, instancia, menu, padre, (uint32_t)h, (uint32_t)w, (uint32_t)y, (uint32_t)x, estilo, titulo, nombre_clase, ex};
  for (int i = 0; i < 12; i++) M32(cs + 4 * i) = vals[i];
  int ok = enviar(c, v->hwnd, WM_NCCREATE, 0, cs) != 0 && (int32_t)enviar(c, v->hwnd, WM_CREATE, 0, cs) != -1;
  c->esp += 48;
  if (!ok) { destruir(c, v); VOLVER(12, 0); }
  enviar(c, v->hwnd, WM_SIZE, 0, ((uint32_t)v->ch << 16) | (uint32_t)v->cw);
  enviar(c, v->hwnd, WM_MOVE, 0, ((uint32_t)(v->y) << 16) | ((uint32_t)v->x & 0xffff));
  if (estilo & WS_VISIBLE) { mostrar_ventana(v->sdl); v->visible = 1; }
  VOLVER(12, v->hwnd);
}

static void destruir(Ctx *c, Ventana *v) {
  if (v->destruida) return;
  enviar(c, v->hwnd, WM_DESTROY, 0, 0);
  enviar(c, v->hwnd, WM_NCDESTROY, 0, 0);
  if (v->sdl && !v->conserva_sdl) SDL_DestroyWindow(v->sdl);
  v->destruida = 1;
}
API(DestroyWindow) { Ventana *v = ventana(ARG(0)); if (v) destruir(c, v); VOLVER(1, v != NULL); }
API(ShowWindow) {
  Ventana *v = ventana(ARG(0));
  if (!v) VOLVER(2, 0);
  int antes = v->visible, cmd = (int)ARG(1);
  if (v->sdl) {
    if (cmd == 0) SDL_HideWindow(v->sdl);
    else if (cmd == 2 || cmd == 6 || cmd == 7) SDL_MinimizeWindow(v->sdl);
    else { mostrar_ventana(v->sdl); if (cmd == 9 || cmd == 1) SDL_RestoreWindow(v->sdl); }
  }
  v->visible = cmd != 0;
  if (v->visible != antes) enviar(c, v->hwnd, WM_SHOWWINDOW, (uint32_t)v->visible, 0);
  VOLVER(2, antes);
}
API(UpdateWindow) { VOLVER(1, 1); }
API(IsWindow) { VOLVER(1, ventana(ARG(0)) != NULL); }
API(IsWindowVisible) { Ventana *v = ventana(ARG(0)); VOLVER(1, v && v->visible); }
API(IsIconic) { Ventana *v = ventana(ARG(0)); VOLVER(1, v && v->sdl && (SDL_GetWindowFlags(v->sdl) & SDL_WINDOW_MINIMIZED)); }
API(IsZoomed) { VOLVER(1, 0); }
API(IsWindowEnabled) { VOLVER(1, 1); }
API(EnableWindow) { VOLVER(2, 0); }
API(GetParent) { Ventana *v = ventana(ARG(0)); VOLVER(1, v ? v->padre : 0); }
API(GetDesktopWindow) { VOLVER(0, ESCRITORIO); }
API(GetForegroundWindow) { Ventana *v = ventana_principal(); VOLVER(0, v ? v->hwnd : 0); }
API(GetActiveWindow) { Ventana *v = ventana_principal(); VOLVER(0, v ? v->hwnd : 0); }
API(GetFocus) { Ventana *v = ventana_principal(); VOLVER(0, v ? v->hwnd : 0); }
API(SetForegroundWindow) { VOLVER(1, 1); }
API(SetActiveWindow) { Ventana *v = ventana_principal(); VOLVER(1, v ? v->hwnd : 0); }
API(SetFocus) { Ventana *v = ventana_principal(); VOLVER(1, v ? v->hwnd : 0); }
API(BringWindowToTop) { VOLVER(1, 1); }
API(GetLastActivePopup) { VOLVER(1, ARG(0)); }
API(GetWindow) { VOLVER(2, 0); }
API(GetTopWindow) { VOLVER(1, 0); }
API(FindWindowA) {
  for (int i = 0; i < n_ventanas; i++) {
    Ventana *v = &ventanas[i];
    if (v->destruida) continue;
    if (ARG(1) && strcmp(v->titulo, gstr(ARG(1)))) continue;
    if (ARG(0)) { Clase *k = buscar_clase(ARG(0)); if (!k || k->wndproc != v->wndproc) continue; }
    VOLVER(2, v->hwnd);
  }
  VOLVER(2, 0);
}

static void a_rect(uint32_t p, int l, int t, int r, int b) { M32(p) = (uint32_t)l; M32(p + 4) = (uint32_t)t; M32(p + 8) = (uint32_t)r; M32(p + 12) = (uint32_t)b; }
static void escritorio(int *w, int *h) {
  const SDL_DisplayMode *m = SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay());
  *w = m ? m->w : 1920; *h = m ? m->h : 1080;
}
API(GetWindowRect) {
  Ventana *v = ventana(ARG(0));
  if (ARG(0) == ESCRITORIO) { int w, h; escritorio(&w, &h); a_rect(ARG(1), 0, 0, w, h); VOLVER(2, 1); }
  if (!v) VOLVER(2, 0);
  a_rect(ARG(1), v->x, v->y, v->x + v->w, v->y + v->h);
  VOLVER(2, 1);
}
API(GetClientRect) {
  Ventana *v = ventana(ARG(0));
  if (ARG(0) == ESCRITORIO) { int w, h; escritorio(&w, &h); a_rect(ARG(1), 0, 0, w, h); VOLVER(2, 1); }
  if (!v) VOLVER(2, 0);
  a_rect(ARG(1), 0, 0, v->cw, v->ch);
  VOLVER(2, 1);
}
API(AdjustWindowRectEx) {
  int iz, ar, de, ab;
  marco(ARG(1), &iz, &ar, &de, &ab);
  uint32_t p = ARG(0);
  a_rect(p, (int32_t)M32(p) - iz, (int32_t)M32(p + 4) - ar, (int32_t)M32(p + 8) + de, (int32_t)M32(p + 12) + ab);
  VOLVER(4, 1);
}
static void reubicar(Ctx *c, Ventana *v, int x, int y, int w, int h, int mover, int medir) {
  if (mover) { v->x = x; v->y = y; }
  if (medir) { v->w = w; v->h = h; }
  int cw = v->cw, ch = v->ch;
  recalcular_cliente(v);
  if (v->sdl && medir && (cw != v->cw || ch != v->ch)) SDL_SetWindowSize(v->sdl, v->cw, v->ch);
  if (medir) enviar(c, v->hwnd, WM_SIZE, 0, ((uint32_t)v->ch << 16) | (uint32_t)v->cw);
}
API(SetWindowPos) {
  Ventana *v = ventana(ARG(0));
  uint32_t fl = ARG(6);
  if (!v) VOLVER(7, 0);
  reubicar(c, v, (int32_t)ARG(2), (int32_t)ARG(3), (int32_t)ARG(4), (int32_t)ARG(5), !(fl & 2), !(fl & 1));
  if (fl & 0x40 && v->sdl) { mostrar_ventana(v->sdl); v->visible = 1; }
  if (fl & 0x80 && v->sdl) { SDL_HideWindow(v->sdl); v->visible = 0; }
  VOLVER(7, 1);
}
API(MoveWindow) {
  Ventana *v = ventana(ARG(0));
  if (v) reubicar(c, v, (int32_t)ARG(1), (int32_t)ARG(2), (int32_t)ARG(3), (int32_t)ARG(4), 1, 1);
  VOLVER(6, v != NULL);
}
API(GetWindowLongA) {
  Ventana *v = ventana(ARG(0));
  int32_t i = (int32_t)ARG(1);
  if (!v) VOLVER(2, 0);
  switch (i) {
    case -4: VOLVER(2, v->wndproc);
    case -6: VOLVER(2, v->instancia);
    case -8: VOLVER(2, v->padre);
    case -12: VOLVER(2, v->menu);
    case -16: VOLVER(2, v->estilo);
    case -20: VOLVER(2, v->estilo_ex);
    case -21: VOLVER(2, v->usuario);
    default: VOLVER(2, i >= 0 && i / 4 < 16 ? v->extra[i / 4] : 0);
  }
}
API(SetWindowLongA) {
  Ventana *v = ventana(ARG(0));
  int32_t i = (int32_t)ARG(1);
  uint32_t n = ARG(2), viejo = 0;
  if (!v) VOLVER(3, 0);
  switch (i) {
    case -4: viejo = v->wndproc; v->wndproc = n; break;
    case -16: viejo = v->estilo; v->estilo = n; recalcular_cliente(v); break;
    case -20: viejo = v->estilo_ex; v->estilo_ex = n; break;
    case -21: viejo = v->usuario; v->usuario = n; break;
    default: if (i >= 0 && i / 4 < 16) { viejo = v->extra[i / 4]; v->extra[i / 4] = n; }
  }
  VOLVER(3, viejo);
}
API(SetWindowTextA) {
  Ventana *v = ventana(ARG(0));
  if (v) { char u[512]; snprintf(v->titulo, sizeof v->titulo, "%s", gstr(ARG(1))); sjis_a_utf8(v->titulo, u, sizeof u); if (v->sdl) SDL_SetWindowTitle(v->sdl, u); }
  VOLVER(2, v != NULL);
}
API(GetWindowTextA) {
  Ventana *v = ventana(ARG(0));
  if (!v || !ARG(2)) VOLVER(3, 0);
  SDL_strlcpy(gstr(ARG(1)), v->titulo, ARG(2));
  VOLVER(3, strlen(gstr(ARG(1))));
}
API(GetWindowTextLengthA) { Ventana *v = ventana(ARG(0)); VOLVER(1, v ? strlen(v->titulo) : 0); }
API(ClientToScreen) {
  Ventana *v = ventana(ARG(0));
  if (v) { int iz, ar, de, ab; marco(v->estilo, &iz, &ar, &de, &ab); M32(ARG(1)) += (uint32_t)(v->x + iz); M32(ARG(1) + 4) += (uint32_t)(v->y + ar); }
  VOLVER(2, v != NULL);
}
API(ScreenToClient) {
  Ventana *v = ventana(ARG(0));
  if (v) { int iz, ar, de, ab; marco(v->estilo, &iz, &ar, &de, &ab); M32(ARG(1)) -= (uint32_t)(v->x + iz); M32(ARG(1) + 4) -= (uint32_t)(v->y + ar); }
  VOLVER(2, v != NULL);
}
API(GetCursorPos) {
  // En pantalla = el punto del juego llevado como ClientToScreen (el juego hace la vuelta con ScreenToClient).
  Ventana *v = ventana_principal();
  int iz = 0, ar = 0, de, ab;
  if (v) marco(v->estilo, &iz, &ar, &de, &ab);
  M32(ARG(0)) = (uint32_t)(raton_x + (v ? v->x + iz : 0)); M32(ARG(0) + 4) = (uint32_t)(raton_y + (v ? v->y + ar : 0));
  VOLVER(1, 1);
}
API(SetCursorPos) { VOLVER(2, 1); }
API(SetCursor) { static uint32_t actual; uint32_t viejo = actual; actual = ARG(0); VOLVER(1, viejo); }
API(ShowCursor) { static int cuenta; cuenta += ARG(0) ? 1 : -1; if (cuenta < 0) SDL_HideCursor(); else SDL_ShowCursor(); VOLVER(1, (uint32_t)cuenta); }
API(LoadCursorA) { VOLVER(2, 0x00030000u + (ARG(1) & 0xffff)); }
API(LoadIconA) { VOLVER(2, 0x00031000u + (ARG(1) & 0xfff)); }
API(LoadImageA) { VOLVER(6, 0x00032000u + (ARG(1) & 0xfff)); }
API(DestroyCursor) { VOLVER(1, 1); }
API(DestroyIcon) { VOLVER(1, 1); }
API(ReleaseCapture) { VOLVER(0, 1); }
API(SetCapture) { VOLVER(1, 0); }
API(GetCapture) { VOLVER(0, 0); }
API(InvalidateRect) { VOLVER(3, 1); }
API(ValidateRect) { VOLVER(2, 1); }
API(SetTimer) { VOLVER(4, ARG(1) ? ARG(1) : 1); }
API(KillTimer) { VOLVER(2, 1); }

API(GetSystemMetrics) {
  int w, h;
  escritorio(&w, &h);
  switch (ARG(0)) {
    case 0: case 16: case 78: VOLVER(1, w);  // SM_CXSCREEN, SM_CXFULLSCREEN, SM_CXVIRTUALSCREEN
    case 1: case 17: case 79: VOLVER(1, h);
    case 4: VOLVER(1, 19);                   // SM_CYCAPTION
    case 5: case 6: VOLVER(1, 1);            // SM_CXBORDER, SM_CYBORDER
    case 7: case 8: VOLVER(1, 3);            // SM_CXDLGFRAME
    case 11: case 12: case 13: case 14: VOLVER(1, 32); // iconos y cursor
    case 32: case 33: VOLVER(1, 4);          // SM_CXFRAME
    case 43: VOLVER(1, 3);                   // SM_CMOUSEBUTTONS
    case 49: case 50: VOLVER(1, 16);
    case 80: case 81: VOLVER(1, 1);          // SM_CMONITORS, SM_SAMEDISPLAYFORMAT
    default: VOLVER(1, 0);
  }
}
API(SystemParametersInfoA) {
  if (ARG(0) == 0x30 && ARG(2)) { int w, h; escritorio(&w, &h); a_rect(ARG(2), 0, 0, w, h); } // SPI_GETWORKAREA
  else if (ARG(2) && ARG(0) != 0x30) M32(ARG(2)) = 0;
  VOLVER(4, 1);
}
API(GetSysColor) {
  static const uint32_t col[] = {0xc8d0d4, 0x6d3a0a, 0x6a240a, 0x808080, 0xc8d0d4, 0xffffff, 0, 0, 0, 0xffffff};
  VOLVER(1, ARG(0) < 10 ? col[ARG(0)] : 0xc8d0d4);
}
API(GetSysColorBrush) { VOLVER(1, 0x00033000u + ARG(0)); }

// Teclado directo.
static int tecla_abajo(uint32_t vk) {
  const bool *k = SDL_GetKeyboardState(NULL);
  if (vk == 1 || vk == 2 || vk == 4)
    return (vk == 1 && (raton_botones & SDL_BUTTON_LMASK)) || (vk == 2 && (raton_botones & SDL_BUTTON_RMASK)) || (vk == 4 && (raton_botones & SDL_BUTTON_MMASK));
  if (vk == 0x10 || vk == 0xa0 || vk == 0xa1) return k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT];
  if (vk == 0x11 || vk == 0xa2 || vk == 0xa3) return k[SDL_SCANCODE_LCTRL] || k[SDL_SCANCODE_RCTRL];
  if (vk == 0x12 || vk == 0xa4 || vk == 0xa5) return k[SDL_SCANCODE_LALT] || k[SDL_SCANCODE_RALT];
  for (const Tecla *t = TECLAS; t->sdl; t++) if (t->vk == vk && k[t->sdl]) return 1;
  return 0;
}
API(GetAsyncKeyState) { VOLVER(1, tecla_abajo(ARG(0)) ? 0x8000u : 0); }
API(GetKeyState) { VOLVER(1, tecla_abajo(ARG(0)) ? 0xff80u : 0); }

// ---- cadenas ----
static uint32_t mensajes_registrados;
API(RegisterWindowMessageA) { VOLVER(1, 0xc800u + (mensajes_registrados++ & 0x3ff)); }
API(CharUpperA) {
  uint32_t p = ARG(0);
  if (p < 0x10000) VOLVER(1, (uint32_t)toupper((int)p));
  for (uint8_t *s = (uint8_t *)gstr(p); *s; s++) {
    if (((*s >= 0x81 && *s <= 0x9f) || (*s >= 0xe0 && *s <= 0xfc)) && s[1]) { s++; continue; }
    *s = (uint8_t)toupper(*s);
  }
  VOLVER(1, p);
}
API(CharToOemA) { if (ARG(0) != ARG(1)) strcpy(gstr(ARG(1)), gstr(ARG(0))); VOLVER(2, 1); }
API(OemToCharA) { if (ARG(0) != ARG(1)) strcpy(gstr(ARG(1)), gstr(ARG(0))); VOLVER(2, 1); }
uint32_t recurso(uint32_t nombre, uint32_t tipo, uint32_t *tam); // k32_varios.c
API(LoadStringA) {
  uint32_t id = ARG(1), buf = ARG(2), max = ARG(3), tam;
  uint32_t d = recurso(id / 16 + 1, 6, &tam); // RT_STRING
  if (!d || !max) VOLVER(4, 0);
  for (uint32_t i = 0; i < id % 16; i++) d += 2 + 2u * M16(d);
  uint32_t n = M16(d), k = 0;
  for (uint32_t i = 0; i < n && k + 1 < max; i++) {
    uint16_t u = M16(d + 2 + 2 * i);
    M8(buf + k++) = u < 0x80 ? (uint8_t)u : '?';
  }
  M8(buf + k) = 0;
  VOLVER(4, k);
}

// wsprintfA: cdecl con argumentos variables en la pila del invitado.
uint32_t formatear(const char *fmt, uint32_t args, char *out, size_t tam);
API(wsprintfA) {
  char out[1025];
  uint32_t n = formatear(gstr(ARG(1)), c->esp + 12, out, sizeof out);
  memcpy(gstr(ARG(0)), out, n + 1);
  VOLVER_CDECL(n);
}

API(MessageBoxA) {
  char t[2048], cap[256];
  sjis_a_utf8(ARG(1) ? gstr(ARG(1)) : "", t, sizeof t);
  sjis_a_utf8(ARG(2) ? gstr(ARG(2)) : "", cap, sizeof cap);
  fprintf(stderr, "[MessageBox] %s: %s\n", cap, t);
  VOLVER(4, 1); // IDOK
}

API(SetWindowsHookExA) { VOLVER(4, 0x00034000u + ARG(0)); }
API(UnhookWindowsHookEx) { VOLVER(1, 1); }
API(CallNextHookEx) { VOLVER(4, 0); }
API(TranslateAcceleratorA) { VOLVER(3, 0); }
API(LoadAcceleratorsA) { VOLVER(2, 0x00035000u); }
API(IsDialogMessageA) { VOLVER(2, 0); }
API(GetDC) { extern uint32_t dc_de_ventana(uint32_t); VOLVER(1, dc_de_ventana(ARG(0))); }
API(GetWindowDC) { extern uint32_t dc_de_ventana(uint32_t); VOLVER(1, dc_de_ventana(ARG(0))); }
API(ReleaseDC) { VOLVER(2, 1); }

const Exportada EXP_USER32[] = {
  {"PeekMessageA", PeekMessageA}, {"GetMessageA", GetMessageA}, {"TranslateMessage", TranslateMessage},
  {"DispatchMessageA", DispatchMessageA}, {"SendMessageA", SendMessageA}, {"PostMessageA", PostMessageA},
  {"PostThreadMessageA", PostThreadMessageA}, {"SendNotifyMessageA", SendNotifyMessageA},
  {"PostQuitMessage", PostQuitMessage}, {"GetMessageTime", GetMessageTime}, {"GetMessagePos", GetMessagePos},
  {"DefWindowProcA", DefWindowProcA}, {"RegisterClassExA", RegisterClassExA}, {"RegisterClassA", RegisterClassA},
  {"GetClassInfoA", GetClassInfoA}, {"UnregisterClassA", UnregisterClassA}, {"CreateWindowExA", CreateWindowExA},
  {"DestroyWindow", DestroyWindow}, {"ShowWindow", ShowWindow}, {"UpdateWindow", UpdateWindow}, {"IsWindow", IsWindow},
  {"IsWindowVisible", IsWindowVisible}, {"IsIconic", IsIconic}, {"IsZoomed", IsZoomed}, {"IsWindowEnabled", IsWindowEnabled},
  {"EnableWindow", EnableWindow}, {"GetParent", GetParent}, {"GetDesktopWindow", GetDesktopWindow},
  {"GetForegroundWindow", GetForegroundWindow}, {"GetActiveWindow", GetActiveWindow}, {"GetFocus", GetFocus},
  {"SetForegroundWindow", SetForegroundWindow}, {"SetActiveWindow", SetActiveWindow}, {"SetFocus", SetFocus},
  {"BringWindowToTop", BringWindowToTop}, {"GetLastActivePopup", GetLastActivePopup}, {"GetWindow", GetWindow},
  {"GetTopWindow", GetTopWindow}, {"FindWindowA", FindWindowA}, {"GetWindowRect", GetWindowRect},
  {"GetClientRect", GetClientRect}, {"AdjustWindowRectEx", AdjustWindowRectEx}, {"SetWindowPos", SetWindowPos},
  {"MoveWindow", MoveWindow}, {"GetWindowLongA", GetWindowLongA}, {"SetWindowLongA", SetWindowLongA},
  {"SetWindowTextA", SetWindowTextA}, {"GetWindowTextA", GetWindowTextA}, {"GetWindowTextLengthA", GetWindowTextLengthA},
  {"ClientToScreen", ClientToScreen}, {"ScreenToClient", ScreenToClient}, {"GetCursorPos", GetCursorPos},
  {"SetCursorPos", SetCursorPos}, {"SetCursor", SetCursor}, {"ShowCursor", ShowCursor}, {"LoadCursorA", LoadCursorA},
  {"LoadIconA", LoadIconA}, {"LoadImageA", LoadImageA}, {"DestroyCursor", DestroyCursor}, {"DestroyIcon", DestroyIcon},
  {"ReleaseCapture", ReleaseCapture}, {"SetCapture", SetCapture}, {"GetCapture", GetCapture},
  {"InvalidateRect", InvalidateRect}, {"ValidateRect", ValidateRect}, {"SetTimer", SetTimer}, {"KillTimer", KillTimer},
  {"GetSystemMetrics", GetSystemMetrics}, {"SystemParametersInfoA", SystemParametersInfoA}, {"GetSysColor", GetSysColor},
  {"GetSysColorBrush", GetSysColorBrush}, {"GetAsyncKeyState", GetAsyncKeyState}, {"GetKeyState", GetKeyState},
  {"RegisterWindowMessageA", RegisterWindowMessageA}, {"CharUpperA", CharUpperA}, {"CharToOemA", CharToOemA},
  {"OemToCharA", OemToCharA}, {"LoadStringA", LoadStringA}, {"wsprintfA", wsprintfA}, {"MessageBoxA", MessageBoxA},
  {"SetWindowsHookExA", SetWindowsHookExA}, {"UnhookWindowsHookEx", UnhookWindowsHookEx},
  {"CallNextHookEx", CallNextHookEx}, {"TranslateAcceleratorA", TranslateAcceleratorA},
  {"LoadAcceleratorsA", LoadAcceleratorsA}, {"IsDialogMessageA", IsDialogMessageA}, {"GetDC", GetDC},
  {"GetWindowDC", GetWindowDC}, {"ReleaseDC", ReleaseDC},
  {0, 0},
};

void mostrar_ventana(SDL_Window *w) { if (!getenv("HINO_OCULTA")) SDL_ShowWindow(w); }
int forzar_ventana(void) { return getenv("HINO_OCULTA") || getenv("HINO_VENTANA"); }
