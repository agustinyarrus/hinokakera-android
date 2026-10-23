// Genera win32/cp932.c con la tabla de Windows (MultiByteToWideChar, página 932).
#include <windows.h>
#include <stdio.h>
int main(void) {
  FILE *f = fopen("win32/cp932.c", "w");
  fprintf(f, "// Generado por tools/c/gen_cp932.c con MultiByteToWideChar(932) de Windows. No editar.\n");
  fprintf(f, "#include <stdint.h>\n#include <stdlib.h>\n\n");
  fprintf(f, "static const uint16_t TABLA[60][189] = {\n");
  for (int l = 0; l < 60; l++) {
    int lider = l < 31 ? 0x81 + l : 0xe0 + (l - 31);
    fprintf(f, "  {");
    for (int t = 0x40; t <= 0xfc; t++) {
      unsigned char b[2] = {(unsigned char)lider, (unsigned char)t};
      WCHAR w = 0;
      int n = MultiByteToWideChar(932, MB_ERR_INVALID_CHARS, (char *)b, 2, &w, 1);
      fprintf(f, "%u,", n == 1 ? w : 0);
    }
    fprintf(f, "},\n");
  }
  fprintf(f, "};\n\n");
  fprintf(f, "static int fila(uint32_t lider) { return lider >= 0x81 && lider <= 0x9f ? (int)lider - 0x81 : lider >= 0xe0 && lider <= 0xfc ? (int)lider - 0xe0 + 31 : -1; }\n");
  fprintf(f, "uint16_t sjis_a_unicode(uint32_t c) {\n  int l = fila(c >> 8), t = (int)(c & 0xff);\n  if (l < 0 || t < 0x40 || t > 0xfc) return 0x30fb;\n  return TABLA[l][t - 0x40] ? TABLA[l][t - 0x40] : 0x30fb;\n}\n\n");
  fprintf(f, "// Inversa: la primera aparición gana, como hace WideCharToMultiByte.\nstatic uint16_t *inversa;\n");
  fprintf(f, "uint32_t unicode_a_sjis(uint16_t u) {\n  if (!inversa) {\n    uint16_t *t = calloc(65536, sizeof *t);\n    for (int l = 59; l >= 0; l--) for (int k = 188; k >= 0; k--) if (TABLA[l][k]) t[TABLA[l][k]] = (uint16_t)(((l < 31 ? 0x81 + l : 0xe0 + l - 31) << 8) | (0x40 + k));\n    inversa = t;\n  }\n  return inversa[u];\n}\n");
  fclose(f);
  return 0;
}
