// Glifos de la GDI de verdad (solo en Windows): el juego pide GetTextMetricsA/GetGlyphOutlineA y acá se hacen
// las mismas llamadas con la misma fuente, así el texto sale idéntico al original. En Android se usa el
// caché que genera tools/c/gen_glifos.c con esta misma GDI.
#if defined(_WIN32) && !defined(HINO_CACHE_GLIFOS)
#include <windows.h>
#include <stdint.h>
#include <string.h>

typedef struct { uint8_t lf[60]; HFONT f; HDC dc; } FuenteReal;
static FuenteReal fuentes[64];
static int n_fuentes;

static HDC dc_de(const uint8_t lf[60]) {
  for (int i = 0; i < n_fuentes; i++) if (!memcmp(fuentes[i].lf, lf, 60)) return fuentes[i].dc;
  if (n_fuentes >= 64) return NULL;
  FuenteReal *r = &fuentes[n_fuentes++];
  memcpy(r->lf, lf, 60);
  LOGFONTA l;
  memcpy(&l, lf, sizeof l);
  r->f = CreateFontIndirectA(&l);
  r->dc = CreateCompatibleDC(NULL);
  SelectObject(r->dc, r->f);
  return r->dc;
}

int gdi_metricas(const uint8_t lf[60], uint8_t tm[56]) {
  HDC dc = dc_de(lf);
  TEXTMETRICA t;
  if (!dc || !GetTextMetricsA(dc, &t)) return 0;
  memcpy(tm, &t, 56);
  return 1;
}

uint32_t gdi_glifo(const uint8_t lf[60], uint32_t ch, uint32_t formato, const uint8_t mat2[16], uint8_t gm[20], uint8_t *buf, uint32_t tam) {
  HDC dc = dc_de(lf);
  GLYPHMETRICS g;
  MAT2 m;
  memcpy(&m, mat2, 16);
  DWORD r = dc ? GetGlyphOutlineA(dc, ch, formato, &g, tam, tam ? buf : NULL, &m) : GDI_ERROR;
  memcpy(gm, &g, 20);
  return r;
}
#endif
