// KERNEL32: versión, entorno, módulos, páginas de códigos, cadenas, hora, recursos, átomos y salida.
#include "win32.h"
#include <SDL3/SDL.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>

uint32_t modulo_por_nombre(const char *dll);                  // modulos.c
uint32_t direccion_exportada(uint32_t modulo, const char *nombre);

API(GetVersion) { VOLVER(0, 0x0a280105u); } // Windows XP 5.1.2600
API(GetProcessVersion) { VOLVER(1, 0x00040000u); }
API(GetThreadLocale) { VOLVER(0, 0x411); }
API(SetErrorMode) { VOLVER(1, 0); }
API(SetHandleCount) { VOLVER(1, ARG(0)); }

static uint32_t linea_comandos, entorno_a, entorno_w;
API(GetCommandLineA) { if (!linea_comandos) linea_comandos = gstrdup("\"" "C:\\HINOKAKERA\\hinokakera.exe" "\""); VOLVER(0, linea_comandos); }
API(GetStartupInfoA) { memset(MEM + ARG(0), 0, 68); M32(ARG(0)) = 68; VOLVER(1, 0); }
API(GetEnvironmentStrings) { if (!entorno_a) entorno_a = heap_alloc(heap_proceso(), 4, 1); VOLVER(0, entorno_a); }
API(GetEnvironmentStringsW) { if (!entorno_w) entorno_w = heap_alloc(heap_proceso(), 4, 1); VOLVER(0, entorno_w); }
API(FreeEnvironmentStringsA) { VOLVER(1, 1); }
API(FreeEnvironmentStringsW) { VOLVER(1, 1); }
API(SetEnvironmentVariableA) { VOLVER(2, 1); }

API(GetModuleHandleA) { VOLVER(1, ARG(0) ? modulo_por_nombre(gstr(ARG(0))) : 0x400000u); }
API(LoadLibraryA) {
  uint32_t m = modulo_por_nombre(gstr(ARG(0)));
  if (!m) { fprintf(stderr, "LoadLibraryA(%s): no disponible\n", gstr(ARG(0))); poner_error(126); }
  VOLVER(1, m);
}
API(FreeLibrary) { VOLVER(1, 1); }
API(GetProcAddress) {
  uint32_t n = ARG(1);
  char tmp[16];
  const char *nombre = n < 0x10000 ? (snprintf(tmp, sizeof tmp, "#%u", n), tmp) : gstr(n);
  uint32_t va = direccion_exportada(ARG(0), nombre);
  if (!va) { fprintf(stderr, "GetProcAddress(%08x, %s): no está\n", ARG(0), nombre); poner_error(127); }
  VOLVER(2, va);
}

// ---- páginas de códigos: el juego espera un Windows japonés (932) ----
API(GetACP) { VOLVER(0, 932); }
API(GetOEMCP) { VOLVER(0, 932); }
API(GetCPInfo) {
  uint32_t cp = ARG(0), p = ARG(1);
  memset(MEM + p, 0, 18);
  M8(p + 4) = '?';
  if (cp == 932 || cp == 0 || cp == 1 || cp == 3) {
    M32(p) = 2;
    M8(p + 6) = 0x81; M8(p + 7) = 0x9f; M8(p + 8) = 0xe0; M8(p + 9) = 0xfc;
  } else M32(p) = 1;
  VOLVER(2, 1);
}

uint16_t sjis_a_unicode(uint32_t c);      // cp932.c
uint32_t unicode_a_sjis(uint16_t u);
static int es_lider(uint8_t b) { return (b >= 0x81 && b <= 0x9f) || (b >= 0xe0 && b <= 0xfc); }

