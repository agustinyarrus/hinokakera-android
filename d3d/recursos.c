// Recursos de Direct3D 9: texturas, superficies, vertex/index buffers, declaraciones, shaders y consultas.
#include "d3d9_int.h"
#include <stdlib.h>

#define D3DUSAGE_RENDERTARGET 1u
#define D3DUSAGE_DEPTHSTENCIL 2u

uint32_t bytes_por_pixel(uint32_t f) {
  switch (f) {
    case 20: return 3;
    case 21: case 22: return 4;
    case 23: case 24: case 25: case 26: case 51: return 2;
    case 28: case 50: return 1;
    default: return 4;
  }
}

static uint8_t expandir(uint32_t v, int bits) { return (uint8_t)(bits == 5 ? (v << 3) | (v >> 2) : bits == 6 ? (v << 2) | (v >> 4) : v * 17); }

void convertir_a_rgba(uint32_t f, const uint8_t *s, uint32_t w, uint32_t h, uint8_t *d) {
  size_t n = (size_t)w * h;
  for (size_t i = 0; i < n; i++, d += 4) {
    uint32_t v;
    switch (f) {
      case 21: d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = s[3]; s += 4; break;
      case 22: d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = 255; s += 4; break;
      case 20: d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = 255; s += 3; break;
      case 23: v = s[0] | (uint32_t)s[1] << 8; d[0] = expandir(v >> 11, 5); d[1] = expandir((v >> 5) & 63, 6); d[2] = expandir(v & 31, 5); d[3] = 255; s += 2; break;
      case 24: case 25: v = s[0] | (uint32_t)s[1] << 8; d[0] = expandir((v >> 10) & 31, 5); d[1] = expandir((v >> 5) & 31, 5); d[2] = expandir(v & 31, 5); d[3] = f == 24 || (v >> 15) ? 255 : 0; s += 2; break;
      case 26: v = s[0] | (uint32_t)s[1] << 8; d[0] = expandir((v >> 8) & 15, 4); d[1] = expandir((v >> 4) & 15, 4); d[2] = expandir(v & 15, 4); d[3] = expandir(v >> 12, 4); s += 2; break;
      case 28: d[0] = d[1] = d[2] = 0; d[3] = s[0]; s += 1; break;
      case 50: d[0] = d[1] = d[2] = s[0]; d[3] = 255; s += 1; break;
      case 51: d[0] = d[1] = d[2] = s[0]; d[3] = s[1]; s += 2; break;
      default: d[0] = d[1] = d[2] = d[3] = 255; s += 4;
    }
  }
}

static uint32_t lado(uint32_t v, int nivel) { v >>= nivel; return v ? v : 1; }

// ---------------- texturas ----------------
Textura *textura_nueva(uint32_t w, uint32_t h, uint32_t niveles, uint32_t uso, uint32_t formato, uint32_t pool) {
  Textura *t = calloc(1, sizeof *t);
  t->r.tipo = R_TEXTURA; t->r.refs = 1;
  uint32_t max = 1;
  while ((w >> max) || (h >> max)) max++;
  if (uso & D3DUSAGE_RENDERTARGET) niveles = 1;
  t->w = w; t->h = h; t->niveles = niveles && niveles < max ? niveles : max; t->uso = uso; t->formato = formato; t->pool = pool;
  if (t->niveles > MAX_NIVELES) t->niveles = MAX_NIVELES;
  glGenTextures(1, &t->gl);
  glBindTexture(GL_TEXTURE_2D, t->gl);
  for (uint32_t i = 0; i < t->niveles; i++)
    glTexImage2D(GL_TEXTURE_2D, (GLint)i, GL_RGBA8, (GLsizei)lado(w, (int)i), (GLsizei)lado(h, (int)i), 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, (GLint)t->niveles - 1);
  if (uso & D3DUSAGE_RENDERTARGET) {
    if (getenv("HINO_LOG_DIBUJOS")) fprintf(stderr, "textura render target %ux%u\n", w, h);
    glGenFramebuffers(1, &t->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t->gl, 0);
    aplicar_destino(); // vuelve al destino actual
  }
  return t;
}

