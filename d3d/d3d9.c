// D3D9.DLL: IDirect3D9 y el estado de IDirect3DDevice9. Dibujo en dibujo.c; recursos en recursos.c.
#include "d3d9_int.h"
#include "../win32/user32.h"
#include <stdlib.h>

Dispositivo *disp;
static uint32_t obj_d3d9;

void recurso_addref(Recurso *r) { r->refs++; }

// ---------------- IDirect3D9 ----------------
static uint32_t refs_d3d9 = 1;
API(D9_QueryInterface) {
  uint32_t iid = M32(ARG(1));
  if (iid == 0x81bdcbcau || iid == 0) { M32(ARG(2)) = ARG(0); refs_d3d9++; VOLVER(3, 0); }
  M32(ARG(2)) = 0;
  VOLVER(3, E_NOINTERFACE);
}
API(D9_AddRef) { VOLVER(1, ++refs_d3d9); }
API(D9_Release) { VOLVER(1, refs_d3d9 > 1 ? --refs_d3d9 : 1); }
API(D9_GetAdapterCount) { VOLVER(1, 1); }
API(D9_GetAdapterIdentifier) {
  uint32_t p = ARG(3);
  memset(MEM + p, 0, 1100);
  strcpy(gstr(p), "nv4_disp.dll");
  strcpy(gstr(p + 512), "NVIDIA GeForce GTX 260");
  strcpy(gstr(p + 1024), "\\\\.\\DISPLAY1");
  M32(p + 1056) = 0x10de; M32(p + 1060) = 0x05e2; // VendorId, DeviceId
  M32(p + 1096) = 1;                              // WHQLLevel
  VOLVER(4, 0);
}
static const uint16_t MODOS[][2] = {{640, 480}, {800, 600}, {1024, 768}, {1280, 720}, {1280, 960}, {1280, 1024}, {1600, 900}, {1920, 1080}};
#define N_MODOS (sizeof MODOS / sizeof MODOS[0])
API(D9_GetAdapterModeCount) { VOLVER(3, ARG(2) == 22 || ARG(2) == 23 ? N_MODOS : 0); }
API(D9_EnumAdapterModes) {
  uint32_t i = ARG(3), p = ARG(4);
  if (i >= N_MODOS) VOLVER(5, D3DERR_INVALIDCALL);
  M32(p) = MODOS[i][0]; M32(p + 4) = MODOS[i][1]; M32(p + 8) = 60; M32(p + 12) = ARG(2);
  VOLVER(5, 0);
}
static void modo_escritorio(uint32_t p) {
  const SDL_DisplayMode *m = SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay());
  M32(p) = m ? (uint32_t)m->w : 1920; M32(p + 4) = m ? (uint32_t)m->h : 1080;
  M32(p + 8) = m && m->refresh_rate > 0 ? (uint32_t)m->refresh_rate : 60; M32(p + 12) = 22; // X8R8G8B8
}
API(D9_GetAdapterDisplayMode) { modo_escritorio(ARG(2)); VOLVER(3, 0); }
API(D9_CheckDeviceType) { VOLVER(6, 0); }
static int formato_soportado(uint32_t f) {
  switch (f) {
    case 20: case 21: case 22: case 23: case 24: case 25: case 26: case 28: case 50: case 51: // color
    case 70: case 71: case 75: case 77: case 80:                                              // profundidad
    case 101: case 102:                                                                       // índices
      return 1;
    default: return 0;
  }
}
API(D9_CheckDeviceFormat) { VOLVER(7, formato_soportado(ARG(6)) ? 0 : D3DERR_NOTAVAILABLE); }
API(D9_CheckDeviceMultiSampleType) {
  if (ARG(6)) M32(ARG(6)) = 1;
  VOLVER(7, ARG(5) == 0 ? 0 : D3DERR_NOTAVAILABLE);
}
API(D9_CheckDepthStencilMatch) { VOLVER(6, 0); }
API(D9_CheckDeviceFormatConversion) { VOLVER(5, 0); }