API(MultiByteToWideChar) {
  uint32_t src = ARG(2), dst = ARG(4), cap = ARG(5);
  int32_t n = (int32_t)ARG(3);
  if (n < 0) n = (int32_t)strlen(gstr(src)) + 1;
  uint32_t k = 0;
  for (int32_t i = 0; i < n; i++) {
    uint8_t b = M8(src + i);
    uint32_t u = b;
    if (b >= 0xa1 && b <= 0xdf) u = 0xff61 + (b - 0xa1u);
    else if (es_lider(b) && i + 1 < n) { u = sjis_a_unicode(((uint32_t)b << 8) | M8(src + i + 1)); i++; }
    if (cap) { if (k >= cap) { poner_error(ERROR_INSUFFICIENT_BUFFER); VOLVER(6, 0); } M16(dst + 2 * k) = (uint16_t)u; }
    k++;
  }
  VOLVER(6, k);
}
API(WideCharToMultiByte) {
  uint32_t src = ARG(2), dst = ARG(4), cap = ARG(5), usado = ARG(7);
  int32_t n = (int32_t)ARG(3);
  if (n < 0) { n = 0; while (M16(src + 2 * n)) n++; n++; }
  uint32_t k = 0;
  int defecto = 0;
  for (int32_t i = 0; i < n; i++) {
    uint16_t u = M16(src + 2 * i);
    uint32_t s = u < 0x80 ? u : (u >= 0xff61 && u <= 0xff9f) ? 0xa1u + (u - 0xff61u) : unicode_a_sjis(u);
    if (!s && u) { s = '?'; defecto = 1; }
    int largo = s > 0xff ? 2 : 1;
    if (cap) {
      if (k + (uint32_t)largo > cap) { poner_error(ERROR_INSUFFICIENT_BUFFER); VOLVER(8, 0); }
      if (largo == 2) { M8(dst + k) = (uint8_t)(s >> 8); M8(dst + k + 1) = (uint8_t)s; } else M8(dst + k) = (uint8_t)s;
    }
    k += (uint32_t)largo;
  }
  if (usado) M32(usado) = (uint32_t)defecto;
  VOLVER(8, k);
}

// Las versiones W de estas tres fallan como en Windows 9x: el CRT entonces usa las A.
API(CompareStringW) { poner_error(ERROR_CALL_NOT_IMPLEMENTED); VOLVER(6, 0); }
API(LCMapStringW) { poner_error(ERROR_CALL_NOT_IMPLEMENTED); VOLVER(6, 0); }
API(GetStringTypeW) { poner_error(ERROR_CALL_NOT_IMPLEMENTED); VOLVER(4, 0); }

API(CompareStringA) {
  const char *a = gstr(ARG(2)), *b = gstr(ARG(4));
  int32_t na = (int32_t)ARG(3), nb = (int32_t)ARG(5);
  if (na < 0) na = (int32_t)strlen(a);
  if (nb < 0) nb = (int32_t)strlen(b);
  int r = 0;
  for (int32_t i = 0; !r && i < na && i < nb; i++) {
    int x = (uint8_t)a[i], y = (uint8_t)b[i];
    if (ARG(1) & 1) { x = tolower(x); y = tolower(y); }
    r = (x > y) - (x < y);
  }
  if (!r) r = (na > nb) - (na < nb);
  VOLVER(6, 2 + r);
}
API(LCMapStringA) {
  uint32_t fl = ARG(1), src = ARG(2), dst = ARG(4), cap = ARG(5);
  int32_t n = (int32_t)ARG(3);
  if (n < 0) n = (int32_t)strlen(gstr(src)) + 1;
  if (!cap) VOLVER(6, n);
  if ((uint32_t)n > cap) { poner_error(ERROR_INSUFFICIENT_BUFFER); VOLVER(6, 0); }
  for (int32_t i = 0; i < n; i++) {
    uint8_t b = M8(src + i);
    if (b < 0x80) b = (uint8_t)(fl & 0x100 ? tolower(b) : fl & 0x200 ? toupper(b) : b);
    M8(dst + i) = b;
  }
  VOLVER(6, n);
}
static uint16_t tipo_c1(uint8_t b) {
  if (b >= 0x80) return 0;
  uint16_t t = 0;
  if (isupper(b)) t |= 0x1;
  if (islower(b)) t |= 0x2;
  if (isdigit(b)) t |= 0x4;
  if (isspace(b)) t |= 0x8;
  if (ispunct(b)) t |= 0x10;
  if (iscntrl(b)) t |= 0x20;
  if (b == ' ' || b == '\t') t |= 0x40;
  if (isxdigit(b)) t |= 0x80;
  if (isalpha(b)) t |= 0x100;
  return t;
}
static void tipos(uint32_t src, int32_t n, uint32_t dst) {
  if (n < 0) n = (int32_t)strlen(gstr(src)) + 1;
  for (int32_t i = 0; i < n; i++) M16(dst + 2 * i) = tipo_c1(M8(src + i));
}
API(GetStringTypeA) { tipos(ARG(2), (int32_t)ARG(3), ARG(4)); VOLVER(5, 1); }
API(GetStringTypeExA) { tipos(ARG(2), (int32_t)ARG(3), ARG(4)); VOLVER(5, 1); }

