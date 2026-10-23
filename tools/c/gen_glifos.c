// Caché de glifos para Android (allá no hay GDI): las mismas llamadas que hace el juego (GetGlyphOutlineA con
// GGO_GRAY4_BITMAP y la matriz {2,0,0,1}) para cada código Shift-JIS en las alturas que usa (18, 24, 28),
// con la GDI real de Windows. El nombre de la fuente se toma del exe para que el LOGFONT sea idéntico.
// Uso: gen_glifos.exe <hinokakera.exe> <salida glifos.bin>
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void escribir32(FILE *f, uint32_t v) { fwrite(&v, 4, 1, f); }

int main(int argc, char **argv) {
  if (argc < 3) return 2;
  // Nombre de la fuente: cadena en 0x568024 del exe (base 0x400000; .rdata/.data se mapean 1:1 en este exe).
  FILE *e = fopen(argv[1], "rb");
  if (!e) return 1;
  unsigned char cab[4096];
  fread(cab, 1, sizeof cab, e);
  uint32_t pe = *(uint32_t *)(cab + 0x3c), nsec = *(uint16_t *)(cab + pe + 6), opt = pe + 24, tamopt = *(uint16_t *)(cab + pe + 20);
  char cara[32] = {0};
  for (uint32_t s = 0; s < nsec; s++) {
    unsigned char *sh = cab + opt + tamopt + 40 * s;
    uint32_t va = *(uint32_t *)(sh + 12), vs = *(uint32_t *)(sh + 8), raw = *(uint32_t *)(sh + 20);
    if (0x168024 >= va && 0x168024 < va + vs) { fseek(e, raw + 0x168024 - va, SEEK_SET); fread(cara, 1, 31, e); }
  }
  fclose(e);
  if (!cara[0]) { fprintf(stderr, "no encontré el nombre de la fuente\n"); return 1; }

  static const int ALTOS[] = {18, 24, 28};
  FILE *f = fopen(argv[2], "wb");
  fwrite("GLF1", 1, 4, f);
  escribir32(f, 3);
  MAT2 m = {{0, 2}, {0, 0}, {0, 0}, {0, 1}};
  unsigned char *buf = malloc(1 << 20);
  for (int a = 0; a < 3; a++) {
    LOGFONTA lf = {0};
    lf.lfHeight = ALTOS[a];
    lf.lfWeight = 400;
    lf.lfCharSet = 0x80;
    memcpy(lf.lfFaceName, cara, sizeof lf.lfFaceName);
    HFONT fu = CreateFontIndirectA(&lf);
    HDC dc = CreateCompatibleDC(NULL);
    SelectObject(dc, fu);
    TEXTMETRICA tm;
    GetTextMetricsA(dc, &tm);
    fwrite(&lf, 1, 60, f);
    fwrite(&tm, 1, 56, f);
    // Códigos: un byte (0x20-0x7e, 0xa1-0xdf) y dos bytes (líder 0x81-0x9f/0xe0-0xfc, segundo 0x40-0xfc sin 0x7f).
    uint32_t codigos[16384], n = 0;
    for (uint32_t c = 0x20; c < 0x7f; c++) codigos[n++] = c;
    for (uint32_t c = 0xa1; c <= 0xdf; c++) codigos[n++] = c;
    for (uint32_t l = 0x81; l <= 0xfc; l++) {
      if (l > 0x9f && l < 0xe0) continue;
      for (uint32_t t = 0x40; t <= 0xfc; t++) if (t != 0x7f) codigos[n++] = (l << 8) | t;
    }
    escribir32(f, n);
    uint32_t validos = 0;
    for (uint32_t i = 0; i < n; i++) {
      GLYPHMETRICS gm;
      memset(&gm, 0, sizeof gm);
      DWORD tam = GetGlyphOutlineA(dc, codigos[i], GGO_GRAY4_BITMAP, &gm, 0, NULL, &m);
      if (tam != GDI_ERROR && tam > 0) tam = GetGlyphOutlineA(dc, codigos[i], GGO_GRAY4_BITMAP, &gm, 1 << 20, buf, &m);
      escribir32(f, codigos[i]);
      escribir32(f, tam);
      fwrite(&gm, 1, 20, f);
      if (tam != GDI_ERROR && tam > 0) { fwrite(buf, 1, tam, f); validos++; }
    }
    printf("alto %d: %u códigos, %u con imagen\n", ALTOS[a], n, validos);
    DeleteDC(dc);
    DeleteObject(fu);
  }
  fclose(f);
  return 0;
}