// Caps de una placa de 2010 con vs/ps 3.0: el juego elige así el mismo camino que en la PC del usuario.
static void llenar_caps(uint32_t p) {
  memset(MEM + p, 0, 304);
  uint32_t v[][2] = {
    {0, 1}, {8, 0x00020000}, {12, 0x60020000}, {16, 0x3a0}, {20, 0x8000000f}, {24, 1}, {28, 0x001bbef0},
    {32, 0x000ef2f2}, {36, 0x0f736191}, {40, 0xff}, {44, 0x3fff}, {48, 0x3fff}, {52, 0xff}, {56, 0x00084208},
    {60, 0x0001ec45}, {64, 0x03030700}, {68, 0x03030700}, {72, 0x03030300}, {76, 0x3f}, {80, 0x3f}, {84, 0x1f},
    {88, 8192}, {92, 8192}, {96, 2048}, {100, 8192}, {104, 8192}, {108, 16}, {136, 0x1ff}, {140, 0x00180008},
    {144, 0x03ffffff}, {148, 8}, {152, 8}, {156, 0x17b}, {160, 8}, {164, 8}, {168, 4}, {180, 0x00ffffff},
    {184, 0x00ffffff}, {188, 16}, {192, 255}, {196, 0xfffe0300}, {200, 256}, {204, 0xffff0300}, {212, 0x51},
    {232, 1}, {236, 0x30f}, {240, 4}, {244, 0x03000300}, {248, 1}, {252, 24}, {256, 32}, {260, 4}, {264, 0x1f},
    {268, 24}, {272, 32}, {276, 4}, {280, 512}, {284, 0x03000300}, {288, 65535}, {292, 65535}, {296, 4096}, {300, 4096},
  };
  for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) M32(p + v[i][0]) = v[i][1];
  MF32(p + 112) = 1e10f; MF32(p + 116) = -1e8f; MF32(p + 120) = -1e8f; MF32(p + 124) = 1e8f; MF32(p + 128) = 1e8f;
  MF32(p + 176) = 8192.0f; MF32(p + 208) = 65504.0f;
}
API(D9_GetDeviceCaps) { llenar_caps(ARG(3)); VOLVER(4, 0); }
API(D9_GetAdapterMonitor) { VOLVER(2, 0x00070001u); }

static void estado_inicial(Dispositivo *d);
static uint32_t obj_disp(Dispositivo *d);
Superficie *backbuffer_nuevo(uint32_t w, uint32_t h);

static void leer_pp(Dispositivo *d, uint32_t p) {
  for (int i = 0; i < 14; i++) d->pp[i] = M32(p + 4u * (uint32_t)i);
  Ventana *v = ventana(d->pp[7] ? d->pp[7] : d->hwnd);
  if (!d->pp[0]) d->pp[0] = v ? (uint32_t)v->cw : 640;
  if (!d->pp[1]) d->pp[1] = v ? (uint32_t)v->ch : 480;
  if (d->pp[2] == 0) d->pp[2] = 22;
  for (int i = 0; i < 14; i++) M32(p + 4u * (uint32_t)i) = d->pp[i]; // D3D completa los ceros
}

// En Android siempre es pantalla completa, diga lo que diga config.dat.
static int en_ventana(Dispositivo *d) {
#ifdef SDL_PLATFORM_ANDROID
  (void)d;
  return 0;
#else
  return d->pp[8] || forzar_ventana();
#endif
}

static void configurar_ventana(Dispositivo *d) {
  // Ventana del tamaño del backbuffer; en "pantalla completa" se usa el escritorio y se escala al presentar.
  int ventana = en_ventana(d);
  SDL_SetWindowFullscreen(d->ventana, !ventana);
  if (ventana) SDL_SetWindowSize(d->ventana, (int)d->pp[0], (int)d->pp[1]);
  mostrar_ventana(d->ventana);
}