API(lstrlenA) { VOLVER(1, ARG(0) ? strlen(gstr(ARG(0))) : 0); }
API(lstrcpyA) { strcpy(gstr(ARG(0)), gstr(ARG(1))); VOLVER(2, ARG(0)); }
API(lstrcpynA) {
  uint32_t n = ARG(2);
  if (n) { SDL_strlcpy(gstr(ARG(0)), gstr(ARG(1)), n); }
  VOLVER(3, ARG(0));
}
API(lstrcatA) { strcat(gstr(ARG(0)), gstr(ARG(1))); VOLVER(2, ARG(0)); }
API(lstrcmpA) { int r = strcmp(gstr(ARG(0)), gstr(ARG(1))); VOLVER(2, (r > 0) - (r < 0)); }
API(lstrcmpiA) { int r = SDL_strcasecmp(gstr(ARG(0)), gstr(ARG(1))); VOLVER(2, (r > 0) - (r < 0)); }
API(MulDiv) {
  int64_t a = (int32_t)ARG(0), b = (int32_t)ARG(1), d = (int32_t)ARG(2);
  if (!d) VOLVER(3, 0xffffffffu);
  int64_t p = a * b, r = (p >= 0) == (d > 0) ? (p + d / 2) / d : (p - d / 2) / d;
  VOLVER(3, (uint32_t)(int32_t)r);
}

// ---- hora (FILETIME: unidades de 100 ns desde 1601) ----
static int64_t dias_desde_civil(int64_t y, unsigned m, unsigned d) {
  y -= m <= 2;
  int64_t era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = (unsigned)(y - era * 400), doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int64_t)doe - 719468;
}
static void civil_desde_dias(int64_t z, int *y, unsigned *m, unsigned *d) {
  z += 719468;
  int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  unsigned doe = (unsigned)(z - era * 146097), yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
  *d = doy - (153 * mp + 2) / 5 + 1;
  *m = mp < 10 ? mp + 3 : mp - 9;
  *y = (int)((int64_t)yoe + era * 400 + (*m <= 2));
}
#define EPOCA_1601 11644473600LL
static void a_systemtime(uint32_t p, int64_t seg, unsigned ms) {
  int64_t dias = seg >= 0 ? seg / 86400 : (seg - 86399) / 86400, resto = seg - dias * 86400;
  int y; unsigned m, d;
  civil_desde_dias(dias, &y, &m, &d);
  M16(p) = (uint16_t)y; M16(p + 2) = (uint16_t)m; M16(p + 4) = (uint16_t)((dias + 4) % 7 + 7) % 7; M16(p + 6) = (uint16_t)d;
  M16(p + 8) = (uint16_t)(resto / 3600); M16(p + 10) = (uint16_t)(resto / 60 % 60); M16(p + 12) = (uint16_t)(resto % 60); M16(p + 14) = (uint16_t)ms;
}
static int64_t desde_systemtime(uint32_t p, unsigned *ms) {
  *ms = M16(p + 14);
  return dias_desde_civil((int16_t)M16(p), M16(p + 2), M16(p + 6)) * 86400 + M16(p + 8) * 3600 + M16(p + 10) * 60 + M16(p + 12);
}
static int64_t desfase_local(void) {
  time_t t = time(NULL);
  struct tm l = *localtime(&t), g = *gmtime(&t);
  return (int64_t)difftime(mktime(&l), mktime(&g));
}
API(GetSystemTime) { a_systemtime(ARG(0), (int64_t)time(NULL), (unsigned)(SDL_GetTicks() % 1000)); VOLVER(1, 0); }
API(GetLocalTime) { a_systemtime(ARG(0), (int64_t)time(NULL) + desfase_local(), (unsigned)(SDL_GetTicks() % 1000)); VOLVER(1, 0); }
API(GetTimeZoneInformation) {
  memset(MEM + ARG(0), 0, 172);
  M32(ARG(0)) = (uint32_t)(int32_t)(-desfase_local() / 60);
  VOLVER(1, 0);
}
API(SystemTimeToFileTime) {
  unsigned ms;
  int64_t s = desde_systemtime(ARG(0), &ms);
  uint64_t ft = (uint64_t)(s + EPOCA_1601) * 10000000ull + ms * 10000ull;
  M64(ARG(1)) = ft;
  VOLVER(2, 1);
}
API(FileTimeToSystemTime) {
  uint64_t ft = M64(ARG(0));
  a_systemtime(ARG(1), (int64_t)(ft / 10000000ull) - EPOCA_1601, (unsigned)(ft / 10000ull % 1000));
  VOLVER(2, 1);
}
API(FileTimeToLocalFileTime) { M64(ARG(1)) = M64(ARG(0)) + (uint64_t)(desfase_local() * 10000000LL); VOLVER(2, 1); }
API(LocalFileTimeToFileTime) { M64(ARG(1)) = M64(ARG(0)) - (uint64_t)(desfase_local() * 10000000LL); VOLVER(2, 1); }

