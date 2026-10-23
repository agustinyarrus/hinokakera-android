// DINPUT8: teclado (estado de 256 bytes con códigos DIK), mouse y mandos de SDL como joysticks de DirectInput.
// teclas_virtuales[] lo escriben los controles táctiles (Android): se suma al teclado real.
#include "win32.h"
#include "teclas.h"
#include "../d3d/com.h"
#include <SDL3/SDL.h>
#include <stdlib.h>

uint8_t teclas_virtuales[256];

typedef enum { D_TECLADO = 1, D_MOUSE, D_JOY } TipoDisp;
typedef struct {
  TipoDisp tipo;
  int refs;
  uint32_t tam_datos;
  SDL_JoystickID id;
  SDL_Gamepad *pad;
  int32_t min[8], max[8], zona[8];
  float mx, my;
  uint32_t obj;
} DispEntrada;

static const uint8_t GUID_TECLADO[16] = {0x61, 0x2b, 0x1d, 0x6f, 0xa0, 0xd5, 0xcf, 0x11, 0xbf, 0xc7, 0x44, 0x45, 0x53, 0x54, 0, 0};
static const uint8_t GUID_MOUSE[16] = {0x60, 0x2b, 0x1d, 0x6f, 0xa0, 0xd5, 0xcf, 0x11, 0xbf, 0xc7, 0x44, 0x45, 0x53, 0x54, 0, 0};
static void guid_mando(uint8_t g[16], SDL_JoystickID id) { memset(g, 0, 16); memcpy(g, "HINOJOY", 7); memcpy(g + 8, &id, 4); }

static uint32_t obj_disp_entrada(DispEntrada *d);

// ---------------- IDirectInput8A ----------------
static int refs_di = 1;
API(DI_QueryInterface) { M32(ARG(2)) = ARG(0); refs_di++; VOLVER(3, 0); }
API(DI_AddRef) { VOLVER(1, (uint32_t)++refs_di); }
API(DI_Release) { VOLVER(1, refs_di > 1 ? (uint32_t)--refs_di : 1); }

API(DI_CreateDevice) {
  const uint8_t *g = MEM + ARG(1);
  DispEntrada *d = calloc(1, sizeof *d);
  d->refs = 1;
  for (int i = 0; i < 8; i++) { d->min[i] = 0; d->max[i] = 65535; }
  if (!memcmp(g, GUID_TECLADO, 16)) d->tipo = D_TECLADO;
  else if (!memcmp(g, GUID_MOUSE, 16)) d->tipo = D_MOUSE;
  else if (!memcmp(g, "HINOJOY", 7)) {
    d->tipo = D_JOY;
    memcpy(&d->id, g + 8, 4);
    d->pad = SDL_OpenGamepad(d->id);
    if (!d->pad) { free(d); M32(ARG(2)) = 0; VOLVER(4, 0x80040154u); } // DIERR_DEVICENOTREG
  } else { free(d); M32(ARG(2)) = 0; VOLVER(4, 0x80040154u); }
  M32(ARG(2)) = obj_disp_entrada(d);
  VOLVER(4, 0);
}

static void llenar_instancia(uint32_t p, const uint8_t guid[16], uint32_t tipo, const char *nombre) {
  memset(MEM + p, 0, 580);
  M32(p) = 580;
  memcpy(MEM + p + 4, guid, 16);
  memcpy(MEM + p + 20, guid, 16);
  M32(p + 36) = tipo;
  SDL_strlcpy(gstr(p + 40), nombre, 260);
  SDL_strlcpy(gstr(p + 300), nombre, 260);
}

API(DI_EnumDevices) {
  uint32_t tipo = ARG(1), cb = ARG(2), ref = ARG(3);
  if (tipo != 0 && tipo != 4 && (tipo & 0xff) != 0x14 && (tipo & 0xff) != 0x15) VOLVER(5, 0);
  int n = 0;
  SDL_JoystickID *ids = SDL_GetGamepads(&n);
  uint32_t ins = heap_alloc(0, 580, 1);
  for (int i = 0; i < n; i++) {
    uint8_t g[16];
    guid_mando(g, ids[i]);
    const char *nombre = SDL_GetGamepadNameForID(ids[i]);
    llenar_instancia(ins, g, 0x00010215u, nombre ? nombre : "Gamepad"); // DI8DEVTYPE_GAMEPAD
    uint32_t args[2] = {ins, ref};
    if (llamar_invitado(c, cb, 2, args, 0) == 0) break; // DIENUM_STOP
  }
  heap_free(0, ins);
  SDL_free(ids);
  fprintf(stderr, "DirectInput: %d mando(s)\n", n);
  VOLVER(5, 0);
}
API(DI_GetDeviceStatus) { VOLVER(2, 0); }
static const Metodo M_DI[] = {
  {"QueryInterface", DI_QueryInterface}, {"AddRef", DI_AddRef}, {"Release", DI_Release}, {"CreateDevice", DI_CreateDevice},
  {"EnumDevices", DI_EnumDevices}, {"GetDeviceStatus", DI_GetDeviceStatus}, {"RunControlPanel", NULL}, {"Initialize", NULL},
  {"FindDevice", NULL}, {"EnumDevicesBySemantics", NULL}, {"ConfigureDevices", NULL},
};

