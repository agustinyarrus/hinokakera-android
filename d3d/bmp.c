// Capturas: guarda un RGBA de arriba hacia abajo como BMP de 24 bits.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

void guardar_bmp(const char *ruta, const uint8_t *rgba, uint32_t w, uint32_t h) {
  uint32_t pitch = (w * 3 + 3) & ~3u, tam = 54 + pitch * h;
  uint8_t cab[54] = {'B', 'M'};
  #define P32(o, v) (cab[o] = (uint8_t)(v), cab[o + 1] = (uint8_t)((v) >> 8), cab[o + 2] = (uint8_t)((v) >> 16), cab[o + 3] = (uint8_t)((v) >> 24))
  P32(2, tam); P32(10, 54); P32(14, 40); P32(18, w); P32(22, h); cab[26] = 1; cab[28] = 24; P32(34, pitch * h);
  FILE *f = fopen(ruta, "wb");
  if (!f) return;
  fwrite(cab, 1, 54, f);
  uint8_t *fila = calloc(1, pitch);
  for (uint32_t y = 0; y < h; y++) {
    const uint8_t *s = rgba + (size_t)(h - 1 - y) * w * 4; // BMP va de abajo hacia arriba
    for (uint32_t x = 0; x < w; x++) { fila[3 * x] = s[4 * x + 2]; fila[3 * x + 1] = s[4 * x + 1]; fila[3 * x + 2] = s[4 * x]; }
    fwrite(fila, 1, pitch, f);
  }
  free(fila);
  fclose(f);
}