// ---- errores y depuración ----
API(FormatMessageA) {
  char txt[64];
  snprintf(txt, sizeof txt, "Error %u.\r\n", ARG(2));
  uint32_t n = (uint32_t)strlen(txt);
  if (ARG(0) & 0x100) { uint32_t va = gstrdup(txt); M32(ARG(4)) = va; } // FORMAT_MESSAGE_ALLOCATE_BUFFER
  else if (ARG(5) > n) memcpy(MEM + ARG(4), txt, n + 1);
  VOLVER(7, n);
}
API(OutputDebugStringA) { fprintf(stderr, "[debug] %s", gstr(ARG(0))); VOLVER(1, 0); }
API(DebugBreak) { fallar(c, M32(c->esp), "DebugBreak"); }
API(RaiseException) {
  uint32_t codigo = ARG(0), n = ARG(2), args = ARG(3);
  fprintf(stderr, "RaiseException %08x (%u argumentos)\n", codigo, n);
  if (codigo == 0xe06d7363u && n >= 3) {
    // Excepción de C++: args[2] -> ThrowInfo -> CatchableTypeArray -> CatchableType -> TypeDescriptor.
    uint32_t ti = M32(args + 8), cta = M32(ti + 12), ct = M32(cta + 4), td = M32(ct + 4);
    fprintf(stderr, "  excepción de C++ de tipo %s\n", gstr(td + 8));
  }
  fallar(c, M32(c->esp), "excepción (todavía no se despachan)");
}
API(UnhandledExceptionFilter) { VOLVER(1, 0); }
API(SetUnhandledExceptionFilter) { VOLVER(1, 0); }
API(ExitProcess) { fprintf(stderr, "ExitProcess(%u)\n", ARG(0)); fflush(NULL); exit((int)ARG(0)); }
API(TerminateProcess) {
  if (ARG(0) == 0xffffffffu) { fprintf(stderr, "TerminateProcess(%u)\n", ARG(1)); fflush(NULL); exit((int)ARG(1)); }
  VOLVER(2, 0);
}
API(FatalAppExitA) { fprintf(stderr, "FatalAppExit: %s\n", gstr(ARG(1))); fflush(NULL); exit(1); }

