// KERNEL32: archivos, directorios, rutas e INI.
// El juego ve su carpeta como C:\HINOKAKERA\. Se lee de la carpeta de datos (de solo lectura: en Android,
// los assets del APK) salvo que el archivo ya exista en la de escritura, donde va todo lo que se escribe.
#include "win32.h"
#include <SDL3/SDL.h>
#include <ctype.h>
#include <stdlib.h>

#define RAIZ "C:\\HINOKAKERA"
static char dir_datos[512], dir_escritura[512];

const char *carpeta_capturas(void) { return dir_escritura; }

void rutas_iniciar(const char *datos, const char *escritura) {
  snprintf(dir_datos, sizeof dir_datos, "%s", datos);
  snprintf(dir_escritura, sizeof dir_escritura, "%s", escritura);
}

// Ruta relativa a la carpeta del juego, en minúsculas y con '/'. NULL si queda fuera.
static int relativa(const char *g, char *out, size_t tam) {
  char tmp[600];
  size_t n = 0;
  if (!g) return 0;
  if (!SDL_strncasecmp(g, RAIZ "\\", strlen(RAIZ) + 1)) g += strlen(RAIZ) + 1;
  else if (!SDL_strcasecmp(g, RAIZ)) g += strlen(RAIZ);
  else if (isalpha((unsigned char)g[0]) && g[1] == ':') return 0;
  for (; *g && n < sizeof tmp - 1; g++) tmp[n++] = *g == '\\' ? '/' : (char)tolower((unsigned char)*g);
  tmp[n] = 0;
  // Normaliza "./" y "x/../".
  char *partes[64], *estado = NULL; int np = 0;
  for (char *p = SDL_strtok_r(tmp, "/", &estado); p; p = SDL_strtok_r(NULL, "/", &estado)) {
    if (!strcmp(p, ".")) continue;
    if (!strcmp(p, "..")) { if (np) np--; continue; }
    if (np < 64) partes[np++] = p;
  }
  out[0] = 0;
  for (int i = 0; i < np; i++) { if (i) SDL_strlcat(out, "/", tam); SDL_strlcat(out, partes[i], tam); }
  return 1;
}

// En Android los datos son assets del APK (rutas relativas): ahí no hay GetPathInfo, se prueba abrir.
static int existe(const char *ruta) {
  if (SDL_GetPathInfo(ruta, NULL)) return 1;
  if (ruta[0] == '/' || (ruta[0] && ruta[1] == ':')) return 0;
  SDL_IOStream *io = SDL_IOFromFile(ruta, "rb");
  if (io) SDL_CloseIO(io);
  return io != NULL;
}

static void crear_padres(const char *ruta) {
  char tmp[600];
  snprintf(tmp, sizeof tmp, "%s", ruta);
  char *u = strrchr(tmp, '/');
  if (u) { *u = 0; SDL_CreateDirectory(tmp); }
}

static int copiar(const char *de, const char *a) {
  size_t n;
  void *d = SDL_LoadFile(de, &n);
  if (!d) return 0;
  crear_padres(a);
  int ok = SDL_SaveFile(a, d, n);
  SDL_free(d);
  return ok;
}

const char *ruta_host(const char *g, int escribir, char *buf, size_t tam) {
  char rel[600];
  if (!relativa(g, rel, sizeof rel)) return NULL;
  char en_escritura[700], en_datos[700];
  snprintf(en_escritura, sizeof en_escritura, "%s/%s", dir_escritura, rel);
  if (dir_datos[0]) snprintf(en_datos, sizeof en_datos, "%s/%s", dir_datos, rel);
  else snprintf(en_datos, sizeof en_datos, "%s", rel); // assets del APK
  if (existe(en_escritura) || escribir) {
    if (escribir == 2 && !existe(en_escritura) && existe(en_datos)) copiar(en_datos, en_escritura);
    if (escribir) crear_padres(en_escritura);
    snprintf(buf, tam, "%s", en_escritura);
  } else snprintf(buf, tam, "%s", en_datos);
  return buf;
}