void textura_subir(Textura *t, int nivel, const uint8_t *datos) {
  uint32_t w = lado(t->w, nivel), h = lado(t->h, nivel);
  uint8_t *rgba = malloc((size_t)w * h * 4);
  convertir_a_rgba(t->formato, datos, w, h, rgba);
  glBindTexture(GL_TEXTURE_2D, t->gl);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexSubImage2D(GL_TEXTURE_2D, nivel, 0, 0, (GLsizei)w, (GLsizei)h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  if (nivel == 0) {
    // Alfa promedio en una grilla de 64x64: el modo ancho la mira para no extender capas transparentes de pantalla
    // completa (el texto del modo historia va en una), solo fondos.
    if (!t->alfa) t->alfa = malloc(64 * 64);
    for (uint32_t cy = 0; cy < 64; cy++)
      for (uint32_t cx = 0; cx < 64; cx++) {
        uint32_t x0 = cx * w / 64, x1 = SDL_max(x0 + 1, (cx + 1) * w / 64), y0 = cy * h / 64, y1 = SDL_max(y0 + 1, (cy + 1) * h / 64);
        uint32_t suma = 0, n = 0;
        for (uint32_t y = y0; y < y1 && y < h; y++)
          for (uint32_t x = x0; x < x1 && x < w; x++) { suma += rgba[((size_t)y * w + x) * 4 + 3]; n++; }
        t->alfa[cy * 64 + cx] = (uint8_t)(n ? suma / n : 255);
      }
  }
  free(rgba);
}

void textura_borrar(Textura *t) {
  glDeleteTextures(1, &t->gl);
  if (t->fbo) glDeleteFramebuffers(1, &t->fbo);
  for (int i = 0; i < MAX_NIVELES; i++) {
    free(t->copia[i]);
    if (t->lock_va[i]) heap_free(0, t->lock_va[i]);
    if (t->sup[i]) { com_borrar(t->sup[i]->r.obj); free(t->sup[i]); }
  }
  for (int i = 0; i < 16; i++) if (disp && disp->tex[i] == t) disp->tex[i] = NULL;
  com_borrar(t->r.obj);
  free(t->alfa);
  free(t);
}

static uint32_t bloquear(uint8_t **copia, uint32_t *lock_va, uint32_t w, uint32_t h, uint32_t formato, uint32_t rect, uint32_t salida) {
  uint32_t pitch = w * bytes_por_pixel(formato), tam = pitch * h;
  if (!*copia) *copia = calloc(1, tam);
  if (!*lock_va) {
    *lock_va = heap_alloc(0, tam, 0);
    if (!*lock_va) return E_OUTOFMEMORY;
    memcpy(MEM + *lock_va, *copia, tam);
  }
  uint32_t off = rect ? M32(rect + 4) * pitch + M32(rect) * bytes_por_pixel(formato) : 0;
  M32(salida) = pitch;
  M32(salida + 4) = *lock_va + off;
  return 0;
}
static void desbloquear(uint8_t *copia, uint32_t *lock_va, uint32_t tam) {
  if (!*lock_va) return;
  memcpy(copia, MEM + *lock_va, tam);
  heap_free(0, *lock_va);
  *lock_va = 0;
}

// ---------------- superficies ----------------
Superficie *superficie_de(Textura *t, int nivel) {
  if (!t->sup[nivel]) {
    Superficie *s = calloc(1, sizeof *s);
    s->r.tipo = R_SUPERFICIE; s->r.refs = 0; // las cuentas van a la textura
    s->tex = t; s->nivel = nivel; s->w = s->w_gl = lado(t->w, nivel); s->h = lado(t->h, nivel);
    s->formato = t->formato; s->uso = t->uso; s->pool = t->pool;
    t->sup[nivel] = s;
    obj_superficie(s);
  }
  return t->sup[nivel];
}

Superficie *superficie_nueva(uint32_t w, uint32_t h, uint32_t formato, int profundidad, int con_fbo, uint32_t extra) {
  Superficie *s = calloc(1, sizeof *s);
  s->r.tipo = R_SUPERFICIE; s->r.refs = 1;
  s->w = w; s->h = h; s->formato = formato; s->profundidad = profundidad;
  s->w_gl = w + extra; // modo ancho: el backbuffer y su profundidad son más anchos que lo que ve el juego
  if (getenv("HINO_LOG_DIBUJOS")) fprintf(stderr, "superficie %ux%u formato %u%s%s\n", w, h, formato, profundidad ? " profundidad" : "", con_fbo ? " fbo" : "");
  if (profundidad) {
    glGenRenderbuffers(1, &s->prof_rb);
    glBindRenderbuffer(GL_RENDERBUFFER, s->prof_rb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, (GLsizei)s->w_gl, (GLsizei)h);
  } else if (con_fbo) {
    glGenRenderbuffers(1, &s->color_rb);
    glBindRenderbuffer(GL_RENDERBUFFER, s->color_rb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, (GLsizei)s->w_gl, (GLsizei)h);
    glGenFramebuffers(1, &s->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, s->fbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, s->color_rb);
  }
  obj_superficie(s);
  return s;
}

Superficie *backbuffer_nuevo(uint32_t w, uint32_t h) { return superficie_nueva(w, h, 22, 0, 1, (uint32_t)ancho_extra); }

void superficie_borrar(Superficie *s) {
  if (s->tex) return;
  if (s->color_rb) glDeleteRenderbuffers(1, &s->color_rb);
  if (s->prof_rb) glDeleteRenderbuffers(1, &s->prof_rb);
  if (s->fbo) glDeleteFramebuffers(1, &s->fbo);
  free(s->copia);
  if (s->lock_va) heap_free(0, s->lock_va);
  if (disp && disp->rt == s) disp->rt = disp->backbuffer;
  if (disp && disp->ds == s) disp->ds = NULL;
  com_borrar(s->r.obj);
  free(s);
}

GLuint fbo_de(Superficie *rt) { return rt->tex ? rt->tex->fbo : rt->fbo; }

// ---------------- cuentas de referencias ----------------
uint32_t recurso_release(Recurso *r) {
  if (r->tipo == R_SUPERFICIE && ((Superficie *)r)->tex) return recurso_release(&((Superficie *)r)->tex->r);
  if (--r->refs > 0) return (uint32_t)r->refs;
  r->refs = 0;
  switch (r->tipo) {
    case R_TEXTURA: textura_borrar((Textura *)r); break;
    case R_SUPERFICIE: if (disp && (Superficie *)r != disp->backbuffer && (Superficie *)r != disp->prof_auto) superficie_borrar((Superficie *)r); break;
    case R_VB: case R_IB: heap_free(0, ((Buffer *)r)->va); com_borrar(r->obj); free(r); break;
    default: com_borrar(r->obj); free(r); break; // shaders, declaraciones y consultas
  }
  return 0;
}
static void addref_de(Recurso *r) {
  if (r->tipo == R_SUPERFICIE && ((Superficie *)r)->tex) r = &((Superficie *)r)->tex->r;
  r->refs++;
}

// IUnknown e IDirect3DResource9 comunes.
API(R_QueryInterface) {
  Recurso *r = com_host(ARG(0));
  uint32_t iid = M32(ARG(1));
  // IUnknown, IDirect3DResource9, IDirect3DBaseTexture9, IDirect3DTexture9, IDirect3DSurface9, buffers.
  static const uint32_t ok[] = {0, 0x05eec05du, 0x580ca87eu, 0x85c31227u, 0x0cfbaf3au, 0xb64bb1b5u, 0x7c9dd65eu};
  for (size_t i = 0; i < sizeof ok / sizeof ok[0]; i++) if (iid == ok[i] && r) { addref_de(r); M32(ARG(2)) = ARG(0); VOLVER(3, 0); }
  M32(ARG(2)) = 0;
  VOLVER(3, E_NOINTERFACE);
}
API(R_AddRef) { Recurso *r = com_host(ARG(0)); if (r) addref_de(r); VOLVER(1, r ? (uint32_t)r->refs : 0); }
API(R_Release) { Recurso *r = com_host(ARG(0)); VOLVER(1, r ? recurso_release(r) : 0); }
API(R_GetDevice) { disp->r.refs++; M32(ARG(1)) = disp->r.obj; VOLVER(2, 0); }
API(R_SetPrivateData) { VOLVER(5, 0); }
API(R_GetPrivateData) { VOLVER(4, D3DERR_NOTAVAILABLE); }
API(R_FreePrivateData) { VOLVER(2, 0); }
API(R_SetPriority) { VOLVER(2, 0); }
API(R_GetPriority) { VOLVER(1, 0); }
API(R_PreLoad) { VOLVER(1, 0); }
#define COMUNES(tipo_rtype)                                                                                   \
  {"QueryInterface", R_QueryInterface}, {"AddRef", R_AddRef}, {"Release", R_Release}, {"GetDevice", R_GetDevice}, \
  {"SetPrivateData", R_SetPrivateData}, {"GetPrivateData", R_GetPrivateData}, {"FreePrivateData", R_FreePrivateData}, \
  {"SetPriority", R_SetPriority}, {"GetPriority", R_GetPriority}, {"PreLoad", R_PreLoad}, {"GetType", tipo_rtype}

// ---------------- IDirect3DTexture9 ----------------
API(T_GetType) { VOLVER(1, 3); }
API(T_SetLOD) { VOLVER(2, 0); }
API(T_GetLOD) { VOLVER(1, 0); }
API(T_GetLevelCount) { Textura *t = com_host(ARG(0)); VOLVER(1, t ? t->niveles : 0); }
API(T_SetAutoGenFilterType) { VOLVER(2, 0); }
API(T_GetAutoGenFilterType) { VOLVER(1, 2); }
API(T_GenerateMipSubLevels) { VOLVER(1, 0); }
static void desc_superficie(uint32_t p, uint32_t formato, uint32_t uso, uint32_t pool, uint32_t w, uint32_t h) {
  M32(p) = formato; M32(p + 4) = 1; M32(p + 8) = uso; M32(p + 12) = pool; M32(p + 16) = 0; M32(p + 20) = 0; M32(p + 24) = w; M32(p + 28) = h;
}
API(T_GetLevelDesc) {
  Textura *t = com_host(ARG(0));
  uint32_t n = ARG(1);
  if (!t || n >= t->niveles) VOLVER(3, D3DERR_INVALIDCALL);
  desc_superficie(ARG(2), t->formato, t->uso, t->pool, lado(t->w, (int)n), lado(t->h, (int)n));
  VOLVER(3, 0);
}
API(T_GetSurfaceLevel) {
  Textura *t = com_host(ARG(0));
  uint32_t n = ARG(1);
  if (!t || n >= t->niveles) VOLVER(3, D3DERR_INVALIDCALL);
  Superficie *s = superficie_de(t, (int)n);
  t->r.refs++;
  M32(ARG(2)) = s->r.obj;
  VOLVER(3, 0);
}
API(T_LockRect) {
  Textura *t = com_host(ARG(0));
  uint32_t n = ARG(1);
  if (!t || n >= t->niveles) VOLVER(5, D3DERR_INVALIDCALL);
  VOLVER(5, bloquear(&t->copia[n], &t->lock_va[n], lado(t->w, (int)n), lado(t->h, (int)n), t->formato, ARG(3), ARG(2)));
}
API(T_UnlockRect) {
  Textura *t = com_host(ARG(0));
  uint32_t n = ARG(1);
  if (!t || n >= t->niveles || !t->lock_va[n]) VOLVER(2, D3DERR_INVALIDCALL);
  uint32_t w = lado(t->w, (int)n), h = lado(t->h, (int)n);
  desbloquear(t->copia[n], &t->lock_va[n], w * h * bytes_por_pixel(t->formato));
  textura_subir(t, (int)n, t->copia[n]);
  VOLVER(2, 0);
}
API(T_AddDirtyRect) { VOLVER(2, 0); }
static const Metodo M_TEX[] = {
  COMUNES(T_GetType), {"SetLOD", T_SetLOD}, {"GetLOD", T_GetLOD}, {"GetLevelCount", T_GetLevelCount},
  {"SetAutoGenFilterType", T_SetAutoGenFilterType}, {"GetAutoGenFilterType", T_GetAutoGenFilterType},
  {"GenerateMipSubLevels", T_GenerateMipSubLevels}, {"GetLevelDesc", T_GetLevelDesc}, {"GetSurfaceLevel", T_GetSurfaceLevel},
  {"LockRect", T_LockRect}, {"UnlockRect", T_UnlockRect}, {"AddDirtyRect", T_AddDirtyRect},
};
uint32_t obj_textura(Textura *t) {
  static uint32_t vt;
  if (!vt) vt = com_vtabla("IDirect3DTexture9", M_TEX, (int)(sizeof M_TEX / sizeof M_TEX[0]));
  return t->r.obj = com_objeto(vt, t);
}

// ---------------- IDirect3DSurface9 ----------------
API(S_GetType) { VOLVER(1, 1); }
API(S_GetContainer) {
  Superficie *s = com_host(ARG(0));
  if (s && s->tex) { s->tex->r.refs++; M32(ARG(2)) = s->tex->r.obj; VOLVER(3, 0); }
  disp->r.refs++;
  M32(ARG(2)) = disp->r.obj;
  VOLVER(3, 0);
}
API(S_GetDesc) {
  Superficie *s = com_host(ARG(0));
  if (!s) VOLVER(2, D3DERR_INVALIDCALL);
  desc_superficie(ARG(1), s->formato, s->profundidad ? D3DUSAGE_DEPTHSTENCIL : s->fbo || (s->tex && s->tex->fbo) ? D3DUSAGE_RENDERTARGET : s->uso, s->pool, s->w, s->h);
  VOLVER(2, 0);
}
API(S_LockRect) {
  Superficie *s = com_host(ARG(0));
  if (!s) VOLVER(4, D3DERR_INVALIDCALL);
  if (s->tex) VOLVER(4, bloquear(&s->tex->copia[s->nivel], &s->tex->lock_va[s->nivel], s->w, s->h, s->formato, ARG(2), ARG(1)));
  VOLVER(4, bloquear(&s->copia, &s->lock_va, s->w, s->h, s->formato, ARG(2), ARG(1)));
}
API(S_UnlockRect) {
  Superficie *s = com_host(ARG(0));
  if (!s) VOLVER(1, D3DERR_INVALIDCALL);
  uint32_t tam = s->w * s->h * bytes_por_pixel(s->formato);
  if (s->tex) {
    desbloquear(s->tex->copia[s->nivel], &s->tex->lock_va[s->nivel], tam);
    textura_subir(s->tex, s->nivel, s->tex->copia[s->nivel]);
  } else desbloquear(s->copia, &s->lock_va, tam);
  VOLVER(1, 0);
}
static const Metodo M_SUP[] = {
  COMUNES(S_GetType), {"GetContainer", S_GetContainer}, {"GetDesc", S_GetDesc}, {"LockRect", S_LockRect},
  {"UnlockRect", S_UnlockRect}, {"GetDC", NULL}, {"ReleaseDC", NULL},
};
uint32_t obj_superficie(Superficie *s) {
  static uint32_t vt;
  if (!vt) vt = com_vtabla("IDirect3DSurface9", M_SUP, (int)(sizeof M_SUP / sizeof M_SUP[0]));
  if (!s->r.obj) s->r.obj = com_objeto(vt, s);
  return s->r.obj;
}

// ---------------- vertex/index buffers ----------------
API(VB_GetType) { VOLVER(1, 6); }
API(IB_GetType) { VOLVER(1, 7); }
API(B_Lock) {
  Buffer *b = com_host(ARG(0));
  if (!b || ARG(1) > b->tam) VOLVER(5, D3DERR_INVALIDCALL);
  M32(ARG(3)) = b->va + ARG(1);
  VOLVER(5, 0);
}
API(B_Unlock) { VOLVER(1, 0); }
API(VB_GetDesc) {
  Buffer *b = com_host(ARG(0));
  uint32_t p = ARG(1);
  M32(p) = 100; M32(p + 4) = 6; M32(p + 8) = b->uso; M32(p + 12) = b->pool; M32(p + 16) = b->tam; M32(p + 20) = b->formato;
  VOLVER(2, 0);
}
API(IB_GetDesc) {
  Buffer *b = com_host(ARG(0));
  uint32_t p = ARG(1);
  M32(p) = b->formato; M32(p + 4) = 7; M32(p + 8) = b->uso; M32(p + 12) = b->pool; M32(p + 16) = b->tam;
  VOLVER(2, 0);
}
static const Metodo M_VB[] = {COMUNES(VB_GetType), {"Lock", B_Lock}, {"Unlock", B_Unlock}, {"GetDesc", VB_GetDesc}};
static const Metodo M_IB[] = {COMUNES(IB_GetType), {"Lock", B_Lock}, {"Unlock", B_Unlock}, {"GetDesc", IB_GetDesc}};
uint32_t obj_vb(Buffer *b) {
  static uint32_t vt;
  if (!vt) vt = com_vtabla("IDirect3DVertexBuffer9", M_VB, 14);
  return b->r.obj = com_objeto(vt, b);
}
uint32_t obj_ib(Buffer *b) {
  static uint32_t vt;
  if (!vt) vt = com_vtabla("IDirect3DIndexBuffer9", M_IB, 14);
  return b->r.obj = com_objeto(vt, b);
}
static Buffer *buffer_nuevo(TipoRecurso tipo, uint32_t tam, uint32_t uso, uint32_t formato, uint32_t pool) {
  Buffer *b = calloc(1, sizeof *b);
  b->r.tipo = tipo; b->r.refs = 1;
  b->tam = tam; b->uso = uso; b->formato = formato; b->pool = pool;
  b->va = heap_alloc(0, tam ? tam : 4, 1);
  return b;
}
void D_CreateVertexBuffer(Ctx *c) {
  Buffer *b = buffer_nuevo(R_VB, ARG(1), ARG(2), ARG(3), ARG(4));
  if (!b->va) { free(b); VOLVER(7, E_OUTOFMEMORY); }
  M32(ARG(5)) = obj_vb(b);
  VOLVER(7, 0);
}
void D_CreateIndexBuffer(Ctx *c) {
  Buffer *b = buffer_nuevo(R_IB, ARG(1), ARG(2), ARG(3), ARG(4));
  if (!b->va) { free(b); VOLVER(7, E_OUTOFMEMORY); }
  M32(ARG(5)) = obj_ib(b);
  VOLVER(7, 0);
}

// ---------------- creación desde el device ----------------
void D_CreateTexture(Ctx *c) {
  uint32_t w = ARG(1), h = ARG(2), niveles = ARG(3), uso = ARG(4), formato = ARG(5), pool = ARG(6);
  if (!w || !h) VOLVER(9, D3DERR_INVALIDCALL);
  Textura *t = textura_nueva(w, h, niveles, uso, formato, pool);
  M32(ARG(7)) = obj_textura(t);
  VOLVER(9, 0);
}
void D_CreateRenderTarget(Ctx *c) {
  Superficie *s = superficie_nueva(ARG(1), ARG(2), ARG(3), 0, 1, 0);
  M32(ARG(7)) = s->r.obj;
  VOLVER(9, 0);
}
void D_CreateDepthStencilSurface(Ctx *c) {
  Superficie *s = superficie_nueva(ARG(1), ARG(2), ARG(3), 1, 0, 0);
  M32(ARG(7)) = s->r.obj;
  VOLVER(9, 0);
}
void D_CreateOffscreenPlainSurface(Ctx *c) {
  Superficie *s = superficie_nueva(ARG(1), ARG(2), ARG(3), 0, 0, 0);
  s->pool = ARG(4);
  M32(ARG(5)) = s->r.obj;
  VOLVER(7, 0);
}

// Copias entre superficies y texturas.
static uint8_t *contenido(Superficie *s) {
  uint32_t tam = s->w * s->h * bytes_por_pixel(s->formato);
  uint8_t **p = s->tex ? &s->tex->copia[s->nivel] : &s->copia;
  if (!*p) *p = calloc(1, tam);
  return *p;
}
void D_UpdateSurface(Ctx *c) {
  Superficie *a = com_host(ARG(1)), *b = com_host(ARG(3));
  if (!a || !b || a->formato != b->formato) VOLVER(5, D3DERR_INVALIDCALL);
  uint32_t bpp = bytes_por_pixel(a->formato), x0 = 0, y0 = 0, w = a->w, h = a->h, dx = 0, dy = 0;
  if (ARG(2)) { x0 = M32(ARG(2)); y0 = M32(ARG(2) + 4); w = M32(ARG(2) + 8) - x0; h = M32(ARG(2) + 12) - y0; }
  if (ARG(4)) { dx = M32(ARG(4)); dy = M32(ARG(4) + 4); }
  uint8_t *src = contenido(a), *dst = contenido(b);
  for (uint32_t y = 0; y < h && dy + y < b->h; y++) memcpy(dst + ((dy + y) * b->w + dx) * bpp, src + ((y0 + y) * a->w + x0) * bpp, w * bpp);
  if (b->tex) textura_subir(b->tex, b->nivel, dst);
  VOLVER(5, 0);
}
void D_UpdateTexture(Ctx *c) {
  Textura *a = com_host(ARG(1)), *b = com_host(ARG(2));
  if (!a || !b) VOLVER(3, D3DERR_INVALIDCALL);
  for (uint32_t i = 0; i < a->niveles && i < b->niveles; i++) {
    if (!a->copia[i]) continue;
    size_t tam = (size_t)lado(a->w, (int)i) * lado(a->h, (int)i) * bytes_por_pixel(a->formato);
    if (!b->copia[i]) b->copia[i] = malloc(tam);
    memcpy(b->copia[i], a->copia[i], tam);
    textura_subir(b, (int)i, b->copia[i]);
  }
  VOLVER(3, 0);
}
void D_GetRenderTargetData(Ctx *c) {
  Superficie *rt = com_host(ARG(1)), *dst = com_host(ARG(2));
  if (!rt || !dst || !fbo_de(rt)) VOLVER(3, D3DERR_INVALIDCALL);
  uint8_t *rgba = malloc((size_t)rt->w * rt->h * 4), *d = contenido(dst);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo_de(rt));
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels((GLint)(rt->w_gl - rt->w) / 2, 0, (GLsizei)rt->w, (GLsizei)rt->h, GL_RGBA, GL_UNSIGNED_BYTE, rgba); // modo ancho: el centro
  for (size_t i = 0; i < (size_t)rt->w * rt->h; i++) { d[4 * i] = rgba[4 * i + 2]; d[4 * i + 1] = rgba[4 * i + 1]; d[4 * i + 2] = rgba[4 * i]; d[4 * i + 3] = rgba[4 * i + 3]; }
  free(rgba);
  aplicar_destino();
  VOLVER(3, 0);
}
void D_StretchRect(Ctx *c) {
  Superficie *a = com_host(ARG(1)), *b = com_host(ARG(3));
  if (!a || !b || !fbo_de(a) || !fbo_de(b)) VOLVER(6, D3DERR_INVALIDCALL);
  int32_t r[4] = {0, 0, (int32_t)a->w, (int32_t)a->h}, s[4] = {0, 0, (int32_t)b->w, (int32_t)b->h};
  if (ARG(2)) for (int i = 0; i < 4; i++) r[i] = (int32_t)M32(ARG(2) + 4u * (uint32_t)i);
  if (ARG(4)) for (int i = 0; i < 4; i++) s[i] = (int32_t)M32(ARG(4) + 4u * (uint32_t)i);
  if (registrando_dibujos())
    fprintf(stderr, "StretchRect %s:%ux%u [%d,%d,%d,%d] -> %s:%ux%u [%d,%d,%d,%d]\n", a == disp->backbuffer ? "bb" : "rt", a->w, a->h,
            r[0], r[1], r[2], r[3], b == disp->backbuffer ? "bb" : "rt", b->w, b->h, s[0], s[1], s[2], s[3]);
  // Modo ancho: lo del juego está corrido al centro del backbuffer.
  for (int i = 0; i < 4; i += 2) { r[i] += (int32_t)(a->w_gl - a->w) / 2; s[i] += (int32_t)(b->w_gl - b->w) / 2; }
  glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_de(a));
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo_de(b));
  glDisable(GL_SCISSOR_TEST);
  glBlitFramebuffer(r[0], r[1], r[2], r[3], s[0], s[1], s[2], s[3], GL_COLOR_BUFFER_BIT, ARG(5) == 2 ? GL_LINEAR : GL_NEAREST);
  aplicar_destino();
  VOLVER(6, 0);
}
void D_ColorFill(Ctx *c) {
  Superficie *s = com_host(ARG(1));
  if (!s || !fbo_de(s)) VOLVER(4, D3DERR_INVALIDCALL);
  uint32_t col = ARG(3);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo_de(s));
  if (ARG(2)) { glEnable(GL_SCISSOR_TEST); glScissor((GLint)M32(ARG(2)), (GLint)M32(ARG(2) + 4), (GLsizei)(M32(ARG(2) + 8) - M32(ARG(2))), (GLsizei)(M32(ARG(2) + 12) - M32(ARG(2) + 4))); }
  else glDisable(GL_SCISSOR_TEST);
  glColorMask(1, 1, 1, 1);
  glClearColor(((col >> 16) & 255) / 255.0f, ((col >> 8) & 255) / 255.0f, (col & 255) / 255.0f, (col >> 24) / 255.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  aplicar_destino();
  VOLVER(4, 0);
}