// ---- átomos ----
#define MAX_ATOMOS 256
static char *atomos[MAX_ATOMOS];
static uint32_t atomo(const char *s, int crear) {
  if ((uintptr_t)s < 0x10000) return (uint32_t)(uintptr_t)s;
  if (s[0] == '#') return (uint32_t)strtoul(s + 1, NULL, 10);
  for (int i = 0; i < MAX_ATOMOS; i++) if (atomos[i] && !SDL_strcasecmp(atomos[i], s)) return 0xc000u + (uint32_t)i;
  if (!crear) return 0;
  for (int i = 0; i < MAX_ATOMOS; i++) if (!atomos[i]) { atomos[i] = SDL_strdup(s); return 0xc000u + (uint32_t)i; }
  return 0;
}
static const char *nombre_o_atomo(uint32_t va) { return va < 0x10000 ? (const char *)(uintptr_t)va : gstr(va); }
API(GlobalAddAtomA) { VOLVER(1, atomo(nombre_o_atomo(ARG(0)), 1)); }
API(GlobalFindAtomA) { VOLVER(1, atomo(nombre_o_atomo(ARG(0)), 0)); }
API(GlobalDeleteAtom) { VOLVER(1, 0); }
API(GlobalGetAtomNameA) {
  uint32_t a = ARG(0), k = a - 0xc000u;
  char tmp[16];
  const char *s = a >= 0xc000u && k < MAX_ATOMOS && atomos[k] ? atomos[k] : (snprintf(tmp, sizeof tmp, "#%u", a), tmp);
  SDL_strlcpy(gstr(ARG(1)), s, ARG(2));
  VOLVER(3, strlen(gstr(ARG(1))));
}

// ---- recursos del exe (directorio .rsrc de la imagen ya cargada) ----
static uint32_t base_rsrc(void) {
  uint32_t pe = 0x400000u + M32(0x40003cu);
  uint32_t rva = M32(pe + 24 + 96 + 2 * 8);
  return rva ? 0x400000u + rva : 0;
}
static int nombre_igual(uint32_t raiz, uint32_t entrada, uint32_t buscado) {
  uint32_t n = M32(entrada);
  if (buscado < 0x10000) return !(n & 0x80000000u) && n == buscado;
  const char *s = gstr(buscado);
  if (s[0] == '#') return !(n & 0x80000000u) && n == strtoul(s + 1, NULL, 10);
  if (!(n & 0x80000000u)) return 0;
  uint32_t str = raiz + (n & 0x7fffffffu), largo = M16(str);
  if (largo != strlen(s)) return 0;
  for (uint32_t i = 0; i < largo; i++) if (toupper((uint8_t)s[i]) != toupper(M16(str + 2 + 2 * i) & 0xff)) return 0;
  return 1;
}
static uint32_t buscar_entrada(uint32_t raiz, uint32_t dir, uint32_t buscado) {
  uint32_t n = M16(dir + 12) + M16(dir + 14);
  for (uint32_t i = 0; i < n; i++) {
    uint32_t e = dir + 16 + 8 * i;
    if (buscado == 0xffffffffu || nombre_igual(raiz, e, buscado)) return e;
  }
  return 0;
}
// IMAGE_RESOURCE_DATA_ENTRY del recurso (tipo -> nombre -> primer idioma), o 0.
static uint32_t entrada_recurso(uint32_t nombre, uint32_t tipo) {
  uint32_t raiz = base_rsrc();
  if (!raiz) return 0;
  uint32_t e = buscar_entrada(raiz, raiz, tipo);
  if (e && (M32(e + 4) & 0x80000000u)) e = buscar_entrada(raiz, raiz + (M32(e + 4) & 0x7fffffffu), nombre);
  if (e && (M32(e + 4) & 0x80000000u)) e = buscar_entrada(raiz, raiz + (M32(e + 4) & 0x7fffffffu), 0xffffffffu);
  return e && !(M32(e + 4) & 0x80000000u) ? raiz + M32(e + 4) : 0;
}
uint32_t recurso(uint32_t nombre, uint32_t tipo, uint32_t *tam) {
  uint32_t de = entrada_recurso(nombre, tipo);
  if (de && tam) *tam = M32(de + 4);
  return de ? 0x400000u + M32(de) : 0;
}
API(FindResourceA) {
  uint32_t de = entrada_recurso(ARG(1), ARG(2));
  if (!de) poner_error(1813); // ERROR_RESOURCE_TYPE_NOT_FOUND
  VOLVER(3, de); // el handle es la IMAGE_RESOURCE_DATA_ENTRY
}
API(LoadResource) { VOLVER(2, ARG(1) ? 0x400000u + M32(ARG(1)) : 0); }
API(SizeofResource) { VOLVER(2, ARG(1) ? M32(ARG(1) + 4) : 0); }
API(LockResource) { VOLVER(1, ARG(0)); }
API(FreeResource) { VOLVER(1, 0); }