// ---- handles de archivo ----
typedef struct { SDL_IOStream *io; } Archivo;
void archivo_cerrar(void *dato) { Archivo *a = dato; if (a->io) SDL_CloseIO(a->io); free(a); }

#define GENERIC_READ 0x80000000u
#define GENERIC_WRITE 0x40000000u
#define INVALIDO 0xffffffffu
// Handles estándar: STD_INPUT/OUTPUT/ERROR.
#define H_STDIN 0x0f00u
#define H_STDOUT 0x0f04u
#define H_STDERR 0x0f08u

API(CreateFileA) {
  const char *g = gstr(ARG(0));
  uint32_t acceso = ARG(1), disp = ARG(4);
  char buf[700];
  int escribe = (acceso & GENERIC_WRITE) != 0;
  const char *r = ruta_host(g, escribe ? (disp == 3 || disp == 4 ? 2 : 1) : 0, buf, sizeof buf);
  if (!r) { poner_error(ERROR_PATH_NOT_FOUND); VOLVER(7, INVALIDO); }
  int hay = existe(r);
  if (!hay && getenv("HINO_LOG_ARCHIVOS")) fprintf(stderr, "CreateFileA %s -> %s no existe (disp %u)\n", g, r, disp);
  const char *modo;
  switch (disp) {
    case 1: if (hay) { poner_error(80); VOLVER(7, INVALIDO); } modo = "w+b"; break; // CREATE_NEW
    case 2: modo = "w+b"; break;                                                    // CREATE_ALWAYS
    case 3: if (!hay) { poner_error(ERROR_FILE_NOT_FOUND); VOLVER(7, INVALIDO); } modo = escribe ? "r+b" : "rb"; break;
    case 4: modo = hay ? (escribe ? "r+b" : "rb") : "w+b"; break;                   // OPEN_ALWAYS
    case 5: if (!hay) { poner_error(ERROR_FILE_NOT_FOUND); VOLVER(7, INVALIDO); } modo = "w+b"; break;
    default: poner_error(ERROR_INVALID_PARAMETER); VOLVER(7, INVALIDO);
  }
  SDL_IOStream *io = SDL_IOFromFile(r, modo);
  if (getenv("HINO_LOG_ARCHIVOS")) fprintf(stderr, "CreateFileA %s -> %s (%s)%s\n", g, r, modo, io ? "" : " FALLA");
  if (!io) { poner_error(ERROR_ACCESS_DENIED); VOLVER(7, INVALIDO); }
  Archivo *a = calloc(1, sizeof *a);
  a->io = io;
  poner_error((disp == 2 || disp == 4) && hay ? ERROR_ALREADY_EXISTS : 0);
  VOLVER(7, objeto_nuevo(OBJ_ARCHIVO, a));
}

API(ReadFile) {
  // Prueba de carreras: HINO_LENTO=ms frena las lecturas de los hilos secundarios (la música lee el .ogg así).
  static int lento = -1;
  if (lento < 0) lento = getenv("HINO_LENTO") ? atoi(getenv("HINO_LENTO")) : 0;
  extern int es_hilo_principal(void);
  if (lento && !es_hilo_principal()) SDL_Delay((Uint32)lento);
  Archivo *a = objeto_dato(ARG(0), OBJ_ARCHIVO);
  uint32_t n = ARG(2), leidos = 0;
  if (a && n) leidos = (uint32_t)SDL_ReadIO(a->io, MEM + ARG(1), n);
  if (ARG(3)) M32(ARG(3)) = leidos;
  if (!a) { poner_error(ERROR_INVALID_HANDLE); VOLVER(5, 0); }
  VOLVER(5, 1);
}