// ---------------- shaders, declaraciones y consultas ----------------
API(VS_GetFunction) {
  ShaderV *s = com_host(ARG(0));
  if (ARG(2)) M32(ARG(2)) = s->fuente_tam;
  if (ARG(1)) memcpy(MEM + ARG(1), MEM + s->fuente_va, s->fuente_tam);
  VOLVER(3, 0);
}
static const Metodo M_VS[] = {{"QueryInterface", R_QueryInterface}, {"AddRef", R_AddRef}, {"Release", R_Release},
                              {"GetDevice", R_GetDevice}, {"GetFunction", VS_GetFunction}};
uint32_t obj_vs(ShaderV *s) {
  static uint32_t vt;
  if (!vt) vt = com_vtabla("IDirect3DVertexShader9", M_VS, 5);
  return s->r.obj = com_objeto(vt, s);
}
API(DE_GetDeclaration) {
  Decl *d = com_host(ARG(0));
  if (ARG(2)) M32(ARG(2)) = (uint32_t)d->n + 1;
  if (ARG(1)) {
    for (int i = 0; i <= d->n; i++) {
      uint32_t p = ARG(1) + 8u * (uint32_t)i;
      if (i == d->n) { M16(p) = 0xff; M16(p + 2) = 0; M8(p + 4) = 17; M8(p + 5) = 0; M8(p + 6) = 0; M8(p + 7) = 0; break; }
      const Elemento *e = &d->e[i];
      M16(p) = e->stream; M16(p + 2) = e->offset; M8(p + 4) = e->tipo; M8(p + 5) = e->metodo; M8(p + 6) = e->uso; M8(p + 7) = e->indice;
    }
  }
  VOLVER(3, 0);
}
static const Metodo M_DECL[] = {{"QueryInterface", R_QueryInterface}, {"AddRef", R_AddRef}, {"Release", R_Release},
                                {"GetDevice", R_GetDevice}, {"GetDeclaration", DE_GetDeclaration}};
