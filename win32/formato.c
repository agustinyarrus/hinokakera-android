// printf mínimo con los argumentos en la pila del invitado (wsprintfA): enteros, cadenas y caracteres.
#include "win32.h"

uint32_t formatear(const char *f, uint32_t args, char *out, size_t tam) {
  size_t k = 0;
  char tmp[64], spec[16];
  while (*f && k + 1 < tam) {
    if (*f != '%') { out[k++] = *f++; continue; }
    const char *ini = f++;
    if (*f == '%') { out[k++] = '%'; f++; continue; }
    while (*f && strchr("-+ #0", *f)) f++;
    while (*f >= '0' && *f <= '9') f++;
    if (*f == '.') { f++; while (*f >= '0' && *f <= '9') f++; }
    int largo = 0;
    while (*f == 'l' || *f == 'h') { largo = *f == 'l'; f++; }
    char conv = *f ? *f++ : 0;
    size_t n = (size_t)(f - ini);
    if (n >= sizeof spec - 2) n = sizeof spec - 2;
    memcpy(spec, ini, n);
    spec[n] = 0;
    // Saca los modificadores de largo: en el invitado todo es de 32 bits.
    char limpio[16]; size_t j = 0;
    for (size_t i = 0; i < n; i++) if (spec[i] != 'l' && spec[i] != 'h') limpio[j++] = spec[i];
    limpio[j] = 0;
    (void)largo;
    uint32_t v = M32(args);
    args += 4;
    switch (conv) {
      case 'd': case 'i': snprintf(tmp, sizeof tmp, limpio, (int)v); break;
      case 'u': case 'x': case 'X': case 'o': snprintf(tmp, sizeof tmp, limpio, v); break;
      case 'c': snprintf(tmp, sizeof tmp, limpio, (int)(v & 0xff)); break;
      case 's': {
        const char *s = v ? gstr(v) : "(null)";
        int w = snprintf(out + k, tam - k, limpio, s);
        k += w < 0 ? 0 : (size_t)w < tam - k ? (size_t)w : tam - k - 1;
        continue;
      }
      default: tmp[0] = 0;
    }
    for (const char *t = tmp; *t && k + 1 < tam; t++) out[k++] = *t;
  }
  out[k] = 0;
  return (uint32_t)k;
}