API(WriteFile) {
  uint32_t h = ARG(0), n = ARG(2);
  if (h == H_STDOUT || h == H_STDERR) {
    fwrite(MEM + ARG(1), 1, n, stderr);
    if (ARG(3)) M32(ARG(3)) = n;
    VOLVER(5, 1);
  }
  Archivo *a = objeto_dato(h, OBJ_ARCHIVO);
  uint32_t escritos = a && n ? (uint32_t)SDL_WriteIO(a->io, MEM + ARG(1), n) : 0;
  if (ARG(3)) M32(ARG(3)) = escritos;
  if (!a) { poner_error(ERROR_INVALID_HANDLE); VOLVER(5, 0); }
  VOLVER(5, escritos == n);
}

API(SetFilePointer) {
  Archivo *a = objeto_dato(ARG(0), OBJ_ARCHIVO);
  if (!a) { poner_error(ERROR_INVALID_HANDLE); VOLVER(4, INVALIDO); }
  int64_t d = (int32_t)ARG(1);
  if (ARG(2)) d = (int64_t)(((uint64_t)M32(ARG(2)) << 32) | ARG(1));
  static const SDL_IOWhence desde[] = {SDL_IO_SEEK_SET, SDL_IO_SEEK_CUR, SDL_IO_SEEK_END};
  int64_t p = SDL_SeekIO(a->io, d, desde[ARG(3) % 3]);
  if (p < 0) { poner_error(131); VOLVER(4, INVALIDO); }
  if (ARG(2)) M32(ARG(2)) = (uint32_t)(p >> 32);
  poner_error(0);
  VOLVER(4, (uint32_t)p);
}

API(GetFileSize) {
  Archivo *a = objeto_dato(ARG(0), OBJ_ARCHIVO);
  if (!a) { poner_error(ERROR_INVALID_HANDLE); VOLVER(2, INVALIDO); }
  int64_t t = SDL_GetIOSize(a->io);
  if (ARG(1)) M32(ARG(1)) = (uint32_t)(t >> 32);
  VOLVER(2, (uint32_t)t);
}

API(FlushFileBuffers) { Archivo *a = objeto_dato(ARG(0), OBJ_ARCHIVO); if (a) SDL_FlushIO(a->io); VOLVER(1, 1); }
API(SetEndOfFile) { fprintf(stderr, "SetEndOfFile no implementado (se ignora)\n"); VOLVER(1, 1); }
API(LockFile) { VOLVER(5, 1); }
API(UnlockFile) { VOLVER(5, 1); }
API(GetFileTime) {
  for (int i = 1; i <= 3; i++) if (ARG(i)) { M32(ARG(i)) = 0xd53e8000u; M32(ARG(i) + 4) = 0x019db1deu; } // 2001-01-01
  VOLVER(4, 1);
}
API(SetFileTime) { VOLVER(4, 1); }
API(GetFileType) {
  uint32_t h = ARG(0);
  if (h == H_STDIN || h == H_STDOUT || h == H_STDERR) VOLVER(1, 2); // FILE_TYPE_CHAR
  VOLVER(1, objeto_dato(h, OBJ_ARCHIVO) ? 1 : 0);
}
API(GetStdHandle) {
  int32_t n = (int32_t)ARG(0);
  VOLVER(1, n == -10 ? H_STDIN : n == -11 ? H_STDOUT : n == -12 ? H_STDERR : INVALIDO);
}
API(SetStdHandle) { VOLVER(2, 1); }

API(GetFileAttributesA) {
  char buf[700];
  const char *r = ruta_host(gstr(ARG(0)), 0, buf, sizeof buf);
  SDL_PathInfo info;
  if (!r || !SDL_GetPathInfo(r, &info)) {
    if (r && existe(r)) VOLVER(1, 0x80); // archivo dentro del APK
    poner_error(ERROR_FILE_NOT_FOUND);
    VOLVER(1, INVALIDO);
  }
  VOLVER(1, info.type == SDL_PATHTYPE_DIRECTORY ? 0x10 : 0x80);
}
API(SetFileAttributesA) { VOLVER(2, 1); }
API(DeleteFileA) {
  char buf[700];
  const char *r = ruta_host(gstr(ARG(0)), 1, buf, sizeof buf);
  VOLVER(1, r && SDL_RemovePath(r));
}
API(MoveFileA) {
  char a[700], b[700];
  const char *ra = ruta_host(gstr(ARG(0)), 2, a, sizeof a), *rb = ruta_host(gstr(ARG(1)), 1, b, sizeof b);
  VOLVER(2, ra && rb && SDL_RenamePath(ra, rb));
}
API(CreateDirectoryA) {
  char buf[700];
  const char *r = ruta_host(gstr(ARG(0)), 1, buf, sizeof buf);
  VOLVER(2, r && SDL_CreateDirectory(r));
}