const Exportada EXP_K32_VARIOS[] = {
  {"GetVersion", GetVersion}, {"GetProcessVersion", GetProcessVersion}, {"GetThreadLocale", GetThreadLocale},
  {"SetErrorMode", SetErrorMode}, {"SetHandleCount", SetHandleCount}, {"GetCommandLineA", GetCommandLineA},
  {"GetStartupInfoA", GetStartupInfoA}, {"GetEnvironmentStrings", GetEnvironmentStrings},
  {"GetEnvironmentStringsW", GetEnvironmentStringsW}, {"FreeEnvironmentStringsA", FreeEnvironmentStringsA},
  {"FreeEnvironmentStringsW", FreeEnvironmentStringsW}, {"SetEnvironmentVariableA", SetEnvironmentVariableA},
  {"GetModuleHandleA", GetModuleHandleA}, {"LoadLibraryA", LoadLibraryA}, {"FreeLibrary", FreeLibrary},
  {"GetProcAddress", GetProcAddress}, {"GetACP", GetACP}, {"GetOEMCP", GetOEMCP}, {"GetCPInfo", GetCPInfo},
  {"MultiByteToWideChar", MultiByteToWideChar}, {"WideCharToMultiByte", WideCharToMultiByte},
  {"CompareStringW", CompareStringW}, {"LCMapStringW", LCMapStringW}, {"GetStringTypeW", GetStringTypeW},
  {"CompareStringA", CompareStringA}, {"LCMapStringA", LCMapStringA}, {"GetStringTypeA", GetStringTypeA},
  {"GetStringTypeExA", GetStringTypeExA}, {"lstrlenA", lstrlenA}, {"lstrcpyA", lstrcpyA}, {"lstrcpynA", lstrcpynA},
  {"lstrcatA", lstrcatA}, {"lstrcmpA", lstrcmpA}, {"lstrcmpiA", lstrcmpiA}, {"MulDiv", MulDiv},
  {"GetSystemTime", GetSystemTime}, {"GetLocalTime", GetLocalTime}, {"GetTimeZoneInformation", GetTimeZoneInformation},
  {"SystemTimeToFileTime", SystemTimeToFileTime}, {"FileTimeToSystemTime", FileTimeToSystemTime},
  {"FileTimeToLocalFileTime", FileTimeToLocalFileTime}, {"LocalFileTimeToFileTime", LocalFileTimeToFileTime},
  {"FormatMessageA", FormatMessageA}, {"OutputDebugStringA", OutputDebugStringA}, {"DebugBreak", DebugBreak},
  {"RaiseException", RaiseException}, {"UnhandledExceptionFilter", UnhandledExceptionFilter},
  {"SetUnhandledExceptionFilter", SetUnhandledExceptionFilter}, {"ExitProcess", ExitProcess},
  {"TerminateProcess", TerminateProcess}, {"FatalAppExitA", FatalAppExitA},
  {"GlobalAddAtomA", GlobalAddAtomA}, {"GlobalFindAtomA", GlobalFindAtomA}, {"GlobalDeleteAtom", GlobalDeleteAtom},
  {"GlobalGetAtomNameA", GlobalGetAtomNameA}, {"FindResourceA", FindResourceA}, {"LoadResource", LoadResource},
  {"SizeofResource", SizeofResource}, {"LockResource", LockResource}, {"FreeResource", FreeResource},
  {0, 0},
};
