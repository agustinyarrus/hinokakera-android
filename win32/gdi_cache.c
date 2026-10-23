// Glifos sin GDI (Android, o Windows con -DHINO_CACHE_GLIFOS para probarlo): se leen de glifos.bin, que
// genera tools/c/gen_glifos.c con la GDI real y los mismos parámetros que usa el juego.
#if !defined(_WIN32) || defined(HINO_CACHE_GLIFOS)
#include <SDL3/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint32_t codigo, tam; const uint8_t *gm, *datos; } Glifo;
typedef struct { int32_t alto; const uint8_t *lf, *tm; uint32_t n; Glifo *g; } Fuente;
static Fuente fuentes[8];
static int n_fuentes, cargado;

const char *ruta_recurso(const char *nombre, char *buf, size_t tam); // principal.c

static void cargar(void) {
  cargado = 1;
  char ruta[1024];
  size_t tam;
  uint8_t *d = SDL_LoadFile(ruta_recurso("glifos.bin", ruta, sizeof ruta), &tam);
  if (!d || tam < 8 || memcmp(d, "GLF1", 4)) { fprintf(stderr, "glifos.bin no disponible: el texto no se va a ver\n"); return; }
  const uint8_t *p = d + 8, *fin = d + tam;
  uint32_t nf = *(const uint32_t *)(d + 4);
  for (uint32_t i = 0; i < nf && i < 8 && p + 120 <= fin; i++) {
    Fuente *f = &fuentes[n_fuentes++];
    f->lf = p; f->tm = p + 60; memcpy(&f->alto, p, 4); memcpy(&f->n, p + 116, 4);
    p += 120;
    f->g = calloc(f->n, sizeof(Glifo));
    for (uint32_t k = 0; k < f->n && p + 28 <= fin; k++) {
      Glifo *g = &f->g[k];
      memcpy(&g->codigo, p, 4); memcpy(&g->tam, p + 4, 4); g->gm = p + 8; p += 28;
      g->datos = p;
      if (g->tam != 0xffffffffu) p += g->tam;
    }
  }
}

static Glifo *buscar(Fuente *f, uint32_t c) {
  uint32_t lo = 0, hi = f->n; // los códigos están ordenados de menor a mayor
  while (lo < hi) { uint32_t m = (lo + hi) / 2; if (f->g[m].codigo < c) lo = m + 1; else hi = m; }
  return lo < f->n && f->g[lo].codigo == c ? &f->g[lo] : NULL;
}

static Fuente *fuente(const uint8_t lf[60]) {
  if (!cargado) cargar();
  int32_t alto;
  memcpy(&alto, lf, 4);
  // El nombre se compara hasta el cero: el juego deja basura después en el LOGFONT.
  for (int i = 0; i < n_fuentes; i++)
    if (fuentes[i].alto == alto && !strncmp((const char *)fuentes[i].lf + 28, (const char *)lf + 28, 32)) return &fuentes[i];
  static int avisado;
  if (!avisado++) fprintf(stderr, "glifos: fuente de alto %d sin caché\n", alto);
  return NULL;
}

int gdi_metricas(const uint8_t lf[60], uint8_t tm[56]) {
  Fuente *f = fuente(lf);
  if (!f) return 0;
  memcpy(tm, f->tm, 56);
  return 1;
}

uint32_t gdi_glifo(const uint8_t lf[60], uint32_t ch, uint32_t formato, const uint8_t mat2[16], uint8_t gm[20], uint8_t *buf, uint32_t tam) {
  static const uint8_t MAT[16] = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0};
  Fuente *f = fuente(lf);
  Glifo *g = f && formato == 5 && !memcmp(mat2, MAT, 16) ? buscar(f, ch) : NULL;
  if (!g) { fprintf(stderr, "glifos: sin %04x (formato %u)\n", ch, formato); return 0xffffffffu; }
  memcpy(gm, g->gm, 20);
  if (g->tam != 0xffffffffu && buf && tam >= g->tam) memcpy(buf, g->datos, g->tam);
  return g->tam;
}
#endif