static uint32_t copiar_cadena(uint32_t dst, uint32_t tam, const char *s) {
  uint32_t n = (uint32_t)strlen(s);
  if (n + 1 > tam) return n + 1;
  memcpy(MEM + dst, s, n + 1);
  return n;
}
API(GetModuleFileNameA) {
  uint32_t n = copiar_cadena(ARG(1), ARG(2), RAIZ "\\hinokakera.exe");
  VOLVER(3, n > ARG(2) ? ARG(2) : n);
}
API(GetCurrentDirectoryA) { VOLVER(2, copiar_cadena(ARG(1), ARG(0), RAIZ)); }
API(GetFullPathNameA) {
  const char *g = gstr(ARG(0));
  char out[700];
  if (isalpha((unsigned char)g[0]) && g[1] == ':') snprintf(out, sizeof out, "%s", g);
  else snprintf(out, sizeof out, RAIZ "\\%s", g);
  uint32_t n = copiar_cadena(ARG(2), ARG(1), out);
  if (ARG(3) && n < ARG(1)) {
    char *u = strrchr(gstr(ARG(2)), '\\');
    M32(ARG(3)) = u ? ARG(2) + (uint32_t)(u + 1 - gstr(ARG(2))) : ARG(2);
  }
  VOLVER(4, n);
}
API(GetShortPathNameA) { VOLVER(3, copiar_cadena(ARG(1), ARG(2), gstr(ARG(0)))); }
API(GetTempFileNameA) {
  static int n;
  char s[300];
  snprintf(s, sizeof s, "%s\\%s%04x.tmp", gstr(ARG(0)), gstr(ARG(1)), ++n);
  copiar_cadena(ARG(3), 260, s);
  VOLVER(4, (uint32_t)n);
}
API(GetDiskFreeSpaceA) {
  uint32_t v[4] = {8, 512, 0x100000, 0x200000};
  for (int i = 0; i < 4; i++) if (ARG(i + 1)) M32(ARG(i + 1)) = v[i];
  VOLVER(5, 1);
}
API(GetDriveTypeA) { VOLVER(1, 3); }
API(GetVolumeInformationA) {
  if (ARG(1)) copiar_cadena(ARG(1), ARG(2), "HINOKAKERA");
  if (ARG(3)) M32(ARG(3)) = 0x12345678u;
  if (ARG(4)) M32(ARG(4)) = 255;
  if (ARG(5)) M32(ARG(5)) = 0x3;
  if (ARG(6)) copiar_cadena(ARG(6), ARG(7), "NTFS");
  VOLVER(8, 1);
}