API(D9_CreateDevice) {
  uint32_t hwnd = ARG(3), fl = ARG(4), pp = ARG(5), salida = ARG(6);
  Ventana *v = ventana(M32(pp + 28) ? M32(pp + 28) : hwnd);
  if (!v || !v->sdl) { fprintf(stderr, "CreateDevice: ventana %08x inválida\n", hwnd); VOLVER(7, D3DERR_INVALIDCALL); }
  Dispositivo *d = calloc(1, sizeof *d);
  d->hwnd = hwnd; d->comportamiento = fl; d->ventana = v->sdl;
  leer_pp(d, pp);
  d->gl = SDL_GL_CreateContext(v->sdl);
  if (!d->gl) { fprintf(stderr, "SDL_GL_CreateContext: %s\n", SDL_GetError()); free(d); VOLVER(7, D3DERR_NOTAVAILABLE); }
  SDL_GL_MakeCurrent(v->sdl, d->gl);
  if (!gl_cargar()) VOLVER(7, D3DERR_NOTAVAILABLE);
  fprintf(stderr, "GL: %s / %s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER));
  SDL_GL_SetSwapInterval(d->pp[13] == 0x80000000u ? 0 : 1);
  disp = d;
  d->r.tipo = 0; d->r.refs = 1;
  ancho_iniciar(v->sdl, !en_ventana(d), d->pp[0], d->pp[1]);
  d->backbuffer = backbuffer_nuevo(d->pp[0], d->pp[1]);
  if (d->pp[9]) d->prof_auto = superficie_nueva(d->pp[0], d->pp[1], d->pp[10], 1, 0, (uint32_t)ancho_extra);
  d->rt = d->backbuffer; d->ds = d->prof_auto;
  recurso_addref(&d->rt->r);
  if (d->ds) recurso_addref(&d->ds->r);
  tactil_iniciar(v->sdl);
  sala_iniciar(v->sdl);
  dibujo_iniciar();
  estado_inicial(d);
  configurar_ventana(d);
  // Direct3D deja la FPU en precisión simple salvo D3DCREATE_FPU_PRESERVE: el juego cuenta con eso.
  if (!(fl & 2)) fldcw(c, c->fcw & ~0x300u);
  M32(salida) = obj_disp(d);
  fprintf(stderr, "CreateDevice: %ux%u formato %u, %s, profundidad %u, intervalo %08x, flags %08x\n",
          d->pp[0], d->pp[1], d->pp[2], d->pp[8] ? "ventana" : "pantalla completa", d->pp[9] ? d->pp[10] : 0, d->pp[13], fl);
  VOLVER(7, 0);
}

static const Metodo M_D3D9[] = {
  {"QueryInterface", D9_QueryInterface}, {"AddRef", D9_AddRef}, {"Release", D9_Release},
  {"RegisterSoftwareDevice", NULL}, {"GetAdapterCount", D9_GetAdapterCount}, {"GetAdapterIdentifier", D9_GetAdapterIdentifier},
  {"GetAdapterModeCount", D9_GetAdapterModeCount}, {"EnumAdapterModes", D9_EnumAdapterModes},
  {"GetAdapterDisplayMode", D9_GetAdapterDisplayMode}, {"CheckDeviceType", D9_CheckDeviceType},
  {"CheckDeviceFormat", D9_CheckDeviceFormat}, {"CheckDeviceMultiSampleType", D9_CheckDeviceMultiSampleType},
  {"CheckDepthStencilMatch", D9_CheckDepthStencilMatch}, {"CheckDeviceFormatConversion", D9_CheckDeviceFormatConversion},
  {"GetDeviceCaps", D9_GetDeviceCaps}, {"GetAdapterMonitor", D9_GetAdapterMonitor}, {"CreateDevice", D9_CreateDevice},
};

API(Direct3DCreate9) {
  if (!obj_d3d9) obj_d3d9 = com_objeto(com_vtabla("IDirect3D9", M_D3D9, 17), NULL);
  else refs_d3d9++;
  VOLVER(1, obj_d3d9);
}

// ---------------- IDirect3DDevice9: estado ----------------
static void identidad(float *m) { memset(m, 0, 64); m[0] = m[5] = m[10] = m[15] = 1; }

static void estado_inicial(Dispositivo *d) {
  static const uint32_t rs[][2] = {
    {8, 3}, {9, 2}, {14, 1}, {16, 1}, {19, 2}, {20, 1}, {22, 3}, {23, 4}, {25, 8}, {36, 0}, {57, 0}, {53, 1}, {54, 1},
    {55, 1}, {56, 8}, {58, 0xffffffffu}, {59, 0xffffffffu}, {60, 0xffffffffu}, {136, 1}, {137, 1}, {141, 1}, {142, 1},
    {145, 1}, {146, 2}, {161, 1}, {162, 0xffffffffu}, {165, 0xbaadcafeu}, {168, 0xf}, {171, 1}, {172, 3}, {173, 1},
    {186, 1}, {187, 1}, {188, 1}, {189, 8}, {190, 0xf}, {191, 0xf}, {192, 0xf}, {193, 0xffffffffu}, {207, 2}, {208, 1}, {209, 1},
  };
  for (size_t i = 0; i < sizeof rs / sizeof rs[0]; i++) d->rs[rs[i][0]] = rs[i][1];
  d->rs[7] = d->pp[9] ? 1 : 0;           // ZENABLE
  d->rs[37] = f_bits(1.0f);              // FOGEND
  d->rs[38] = f_bits(1.0f);              // FOGDENSITY
  d->rs[154] = d->rs[155] = f_bits(1.0f);
  d->rs[158] = f_bits(1.0f);
  d->rs[166] = f_bits(64.0f);
  for (int i = 0; i < 8; i++) {
    uint32_t *t = d->tss[i];
    t[1] = i ? 1 : 4; t[2] = 2; t[3] = 1; t[4] = i ? 1 : 2; t[5] = 2; t[6] = 1;
    t[11] = (uint32_t)i; t[26] = t[27] = t[28] = 1;
  }
  for (int i = 0; i < 16; i++) {
    uint32_t *s = d->ss[i];
    s[1] = s[2] = s[3] = 1; s[5] = s[6] = 1; s[7] = 0; s[10] = 1;
  }
  for (int i = 0; i < 260; i++) identidad(d->mat[i]);
  d->vp[0] = d->vp[1] = 0; d->vp[2] = d->pp[0]; d->vp[3] = d->pp[1]; d->vp[4] = f_bits(0); d->vp[5] = f_bits(1);
  d->tijera[2] = d->pp[0]; d->tijera[3] = d->pp[1];
}

#define THIS ((void)ARG(0))
API(D_QueryInterface) { M32(ARG(2)) = 0; VOLVER(3, E_NOINTERFACE); }
API(D_AddRef) { VOLVER(1, ++disp->r.refs); }
API(D_Release) { VOLVER(1, disp->r.refs > 1 ? --disp->r.refs : 1); } // el device vive hasta que el proceso termina
API(D_TestCooperativeLevel) { VOLVER(1, 0); }
API(D_GetAvailableTextureMem) { VOLVER(1, 512u << 20); }
API(D_EvictManagedResources) { VOLVER(1, 0); }
API(D_GetDirect3D) { refs_d3d9++; M32(ARG(1)) = obj_d3d9; VOLVER(2, 0); }
API(D_GetDeviceCaps) { llenar_caps(ARG(1)); VOLVER(2, 0); }
API(D_GetDisplayMode) {
  uint32_t p = ARG(2);
  if (disp->pp[8]) modo_escritorio(p);
  else { M32(p) = disp->pp[0]; M32(p + 4) = disp->pp[1]; M32(p + 8) = 60; M32(p + 12) = disp->pp[2]; }
  VOLVER(3, 0);
}
API(D_GetCreationParameters) {
  uint32_t p = ARG(1);
  M32(p) = 0; M32(p + 4) = 1; M32(p + 8) = disp->hwnd; M32(p + 12) = disp->comportamiento;
  VOLVER(2, 0);
}
API(D_SetCursorProperties) { VOLVER(4, 0); }
API(D_SetCursorPosition) { VOLVER(4, 0); }
API(D_ShowCursor) { VOLVER(2, 0); }
API(D_Reset) {
  Dispositivo *d = disp;
  leer_pp(d, ARG(1));
  ancho_iniciar(d->ventana, !en_ventana(d), d->pp[0], d->pp[1]);
  superficie_borrar(d->backbuffer);
  d->backbuffer = backbuffer_nuevo(d->pp[0], d->pp[1]);
  if (d->prof_auto) superficie_borrar(d->prof_auto);
  d->prof_auto = d->pp[9] ? superficie_nueva(d->pp[0], d->pp[1], d->pp[10], 1, 0, (uint32_t)ancho_extra) : NULL;
  d->rt = d->backbuffer; d->ds = d->prof_auto;
  recurso_addref(&d->rt->r);
  if (d->ds) recurso_addref(&d->ds->r);
  estado_inicial(d);
  configurar_ventana(d);
  SDL_GL_SetSwapInterval(d->pp[13] == 0x80000000u ? 0 : 1);
  fprintf(stderr, "Reset: %ux%u %s\n", d->pp[0], d->pp[1], d->pp[8] ? "ventana" : "pantalla completa");
  VOLVER(2, 0);
}
API(D_Present) { presentar(); VOLVER(5, 0); }
API(D_GetBackBuffer) {
  recurso_addref(&disp->backbuffer->r);
  M32(ARG(4)) = obj_superficie(disp->backbuffer);
  VOLVER(5, 0);
}
API(D_GetRasterStatus) { M32(ARG(2)) = 0; M32(ARG(2) + 4) = 0; VOLVER(3, 0); }
API(D_SetDialogBoxMode) { VOLVER(2, 0); }
API(D_SetGammaRamp) { VOLVER(4, 0); }
API(D_GetGammaRamp) {
  for (uint32_t i = 0; i < 256; i++) for (int k = 0; k < 3; k++) M16(ARG(2) + 512u * (uint32_t)k + 2 * i) = (uint16_t)(i * 257);
  VOLVER(3, 0);
}

API(D_SetRenderTarget) {
  uint32_t i = ARG(1);
  Superficie *s = com_host(ARG(2));
  if (i != 0) { if (s) fprintf(stderr, "SetRenderTarget(%u) ignorado\n", i); VOLVER(3, 0); }
  if (!s) VOLVER(3, D3DERR_INVALIDCALL);
  if (registrando_dibujos()) fprintf(stderr, "SetRenderTarget %s:%ux%u\n", s == disp->backbuffer ? "bb" : "rt", s->w, s->h);
  recurso_addref(&s->r);
  recurso_release(&disp->rt->r);
  disp->rt = s;
  // D3D9 reinicia el viewport al tamaño del nuevo destino.
  disp->vp[0] = disp->vp[1] = 0; disp->vp[2] = s->w; disp->vp[3] = s->h; disp->vp[4] = f_bits(0); disp->vp[5] = f_bits(1);
  disp->tijera[0] = disp->tijera[1] = 0; disp->tijera[2] = s->w; disp->tijera[3] = s->h;
  VOLVER(3, 0);
}
API(D_GetRenderTarget) {
  if (ARG(1) != 0) { M32(ARG(2)) = 0; VOLVER(3, D3DERR_NOTAVAILABLE); }
  recurso_addref(&disp->rt->r);
  M32(ARG(2)) = obj_superficie(disp->rt);
  VOLVER(3, 0);
}
API(D_SetDepthStencilSurface) {
  Superficie *s = com_host(ARG(1));
  if (s) recurso_addref(&s->r);
  if (disp->ds) recurso_release(&disp->ds->r);
  disp->ds = s;
  VOLVER(2, 0);
}
API(D_GetDepthStencilSurface) {
  if (!disp->ds) { M32(ARG(1)) = 0; VOLVER(2, D3DERR_NOTAVAILABLE); }
  recurso_addref(&disp->ds->r);
  M32(ARG(1)) = obj_superficie(disp->ds);
  VOLVER(2, 0);
}
API(D_BeginScene) { disp->en_escena = 1; VOLVER(1, 0); }
API(D_EndScene) { disp->en_escena = 0; VOLVER(1, 0); }
API(D_Clear) { limpiar(ARG(1), ARG(2), ARG(3), ARG(4), MF32(c->esp + 24), ARG(6)); VOLVER(7, 0); }

static int indice_matriz(uint32_t t) { return t < 24 ? (int)t : t >= 256 && t < 260 ? (int)t : -1; }
API(D_SetTransform) {
  int i = indice_matriz(ARG(1));
  if (i >= 0) memcpy(disp->mat[i], MEM + ARG(2), 64);
  VOLVER(3, 0);
}
API(D_GetTransform) {
  int i = indice_matriz(ARG(1));
  if (i >= 0) memcpy(MEM + ARG(2), disp->mat[i], 64);
  VOLVER(3, 0);
}
API(D_MultiplyTransform) {
  int i = indice_matriz(ARG(1));
  if (i >= 0) {
    float r[16], *a = disp->mat[i], b[16];
    memcpy(b, MEM + ARG(2), 64);
    for (int f = 0; f < 4; f++) for (int k = 0; k < 4; k++) r[f * 4 + k] = a[f * 4] * b[k] + a[f * 4 + 1] * b[4 + k] + a[f * 4 + 2] * b[8 + k] + a[f * 4 + 3] * b[12 + k];
    memcpy(a, r, 64);
  }
  VOLVER(3, 0);
}
API(D_SetViewport) { memcpy(disp->vp, MEM + ARG(1), 24); VOLVER(2, 0); }
API(D_GetViewport) { memcpy(MEM + ARG(1), disp->vp, 24); VOLVER(2, 0); }
API(D_SetMaterial) { memcpy(&disp->material, MEM + ARG(1), 68); VOLVER(2, 0); }
API(D_GetMaterial) { memcpy(MEM + ARG(1), &disp->material, 68); VOLVER(2, 0); }
API(D_SetLight) { if (ARG(1) < 8) memcpy(&disp->luces[ARG(1)], MEM + ARG(2), 104); VOLVER(3, 0); }
API(D_GetLight) { if (ARG(1) < 8) memcpy(MEM + ARG(2), &disp->luces[ARG(1)], 104); VOLVER(3, 0); }
API(D_LightEnable) { if (ARG(1) < 8) disp->luz_on[ARG(1)] = ARG(2) != 0; VOLVER(3, 0); }
API(D_GetLightEnable) { M32(ARG(2)) = ARG(1) < 8 ? (uint32_t)disp->luz_on[ARG(1)] : 0; VOLVER(3, 0); }
API(D_SetClipPlane) { VOLVER(3, 0); }
// HINO_LOG_ESTADOS: informa cada combinación (estado, valor) la primera vez que aparece.
static void registrar_estado(const char *tipo, uint32_t a, uint32_t b, uint32_t v) {
  static int activo = -1;
  if (activo < 0) activo = getenv("HINO_LOG_ESTADOS") != NULL;
  if (!activo) return;
  static uint64_t vistos[4096];
  static int n;
  uint64_t k = ((uint64_t)(tipo[0] ^ (tipo[1] << 8)) << 48) ^ ((uint64_t)a << 40) ^ ((uint64_t)b << 32) ^ v;
  for (int i = 0; i < n; i++) if (vistos[i] == k) return;
  if (n < 4096) vistos[n++] = k;
  fprintf(stderr, "estado %s %u %u = %08x\n", tipo, a, b, v);
}
API(D_SetRenderState) { registrar_estado("RS", 0, ARG(1), ARG(2)); if (ARG(1) < 256) disp->rs[ARG(1)] = ARG(2); VOLVER(3, 0); }
API(D_GetRenderState) { M32(ARG(2)) = ARG(1) < 256 ? disp->rs[ARG(1)] : 0; VOLVER(3, 0); }
API(D_GetTexture) {
  Textura *t = ARG(1) < 16 ? disp->tex[ARG(1)] : NULL;
  if (t) recurso_addref(&t->r);
  M32(ARG(2)) = t ? t->r.obj : 0;
  VOLVER(3, 0);
}
API(D_SetTexture) {
  uint32_t i = ARG(1);
  if (i >= 16) VOLVER(3, 0); // muestreadores de vértices (D3DVERTEXTEXTURESAMPLER0+)
  Textura *t = com_host(ARG(2));
  if (t && t->r.tipo != R_TEXTURA) t = NULL;
  if (t) recurso_addref(&t->r);
  if (disp->tex[i]) recurso_release(&disp->tex[i]->r);
  disp->tex[i] = t;
  VOLVER(3, 0);
}
API(D_GetTextureStageState) { M32(ARG(3)) = ARG(1) < 8 && ARG(2) < 33 ? disp->tss[ARG(1)][ARG(2)] : 0; VOLVER(4, 0); }
API(D_SetTextureStageState) { registrar_estado("TSS", ARG(1), ARG(2), ARG(3)); if (ARG(1) < 8 && ARG(2) < 33) disp->tss[ARG(1)][ARG(2)] = ARG(3); VOLVER(4, 0); }
API(D_GetSamplerState) { M32(ARG(3)) = ARG(1) < 16 && ARG(2) < 14 ? disp->ss[ARG(1)][ARG(2)] : 0; VOLVER(4, 0); }
API(D_SetSamplerState) { registrar_estado("SS", ARG(1), ARG(2), ARG(3)); if (ARG(1) < 16 && ARG(2) < 14) disp->ss[ARG(1)][ARG(2)] = ARG(3); VOLVER(4, 0); }
API(D_ValidateDevice) { if (ARG(1)) M32(ARG(1)) = 1; VOLVER(2, 0); }
API(D_SetScissorRect) {
  uint32_t r = ARG(1);
  disp->tijera[0] = M32(r); disp->tijera[1] = M32(r + 4);
  disp->tijera[2] = M32(r + 8) - M32(r); disp->tijera[3] = M32(r + 12) - M32(r + 4);
  VOLVER(2, 0);
}
API(D_GetScissorRect) {
  uint32_t r = ARG(1);
  M32(r) = disp->tijera[0]; M32(r + 4) = disp->tijera[1];
  M32(r + 8) = disp->tijera[0] + disp->tijera[2]; M32(r + 12) = disp->tijera[1] + disp->tijera[3];
  VOLVER(2, 0);
}
API(D_SetSoftwareVertexProcessing) { VOLVER(2, 0); }
API(D_GetSoftwareVertexProcessing) { VOLVER(1, 0); }
API(D_SetNPatchMode) { VOLVER(2, 0); }

API(D_DrawPrimitive) { dibujar(0, ARG(1), ARG(2), ARG(3), 0, 0, 0, 0, 0); VOLVER(4, 0); }
API(D_DrawIndexedPrimitive) { dibujar(1, ARG(1), ARG(2), ARG(6), ARG(5), 0, 0, 0, 0); VOLVER(7, 0); }
API(D_DrawPrimitiveUP) { dibujar(0, ARG(1), 0, ARG(2), 0, ARG(3), ARG(4), 0, 0); VOLVER(5, 0); }
API(D_DrawIndexedPrimitiveUP) { dibujar(1, ARG(1), 0, ARG(4), 0, ARG(7), ARG(8), ARG(5), ARG(6)); VOLVER(9, 0); }

API(D_CreateVertexDeclaration) {
  Decl *d = calloc(1, sizeof *d);
  d->r.tipo = R_DECL; d->r.refs = 1;
  for (uint32_t p = ARG(1); d->n < 32 && M16(p) != 0xff; p += 8, d->n++) {
    Elemento *e = &d->e[d->n];
    e->stream = M16(p); e->offset = M16(p + 2); e->tipo = M8(p + 4); e->metodo = M8(p + 5); e->uso = M8(p + 6); e->indice = M8(p + 7);
  }
  M32(ARG(2)) = obj_decl(d);
  VOLVER(3, 0);
}
API(D_SetVertexDeclaration) {
  Decl *d = com_host(ARG(1));
  if (d) recurso_addref(&d->r);
  if (disp->decl) recurso_release(&disp->decl->r);
  disp->decl = d;
  if (d) disp->fvf = 0;
  VOLVER(2, 0);
}
API(D_GetVertexDeclaration) {
  if (disp->decl) recurso_addref(&disp->decl->r);
  M32(ARG(1)) = disp->decl ? disp->decl->r.obj : 0;
  VOLVER(2, 0);
}
API(D_SetFVF) {
  disp->fvf = ARG(1);
  if (disp->decl && ARG(1)) { recurso_release(&disp->decl->r); disp->decl = NULL; }
  VOLVER(2, 0);
}
API(D_GetFVF) { M32(ARG(1)) = disp->fvf; VOLVER(2, 0); }

API(D_CreateVertexShader) {
  // El "bytecode" es lo que devolvió nuestro D3DXAssembleShader: comentario con el texto fuente.
  uint32_t p = ARG(1);
  if (M32(p) >> 16 != 0xfffe || (M32(p + 4) & 0xffff) != 0xfffe || M32(p + 8) != 0x5458544cu) {
    fprintf(stderr, "CreateVertexShader: bytecode que no viene de nuestro ensamblador\n");
    VOLVER(3, D3DERR_INVALIDCALL);
  }
  ShaderV *s = vs_traducir(gstr(p + 12));
  if (!s) VOLVER(3, D3DERR_INVALIDCALL);
  s->fuente_va = p; s->fuente_tam = 4u * ((M32(p + 4) >> 16) + 3);
  M32(ARG(2)) = obj_vs(s);
  VOLVER(3, 0);
}
API(D_SetVertexShader) {
  ShaderV *s = com_host(ARG(1));
  if (s) recurso_addref(&s->r);
  if (disp->vs) recurso_release(&disp->vs->r);
  disp->vs = s;
  VOLVER(2, 0);
}
API(D_GetVertexShader) {
  if (disp->vs) recurso_addref(&disp->vs->r);
  M32(ARG(1)) = disp->vs ? disp->vs->r.obj : 0;
  VOLVER(2, 0);
}
API(D_SetVertexShaderConstantF) {
  uint32_t ini = ARG(1), n = ARG(3);
  if (ini + n <= 256) memcpy(disp->vsc[ini], MEM + ARG(2), 16 * n);
  VOLVER(4, 0);
}
API(D_GetVertexShaderConstantF) {
  uint32_t ini = ARG(1), n = ARG(3);
  if (ini + n <= 256) memcpy(MEM + ARG(2), disp->vsc[ini], 16 * n);
  VOLVER(4, 0);
}
API(D_SetVertexShaderConstantI) { VOLVER(4, 0); }
API(D_SetVertexShaderConstantB) { VOLVER(4, 0); }
API(D_SetStreamSource) {
  uint32_t i = ARG(1);
  if (i >= 16) VOLVER(5, D3DERR_INVALIDCALL);
  Buffer *b = com_host(ARG(2));
  if (b) recurso_addref(&b->r);
  if (disp->stream[i].b) recurso_release(&disp->stream[i].b->r);
  disp->stream[i].b = b; disp->stream[i].off = ARG(3); disp->stream[i].stride = ARG(4);
  VOLVER(5, 0);
}
API(D_GetStreamSource) {
  uint32_t i = ARG(1);
  Buffer *b = i < 16 ? disp->stream[i].b : NULL;
  if (b) recurso_addref(&b->r);
  M32(ARG(2)) = b ? b->r.obj : 0;
  if (ARG(3)) M32(ARG(3)) = i < 16 ? disp->stream[i].off : 0;
  if (ARG(4)) M32(ARG(4)) = i < 16 ? disp->stream[i].stride : 0;
  VOLVER(5, 0);
}
API(D_SetStreamSourceFreq) { VOLVER(3, 0); }
API(D_SetIndices) {
  Buffer *b = com_host(ARG(1));
  if (b) recurso_addref(&b->r);
  if (disp->ib) recurso_release(&disp->ib->r);
  disp->ib = b;
  VOLVER(2, 0);
}
API(D_GetIndices) {
  if (disp->ib) recurso_addref(&disp->ib->r);
  M32(ARG(1)) = disp->ib ? disp->ib->r.obj : 0;
  VOLVER(2, 0);
}
API(D_SetPixelShader) {
  if (ARG(1)) fprintf(stderr, "SetPixelShader con shader: no soportado todavía\n");
  disp->ps = ARG(1);
  VOLVER(2, 0);
}
API(D_GetPixelShader) { M32(ARG(1)) = 0; VOLVER(2, 0); }
API(D_SetPixelShaderConstantF) { VOLVER(4, 0); }

// Recursos (recursos.c).
void D_CreateTexture(Ctx *c);
void D_CreateVertexBuffer(Ctx *c);
void D_CreateIndexBuffer(Ctx *c);
void D_CreateRenderTarget(Ctx *c);
void D_CreateDepthStencilSurface(Ctx *c);
void D_CreateOffscreenPlainSurface(Ctx *c);
void D_UpdateSurface(Ctx *c);
void D_UpdateTexture(Ctx *c);
void D_GetRenderTargetData(Ctx *c);
void D_StretchRect(Ctx *c);
void D_ColorFill(Ctx *c);
void D_CreateQuery(Ctx *c);

static const Metodo M_DISP[] = {
  {"QueryInterface", D_QueryInterface}, {"AddRef", D_AddRef}, {"Release", D_Release},
  {"TestCooperativeLevel", D_TestCooperativeLevel}, {"GetAvailableTextureMem", D_GetAvailableTextureMem},
  {"EvictManagedResources", D_EvictManagedResources}, {"GetDirect3D", D_GetDirect3D}, {"GetDeviceCaps", D_GetDeviceCaps},
  {"GetDisplayMode", D_GetDisplayMode}, {"GetCreationParameters", D_GetCreationParameters},
  {"SetCursorProperties", D_SetCursorProperties}, {"SetCursorPosition", D_SetCursorPosition}, {"ShowCursor", D_ShowCursor},
  {"CreateAdditionalSwapChain", NULL}, {"GetSwapChain", NULL}, {"GetNumberOfSwapChains", NULL}, {"Reset", D_Reset},
  {"Present", D_Present}, {"GetBackBuffer", D_GetBackBuffer}, {"GetRasterStatus", D_GetRasterStatus},
  {"SetDialogBoxMode", D_SetDialogBoxMode}, {"SetGammaRamp", D_SetGammaRamp}, {"GetGammaRamp", D_GetGammaRamp},
  {"CreateTexture", D_CreateTexture}, {"CreateVolumeTexture", NULL}, {"CreateCubeTexture", NULL},
  {"CreateVertexBuffer", D_CreateVertexBuffer}, {"CreateIndexBuffer", D_CreateIndexBuffer},
  {"CreateRenderTarget", D_CreateRenderTarget}, {"CreateDepthStencilSurface", D_CreateDepthStencilSurface},
  {"UpdateSurface", D_UpdateSurface}, {"UpdateTexture", D_UpdateTexture}, {"GetRenderTargetData", D_GetRenderTargetData},
  {"GetFrontBufferData", NULL}, {"StretchRect", D_StretchRect}, {"ColorFill", D_ColorFill},
  {"CreateOffscreenPlainSurface", D_CreateOffscreenPlainSurface}, {"SetRenderTarget", D_SetRenderTarget},
  {"GetRenderTarget", D_GetRenderTarget}, {"SetDepthStencilSurface", D_SetDepthStencilSurface},
  {"GetDepthStencilSurface", D_GetDepthStencilSurface}, {"BeginScene", D_BeginScene}, {"EndScene", D_EndScene},
  {"Clear", D_Clear}, {"SetTransform", D_SetTransform}, {"GetTransform", D_GetTransform},
  {"MultiplyTransform", D_MultiplyTransform}, {"SetViewport", D_SetViewport}, {"GetViewport", D_GetViewport},
  {"SetMaterial", D_SetMaterial}, {"GetMaterial", D_GetMaterial}, {"SetLight", D_SetLight}, {"GetLight", D_GetLight},
  {"LightEnable", D_LightEnable}, {"GetLightEnable", D_GetLightEnable}, {"SetClipPlane", D_SetClipPlane},
  {"GetClipPlane", NULL}, {"SetRenderState", D_SetRenderState}, {"GetRenderState", D_GetRenderState},
  {"CreateStateBlock", NULL}, {"BeginStateBlock", NULL}, {"EndStateBlock", NULL}, {"SetClipStatus", NULL},
  {"GetClipStatus", NULL}, {"GetTexture", D_GetTexture}, {"SetTexture", D_SetTexture},
  {"GetTextureStageState", D_GetTextureStageState}, {"SetTextureStageState", D_SetTextureStageState},
  {"GetSamplerState", D_GetSamplerState}, {"SetSamplerState", D_SetSamplerState}, {"ValidateDevice", D_ValidateDevice},
  {"SetPaletteEntries", NULL}, {"GetPaletteEntries", NULL}, {"SetCurrentTexturePalette", NULL},
  {"GetCurrentTexturePalette", NULL}, {"SetScissorRect", D_SetScissorRect}, {"GetScissorRect", D_GetScissorRect},
  {"SetSoftwareVertexProcessing", D_SetSoftwareVertexProcessing},
  {"GetSoftwareVertexProcessing", D_GetSoftwareVertexProcessing}, {"SetNPatchMode", D_SetNPatchMode},
  {"GetNPatchMode", NULL}, {"DrawPrimitive", D_DrawPrimitive}, {"DrawIndexedPrimitive", D_DrawIndexedPrimitive},
  {"DrawPrimitiveUP", D_DrawPrimitiveUP}, {"DrawIndexedPrimitiveUP", D_DrawIndexedPrimitiveUP},
  {"ProcessVertices", NULL}, {"CreateVertexDeclaration", D_CreateVertexDeclaration},
  {"SetVertexDeclaration", D_SetVertexDeclaration}, {"GetVertexDeclaration", D_GetVertexDeclaration},
  {"SetFVF", D_SetFVF}, {"GetFVF", D_GetFVF}, {"CreateVertexShader", D_CreateVertexShader},
  {"SetVertexShader", D_SetVertexShader}, {"GetVertexShader", D_GetVertexShader},
  {"SetVertexShaderConstantF", D_SetVertexShaderConstantF}, {"GetVertexShaderConstantF", D_GetVertexShaderConstantF},
  {"SetVertexShaderConstantI", D_SetVertexShaderConstantI}, {"GetVertexShaderConstantI", NULL},
  {"SetVertexShaderConstantB", D_SetVertexShaderConstantB}, {"GetVertexShaderConstantB", NULL},
  {"SetStreamSource", D_SetStreamSource}, {"GetStreamSource", D_GetStreamSource},
  {"SetStreamSourceFreq", D_SetStreamSourceFreq}, {"GetStreamSourceFreq", NULL}, {"SetIndices", D_SetIndices},
  {"GetIndices", D_GetIndices}, {"CreatePixelShader", NULL}, {"SetPixelShader", D_SetPixelShader},
  {"GetPixelShader", D_GetPixelShader}, {"SetPixelShaderConstantF", D_SetPixelShaderConstantF},
  {"GetPixelShaderConstantF", NULL}, {"SetPixelShaderConstantI", NULL}, {"GetPixelShaderConstantI", NULL},
  {"SetPixelShaderConstantB", NULL}, {"GetPixelShaderConstantB", NULL}, {"DrawRectPatch", NULL},
  {"DrawTriPatch", NULL}, {"DeletePatch", NULL}, {"CreateQuery", D_CreateQuery},
};

static uint32_t obj_disp(Dispositivo *d) {
  static uint32_t vt;
  if (!vt) vt = com_vtabla("IDirect3DDevice9", M_DISP, (int)(sizeof M_DISP / sizeof M_DISP[0]));
  d->r.obj = com_objeto(vt, d);
  return d->r.obj;
}

const Exportada EXP_D3D9[] = {{"Direct3DCreate9", Direct3DCreate9}, {0, 0}};