API(DirectInput8Create) {
  static uint32_t obj;
  if (!obj) obj = com_objeto(com_vtabla("IDirectInput8A", M_DI, 11), NULL);
  M32(ARG(3)) = obj;
  VOLVER(5, 0);
}

// ---------------- IDirectInputDevice8A ----------------
#define DEV ((DispEntrada *)com_host(ARG(0)))
API(DD_QueryInterface) { M32(ARG(2)) = ARG(0); DEV->refs++; VOLVER(3, 0); }
API(DD_AddRef) { VOLVER(1, (uint32_t)++DEV->refs); }
API(DD_Release) {
  DispEntrada *d = DEV;
  if (--d->refs > 0) VOLVER(1, (uint32_t)d->refs);
  if (d->pad) SDL_CloseGamepad(d->pad);
  com_borrar(d->obj);
  free(d);
  VOLVER(1, 0);
}
API(DD_GetCapabilities) {
  DispEntrada *d = DEV;
  uint32_t p = ARG(1);
  memset(MEM + p + 4, 0, 0x28);
  M32(p + 4) = 1; // DIDC_ATTACHED
  if (d->tipo == D_TECLADO) { M32(p + 8) = 0x413; M32(p + 16) = 128; }
  else if (d->tipo == D_MOUSE) { M32(p + 8) = 0x112; M32(p + 12) = 3; M32(p + 16) = 3; }
  else { M32(p + 8) = 0x00010215u; M32(p + 12) = 4; M32(p + 16) = 12; M32(p + 20) = 1; }
  VOLVER(2, 0);
}
// Ejes del mando: X, Y, Rx, Ry (offsets de DIJOYSTATE: 0, 4, 12, 16).
static const uint32_t OFS_EJE[4] = {0, 4, 12, 16};
static const uint8_t GUID_EJE[4] = {0x80, 0x81, 0x83, 0x84}; // primer byte de GUID_XAxis, YAxis, RxAxis, RyAxis
API(DD_EnumObjects) {
  DispEntrada *d = DEV;
  uint32_t cb = ARG(1), ref = ARG(2), filtro = ARG(3);
  if (d->tipo != D_JOY || !(filtro & 3) && filtro != 0) VOLVER(4, 0);
  uint32_t o = heap_alloc(0, 316, 1);
  for (uint32_t i = 0; i < 4; i++) {
    memset(MEM + o, 0, 316);
    M32(o) = 316;
    M8(o + 4) = GUID_EJE[i]; M8(o + 5) = 0x72; M8(o + 6) = 0x6e; M8(o + 7) = 0xa3; // GUID_*Axis {A36D02E0-...}
    M32(o + 20) = OFS_EJE[i];
    M32(o + 24) = 2u | (i << 8); // DIDFT_ABSAXIS | instancia
    snprintf(gstr(o + 32), 260, "Eje %u", i);
    uint32_t args[2] = {o, ref};
    if (llamar_invitado(c, cb, 2, args, 0) == 0) break;
  }
  heap_free(0, o);
  VOLVER(4, 0);
}
static int eje_de(uint32_t obj, uint32_t como) {
  if (como == 0) return -1;                  // DIPH_DEVICE: todos
  if (como == 1) { for (int i = 0; i < 4; i++) if (OFS_EJE[i] == obj) return i; return -2; } // DIPH_BYOFFSET
  return (int)((obj >> 8) & 0xffff) < 4 ? (int)((obj >> 8) & 0xffff) : -2; // DIPH_BYID
}
API(DD_SetProperty) {
  DispEntrada *d = DEV;
  uint32_t prop = ARG(1), h = ARG(2);
  int e = eje_de(M32(h + 8), M32(h + 12));
  for (int i = 0; i < 8; i++) {
    if (e == -2 || (e >= 0 && e != i)) continue;
    if (prop == 4) { d->min[i] = (int32_t)M32(h + 16); d->max[i] = (int32_t)M32(h + 20); }
    if (prop == 5) d->zona[i] = (int32_t)M32(h + 16);
  }
  VOLVER(3, 0);
}
API(DD_GetProperty) {
  DispEntrada *d = DEV;
  uint32_t prop = ARG(1), h = ARG(2);
  int e = eje_de(M32(h + 8), M32(h + 12));
  if (e < 0) e = 0;
  if (prop == 4) { M32(h + 16) = (uint32_t)d->min[e]; M32(h + 20) = (uint32_t)d->max[e]; }
  else if (prop == 5) M32(h + 16) = (uint32_t)d->zona[e];
  else M32(h + 16) = 0;
  VOLVER(3, 0);
}
API(DD_Acquire) { VOLVER(1, 0); }
API(DD_Unacquire) { VOLVER(1, 0); }
API(DD_Poll) { VOLVER(1, DEV->tipo == D_JOY ? 0 : 1); } // DI_NOEFFECT para teclado y mouse