// ---- búsquedas (FindFirstFileA) ----
typedef struct { char **nombres; int n, i; char dir[700]; } Busqueda;
void busqueda_cerrar(void *dato) {
  Busqueda *b = dato;
  for (int i = 0; i < b->n; i++) free(b->nombres[i]);
  free(b->nombres); free(b);
}
static int coincide(const char *p, const char *s) {
  if (!strcmp(p, "*.*")) return 1; // en Windows también abarca nombres sin punto
  for (; *p; p++, s++) {
    if (*p == '*') { for (const char *t = s; ; t++) { if (coincide(p + 1, t)) return 1; if (!*t) return 0; } }
    if (!*s || (*p != '?' && tolower((unsigned char)*p) != tolower((unsigned char)*s))) return 0;
  }
  return !*s;
}
typedef struct { Busqueda *b; const char *patron; } Junta;
static SDL_EnumerationResult juntar(void *ud, const char *dir, const char *nombre) {
  (void)dir;
  Junta *j = ud;
  if (!coincide(j->patron, nombre)) return SDL_ENUM_CONTINUE;
  for (int i = 0; i < j->b->n; i++) if (!SDL_strcasecmp(j->b->nombres[i], nombre)) return SDL_ENUM_CONTINUE;
  j->b->nombres = realloc(j->b->nombres, sizeof(char *) * (size_t)(j->b->n + 1));
  j->b->nombres[j->b->n++] = SDL_strdup(nombre);
  return SDL_ENUM_CONTINUE;
}
static int llenar_busqueda(Busqueda *b, uint32_t datos) {
  if (b->i >= b->n) return 0;
  const char *nombre = b->nombres[b->i++];
  char ruta[1400];
  snprintf(ruta, sizeof ruta, "%s/%s", b->dir, nombre);
  SDL_PathInfo info = {0};
  SDL_GetPathInfo(ruta, &info);
  memset(MEM + datos, 0, 318);
  M32(datos) = info.type == SDL_PATHTYPE_DIRECTORY ? 0x10 : 0x80;
  M32(datos + 32) = (uint32_t)info.size;
  copiar_cadena(datos + 44, 260, nombre);
  return 1;
}
API(FindFirstFileA) {
  const char *g = gstr(ARG(0));
  char rel[600], copia[600];
  snprintf(copia, sizeof copia, "%s", g);
  char *u = strrchr(copia, '\\');
  const char *patron = u ? u + 1 : copia;
  if (u) *u = 0; else copia[0] = 0;
  Busqueda *b = calloc(1, sizeof *b);
  Junta j = {b, patron};
  // Une lo que hay en la carpeta de escritura y en la de datos.
  for (int k = 0; k < 2; k++) {
    char dir[700];
    if (!relativa(copia[0] ? copia : RAIZ, rel, sizeof rel)) break;
    const char *base = k ? dir_datos : dir_escritura; // datos vacío = assets del APK (rutas relativas)
    snprintf(dir, sizeof dir, "%s%s%s", base, base[0] && rel[0] ? "/" : "", rel);
    if (!b->dir[0] || !k) snprintf(b->dir, sizeof b->dir, "%s", dir);
    SDL_EnumerateDirectory(dir, juntar, &j);
  }
  if (!llenar_busqueda(b, ARG(1))) { busqueda_cerrar(b); poner_error(ERROR_FILE_NOT_FOUND); VOLVER(2, INVALIDO); }
  VOLVER(2, objeto_nuevo(OBJ_BUSQUEDA, b));
}
API(FindNextFileA) {
  Busqueda *b = objeto_dato(ARG(0), OBJ_BUSQUEDA);
  if (!b || !llenar_busqueda(b, ARG(1))) { poner_error(ERROR_NO_MORE_FILES); VOLVER(2, 0); }
  VOLVER(2, 1);
}
API(FindClose) { VOLVER(1, objeto_cerrar(ARG(0))); }