uint32_t obj_decl(Decl *d) {
  static uint32_t vt;
  if (!vt) vt = com_vtabla("IDirect3DVertexDeclaration9", M_DECL, 5);
  return d->r.obj = com_objeto(vt, d);
}
API(Q_GetType) { Consulta *q = com_host(ARG(0)); VOLVER(1, q->tipo); }
API(Q_GetDataSize) { Consulta *q = com_host(ARG(0)); VOLVER(1, q->tipo == 8 ? 4 : q->tipo == 9 ? 4 : 0); }
API(Q_Issue) { VOLVER(2, 0); }
API(Q_GetData) {
  Consulta *q = com_host(ARG(0));
  if (ARG(1) && ARG(2) >= 4) M32(ARG(1)) = q->tipo == 9 ? 1000000u : 1u; // EVENT: listo; OCCLUSION: visible
  VOLVER(4, 0);
}
static const Metodo M_Q[] = {{"QueryInterface", R_QueryInterface}, {"AddRef", R_AddRef}, {"Release", R_Release},
                             {"GetDevice", R_GetDevice}, {"GetType", Q_GetType}, {"GetDataSize", Q_GetDataSize},
                             {"Issue", Q_Issue}, {"GetData", Q_GetData}};
void D_CreateQuery(Ctx *c) {
  uint32_t tipo = ARG(1);
  if (tipo != 8 && tipo != 9) VOLVER(3, D3DERR_NOTAVAILABLE);
  if (!ARG(2)) VOLVER(3, 0);
  static uint32_t vt;
  if (!vt) vt = com_vtabla("IDirect3DQuery9", M_Q, 8);
  Consulta *q = calloc(1, sizeof *q);
  q->r.tipo = R_CONSULTA; q->r.refs = 1; q->tipo = tipo;
  M32(ARG(2)) = q->r.obj = com_objeto(vt, q);
  VOLVER(3, 0);
}