static int32_t escalar(DispEntrada *d, int i, int16_t v) {
  int64_t r = (int64_t)(v + 32768) * (d->max[i] - d->min[i]) / 65535 + d->min[i];
  int64_t centro = (d->max[i] + d->min[i]) / 2, zona = (int64_t)d->zona[i] * (d->max[i] - d->min[i]) / 20000;
  if (r - centro < zona && centro - r < zona) r = centro;
  return (int32_t)r;
}

API(DD_GetDeviceState) {
  DispEntrada *d = DEV;
  uint32_t n = ARG(1), p = ARG(2);
  memset(MEM + p, 0, n);
  if (d->tipo == D_TECLADO) {
    const bool *k = SDL_GetKeyboardState(NULL);
    for (const Tecla *t = TECLAS; t->sdl; t++) if (k[t->sdl] && t->dik < n) M8(p + t->dik) = 0x80;
    for (uint32_t i = 0; i < n && i < 256; i++) if (teclas_virtuales[i]) M8(p + i) = 0x80;
  } else if (d->tipo == D_MOUSE) {
    // El mouse de user32.c (en coordenadas del juego): así el toque de Android también es el mouse.
    extern int raton_dx, raton_dy;
    extern uint32_t raton_botones;
    if (n >= 16) {
      M32(p) = (uint32_t)raton_dx; M32(p + 4) = (uint32_t)raton_dy;
      M8(p + 12) = raton_botones & SDL_BUTTON_LMASK ? 0x80 : 0; M8(p + 13) = raton_botones & SDL_BUTTON_RMASK ? 0x80 : 0;
      M8(p + 14) = raton_botones & SDL_BUTTON_MMASK ? 0x80 : 0;
    }
    raton_dx = raton_dy = 0;
  } else if (d->pad && n >= 80) {
    SDL_Gamepad *g = d->pad;
    static const SDL_GamepadAxis EJES[4] = {SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY, SDL_GAMEPAD_AXIS_RIGHTX, SDL_GAMEPAD_AXIS_RIGHTY};
    for (int i = 0; i < 4; i++) M32(p + OFS_EJE[i]) = (uint32_t)escalar(d, i, SDL_GetGamepadAxis(g, EJES[i]));
    M32(p + 8) = (uint32_t)((d->max[2] + d->min[2]) / 2); // Z centrado
    M32(p + 20) = (uint32_t)((d->max[5] + d->min[5]) / 2);
    int ar = SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_UP), ab = SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
    int iz = SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_LEFT), de = SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
    int ang = ar && de ? 4500 : de && ab ? 13500 : ab && iz ? 22500 : iz && ar ? 31500 : ar ? 0 : de ? 9000 : ab ? 18000 : iz ? 27000 : -1;
    M32(p + 32) = (uint32_t)ang;
    M32(p + 36) = M32(p + 40) = M32(p + 44) = 0xffffffffu;
    static const SDL_GamepadButton BOTONES[12] = {
      SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH,
      SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_GAMEPAD_BUTTON_BACK, SDL_GAMEPAD_BUTTON_START,
      SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK, SDL_GAMEPAD_BUTTON_INVALID, SDL_GAMEPAD_BUTTON_INVALID};
    for (int i = 0; i < 10; i++) M8(p + 48 + i) = SDL_GetGamepadButton(g, BOTONES[i]) ? 0x80 : 0;
    M8(p + 58) = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 16000 ? 0x80 : 0;
    M8(p + 59) = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 16000 ? 0x80 : 0;
  }
  VOLVER(3, 0);
}
// Mouse con buffer: el modo historia (novela visual) lee los clics con GetDeviceData. Los eventos los pone
// raton_juego() de user32.c (mouse de verdad o toque sobre la imagen); el teclado y los mandos van sin buffer.
#define COLA_RATON 64
static uint32_t cola_raton[COLA_RATON][4]; // dwOfs, dwData, dwTimeStamp, dwSequence
static int raton_ini, raton_n;
void raton_evento(uint32_t ofs, uint32_t dato) {
  static uint32_t secuencia;
  if (raton_n == COLA_RATON) { raton_ini = (raton_ini + 1) % COLA_RATON; raton_n--; } // lleno: se pierde el más viejo
  uint32_t *e = cola_raton[(raton_ini + raton_n++) % COLA_RATON];
  e[0] = ofs; e[1] = dato; e[2] = (uint32_t)SDL_GetTicks(); e[3] = ++secuencia;
}
API(DD_GetDeviceData) {
  uint32_t tam = ARG(1), dst = ARG(2), pn = ARG(3), n = 0, max = pn ? M32(pn) : 0;
  if (DEV->tipo == D_MOUSE) {
    for (; n < max && (int)n < raton_n; n++) {
      if (!dst) continue;
      uint32_t p = dst + n * tam, *e = cola_raton[(raton_ini + (int)n) % COLA_RATON];
      memset(MEM + p, 0, tam);
      for (int i = 0; i < 4 && 4u * i + 4 <= tam; i++) M32(p + 4u * i) = e[i];
    }
    if (!(ARG(4) & 1)) { raton_ini = (raton_ini + (int)n) % COLA_RATON; raton_n -= (int)n; } // DIGDD_PEEK no los saca
  }
  if (pn) M32(pn) = n;
  VOLVER(5, 0);
}
API(DD_SetDataFormat) { DEV->tam_datos = M32(ARG(1) + 12); VOLVER(2, 0); }
API(DD_SetEventNotification) { VOLVER(2, 0); }
API(DD_SetCooperativeLevel) { VOLVER(3, 0); }
API(DD_GetDeviceInfo) {
  DispEntrada *d = DEV;
  uint8_t g[16];
  if (d->tipo == D_JOY) guid_mando(g, d->id);
  llenar_instancia(ARG(1), d->tipo == D_TECLADO ? GUID_TECLADO : d->tipo == D_MOUSE ? GUID_MOUSE : g,
                   d->tipo == D_TECLADO ? 0x413 : d->tipo == D_MOUSE ? 0x112 : 0x00010215u, d->tipo == D_JOY ? "Gamepad" : "Sistema");
  VOLVER(2, 0);
}
static const Metodo M_DD[] = {
  {"QueryInterface", DD_QueryInterface}, {"AddRef", DD_AddRef}, {"Release", DD_Release},
  {"GetCapabilities", DD_GetCapabilities}, {"EnumObjects", DD_EnumObjects}, {"GetProperty", DD_GetProperty},
  {"SetProperty", DD_SetProperty}, {"Acquire", DD_Acquire}, {"Unacquire", DD_Unacquire},
  {"GetDeviceState", DD_GetDeviceState}, {"GetDeviceData", DD_GetDeviceData}, {"SetDataFormat", DD_SetDataFormat},
  {"SetEventNotification", DD_SetEventNotification}, {"SetCooperativeLevel", DD_SetCooperativeLevel},
  {"GetObjectInfo", NULL}, {"GetDeviceInfo", DD_GetDeviceInfo}, {"RunControlPanel", NULL}, {"Initialize", NULL},
  {"CreateEffect", NULL}, {"EnumEffects", NULL}, {"GetEffectInfo", NULL}, {"GetForceFeedbackState", NULL},
  {"SendForceFeedbackCommand", NULL}, {"EnumCreatedEffectObjects", NULL}, {"Escape", NULL}, {"Poll", DD_Poll},
  {"SendDeviceData", NULL}, {"EnumEffectsInFile", NULL}, {"WriteEffectToFile", NULL}, {"BuildActionMap", NULL},
  {"SetActionMap", NULL}, {"GetImageInfo", NULL},
};
static uint32_t obj_disp_entrada(DispEntrada *d) {
  static uint32_t vt;
  if (!vt) vt = com_vtabla("IDirectInputDevice8A", M_DD, 32);
  return d->obj = com_objeto(vt, d);
}

const Exportada EXP_DINPUT8[] = {{"DirectInput8Create", DirectInput8Create}, {0, 0}};

// Guion de teclas para pruebas: HINO_TECLAS="cuadro:dik:1|0,..." (dik en hex). Se aplica en cada Present,
// así el mismo guion lleva al port y al original (con el espía de dinput8) por las mismas pantallas.
void guion_teclas(uint32_t cuadro) {
  const char *g = getenv("HINO_TECLAS");
  if (!g) return;
  for (const char *p = g; p && *p; ) {
    unsigned n, dik, v;
    if (sscanf(p, "%u:%x:%u", &n, &dik, &v) == 3 && n == cuadro && dik < 256) teclas_virtuales[dik] = v ? 1 : 0;
    p = strchr(p, ',');
    if (p) p++;
  }
}