// ---- INI ----
static char *leer_ini(const char *archivo) {
  char buf[700];
  const char *r = ruta_host(archivo, 0, buf, sizeof buf);
  return r ? SDL_LoadFile(r, NULL) : NULL;
}
static int buscar_ini(const char *txt, const char *sec, const char *clave, char *out, size_t tam) {
  int en = 0;
  for (const char *l = txt; l && *l; l = strchr(l, '\n') ? strchr(l, '\n') + 1 : NULL) {
    while (*l == ' ' || *l == '\t') l++;
    if (*l == '[') { const char *f = strchr(l, ']'); en = f && (size_t)(f - l - 1) == strlen(sec) && !SDL_strncasecmp(l + 1, sec, strlen(sec)); continue; }
    if (!en) continue;
    const char *ig = strchr(l, '=');
    const char *fin = strchr(l, '\n');
    if (!ig || (fin && ig > fin)) continue;
    const char *k = ig;
    while (k > l && (k[-1] == ' ' || k[-1] == '\t')) k--;
    if ((size_t)(k - l) != strlen(clave) || SDL_strncasecmp(l, clave, strlen(clave))) continue;
    ig++;
    size_t n = fin ? (size_t)(fin - ig) : strlen(ig);
    while (n && (ig[n - 1] == '\r' || ig[n - 1] == ' ')) n--;
    snprintf(out, tam, "%.*s", (int)n, ig);
    return 1;
  }
  return 0;
}
API(GetPrivateProfileStringA) {
  char val[1024];
  char *txt = leer_ini(gstr(ARG(5)));
  const char *res = txt && ARG(0) && ARG(1) && buscar_ini(txt, gstr(ARG(0)), gstr(ARG(1)), val, sizeof val) ? val : (ARG(2) ? gstr(ARG(2)) : "");
  uint32_t tam = ARG(4), n = (uint32_t)strlen(res);
  if (tam) { if (n >= tam) n = tam - 1; memcpy(MEM + ARG(3), res, n); M8(ARG(3) + n) = 0; }
  SDL_free(txt);
  VOLVER(6, n);
}
API(GetPrivateProfileIntA) {
  char val[64];
  char *txt = leer_ini(gstr(ARG(3)));
  uint32_t r = txt && buscar_ini(txt, gstr(ARG(0)), gstr(ARG(1)), val, sizeof val) ? (uint32_t)strtol(val, NULL, 10) : ARG(2);
  SDL_free(txt);
  VOLVER(4, r);
}
API(WritePrivateProfileStringA) {
  fprintf(stderr, "WritePrivateProfileStringA [%s] %s=%s en %s (se ignora)\n", gstr(ARG(0)), ARG(1) ? gstr(ARG(1)) : "", ARG(2) ? gstr(ARG(2)) : "", gstr(ARG(3)));
  VOLVER(4, 1);
}
API(GetProfileIntA) { VOLVER(3, ARG(2)); }

const Exportada EXP_K32_ARCHIVOS[] = {
  {"CreateFileA", CreateFileA}, {"ReadFile", ReadFile}, {"WriteFile", WriteFile}, {"SetFilePointer", SetFilePointer},
  {"GetFileSize", GetFileSize}, {"FlushFileBuffers", FlushFileBuffers}, {"SetEndOfFile", SetEndOfFile},
  {"LockFile", LockFile}, {"UnlockFile", UnlockFile}, {"GetFileTime", GetFileTime}, {"SetFileTime", SetFileTime},
  {"GetFileType", GetFileType}, {"GetStdHandle", GetStdHandle}, {"SetStdHandle", SetStdHandle},
  {"GetFileAttributesA", GetFileAttributesA}, {"SetFileAttributesA", SetFileAttributesA}, {"DeleteFileA", DeleteFileA},
  {"MoveFileA", MoveFileA}, {"CreateDirectoryA", CreateDirectoryA}, {"GetModuleFileNameA", GetModuleFileNameA},
  {"GetCurrentDirectoryA", GetCurrentDirectoryA}, {"GetFullPathNameA", GetFullPathNameA},
  {"GetShortPathNameA", GetShortPathNameA}, {"GetTempFileNameA", GetTempFileNameA},
  {"GetDiskFreeSpaceA", GetDiskFreeSpaceA}, {"GetDriveTypeA", GetDriveTypeA}, {"GetVolumeInformationA", GetVolumeInformationA},
  {"FindFirstFileA", FindFirstFileA}, {"FindNextFileA", FindNextFileA}, {"FindClose", FindClose},
  {"GetPrivateProfileStringA", GetPrivateProfileStringA}, {"GetPrivateProfileIntA", GetPrivateProfileIntA},
  {"WritePrivateProfileStringA", WritePrivateProfileStringA}, {"GetProfileIntA", GetProfileIntA},
  {0, 0},
};
