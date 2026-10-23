// GDI32: objetos y DC mínimos. El juego solo usa GDI para sacar glifos de las fuentes (GetGlyphOutlineA);
// eso se completa cuando el arranque llegue a dibujar texto.
#include "win32.h"
#include <SDL3/SDL.h>
#include <stdlib.h>

typedef enum { G_LIBRE, G_DC, G_FUENTE, G_PINCEL, G_PLUMA, G_BITMAP, G_STOCK } TipoG;
typedef struct {
  TipoG tipo;
  uint32_t fuente, pincel, pluma, bitmap, texto, fondo, ventana; // DC: objetos elegidos y colores
  uint8_t logfont[60];                                          // fuente: LOGFONTA tal cual
} ObjG;
#define MAX_G 1024
static ObjG objs[MAX_G];
#define G(i) (0x00050000u + 4u * (uint32_t)(i))

static uint32_t nuevo(TipoG t) {
  for (int i = 1; i < MAX_G; i++) if (objs[i].tipo == G_LIBRE) { memset(&objs[i], 0, sizeof objs[i]); objs[i].tipo = t; return G(i); }
  fprintf(stderr, "sin objetos GDI\n");
  exit(1);
}
static ObjG *obj(uint32_t h) {
  uint32_t i = (h - 0x00050000u) / 4;
  return h >= G(1) && (h - 0x00050000u) % 4 == 0 && i < MAX_G && objs[i].tipo != G_LIBRE ? &objs[i] : NULL;
}

uint32_t dc_de_ventana(uint32_t hwnd) { uint32_t h = nuevo(G_DC); obj(h)->ventana = hwnd; return h; }

API(GetStockObject) { VOLVER(1, 0x00060000u + ARG(0)); }
API(CreateCompatibleDC) { VOLVER(1, nuevo(G_DC)); }
API(DeleteDC) { ObjG *o = obj(ARG(0)); if (o) o->tipo = G_LIBRE; VOLVER(1, o != NULL); }
API(DeleteObject) { ObjG *o = obj(ARG(0)); if (o && o->tipo != G_DC) o->tipo = G_LIBRE; VOLVER(1, 1); }
API(SelectObject) {
  ObjG *dc = obj(ARG(0)), *o = obj(ARG(1));
  if (!dc) VOLVER(2, 0);
  uint32_t *ranura = o && o->tipo == G_FUENTE ? &dc->fuente : o && o->tipo == G_BITMAP ? &dc->bitmap : o && o->tipo == G_PLUMA ? &dc->pluma : &dc->pincel;
  uint32_t viejo = *ranura;
  *ranura = ARG(1);
  VOLVER(2, viejo ? viejo : 0x00060000u + 13); // SYSTEM_FONT u otro de stock
}
API(SetTextColor) { ObjG *dc = obj(ARG(0)); uint32_t v = dc ? dc->texto : 0; if (dc) dc->texto = ARG(1); VOLVER(2, v); }
API(SetBkColor) { ObjG *dc = obj(ARG(0)); uint32_t v = dc ? dc->fondo : 0; if (dc) dc->fondo = ARG(1); VOLVER(2, v); }
API(CreateFontIndirectA) {
  uint32_t h = nuevo(G_FUENTE);
  memcpy(obj(h)->logfont, MEM + ARG(0), 60);
  VOLVER(1, h);
}
API(CreateFontA) {
  uint32_t h = nuevo(G_FUENTE);
  uint8_t *lf = obj(h)->logfont;
  for (int i = 0; i < 5; i++) memcpy(lf + 4 * i, MEM + c->esp + 4 + 4 * i, 4); // alto, ancho, escape, orientación, peso
  for (int i = 0; i < 8; i++) lf[20 + i] = (uint8_t)ARG(5 + i);              // itálica ... calidad, familia
  SDL_strlcpy((char *)lf + 28, ARG(13) ? gstr(ARG(13)) : "", 32);
  char u[128];
  extern void sjis_a_utf8(const char *, char *, size_t);
  sjis_a_utf8((char *)lf + 28, u, sizeof u);
  fprintf(stderr, "CreateFontA(alto %d, peso %d, juego %u, calidad %u, '%s')\n", (int32_t)ARG(0), ARG(4), ARG(8), ARG(11), u);
  VOLVER(14, h);
}
API(GetDeviceCaps) {
  switch (ARG(1)) {
    case 88: case 90: VOLVER(2, 96);    // LOGPIXELSX/Y
    case 12: VOLVER(2, 32);             // BITSPIXEL
    case 14: VOLVER(2, 1);              // PLANES
    case 8: VOLVER(2, 1920);            // HORZRES
    case 10: VOLVER(2, 1080);           // VERTRES
    case 24: VOLVER(2, 0xffffffffu);    // NUMCOLORS
    case 38: VOLVER(2, 0x7e99);         // RASTERCAPS
    case 116: VOLVER(2, 60);            // VREFRESH
    default: VOLVER(2, 0);
  }
}
// Texto: métricas y glifos de la fuente elegida en el DC (gdi_real.c en Windows, caché en Android).
int gdi_metricas(const uint8_t lf[60], uint8_t tm[56]);
uint32_t gdi_glifo(const uint8_t lf[60], uint32_t ch, uint32_t formato, const uint8_t mat2[16], uint8_t gm[20], uint8_t *buf, uint32_t tam);
static const uint8_t *fuente_de(uint32_t hdc) {
  ObjG *dc = obj(hdc), *f = dc ? obj(dc->fuente) : NULL;
  return f && f->tipo == G_FUENTE ? f->logfont : NULL;
}
API(GetTextMetricsA) {
  const uint8_t *lf = fuente_de(ARG(0));
  uint8_t tm[56];
  if (!lf || !gdi_metricas(lf, tm)) VOLVER(2, 0);
  memcpy(MEM + ARG(1), tm, 56);
  VOLVER(2, 1);
}
API(GetGlyphOutlineA) {
  const uint8_t *lf = fuente_de(ARG(0));
  uint32_t tam = ARG(4), buf = ARG(5);
  if (!lf) VOLVER(7, 0xffffffffu);
  static uint32_t vistos[64];
  int32_t alto = (int32_t)(lf[0] | lf[1] << 8 | lf[2] << 16 | (uint32_t)lf[3] << 24);
  for (int i = 0; i < 64; i++) { if (vistos[i] == (uint32_t)alto) break; if (!vistos[i]) { vistos[i] = (uint32_t)alto; fprintf(stderr, "fuente en uso: alto %d, formato %u\n", alto, ARG(2)); break; } }
  uint8_t gm[20];
  uint32_t r = gdi_glifo(lf, ARG(1), ARG(2), MEM + ARG(6), gm, buf && tam ? MEM + buf : NULL, buf ? tam : 0);
  if (ARG(3)) memcpy(MEM + ARG(3), gm, 20);
  VOLVER(7, r);
}
API(GetObjectType) { ObjG *o = obj(ARG(0)); VOLVER(1, !o ? 0 : o->tipo == G_DC ? 3 : o->tipo == G_FUENTE ? 6 : o->tipo == G_BITMAP ? 7 : 2); }

const Exportada EXP_GDI32[] = {
  {"GetStockObject", GetStockObject}, {"CreateCompatibleDC", CreateCompatibleDC}, {"DeleteDC", DeleteDC},
  {"DeleteObject", DeleteObject}, {"SelectObject", SelectObject}, {"SetTextColor", SetTextColor},
  {"SetBkColor", SetBkColor}, {"CreateFontIndirectA", CreateFontIndirectA}, {"CreateFontA", CreateFontA},
  {"GetObjectType", GetObjectType}, {"GetDeviceCaps", GetDeviceCaps}, {"GetTextMetricsA", GetTextMetricsA},
  {"GetGlyphOutlineA", GetGlyphOutlineA},
  {0, 0},
};
