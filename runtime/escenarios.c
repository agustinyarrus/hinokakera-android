// Lo que necesitan los escenarios agregados (tools/escenario/mods.mjs) del lado del juego, por los ganchos de
// analisis/ganchos.txt.
//
// Desniveles (Eclipse Caldera, tools/escenario/caldera.mjs): columnas a las que los luchadores se suben de un salto.
// El juego tiene el piso en y = 0: compara la altura con 0 para saber si alguien está parado, lo deja en 0 al caer y
// pone el escenario alrededor del centro de la pelea. Los ganchos de analisis/ganchos.txt cambian eso:
// - el piso en (x, z) es la tapa más alta de las columnas que hay ahí (o 0), y las comparaciones de la altura con 0
//   (o con 8, el mínimo de algunos ataques en el aire) se hacen contra ese piso;
// - una columna más alta que el luchador es pared (se lo saca para afuera) y la arena tiene un radio;
// - los modelos del escenario que se llaman mod_w* quedan fijos en el mundo en vez de seguir a la pelea.
// Las columnas salen de dat/suelo.dat (en vsobj2.dat): "sue1", escenarios, y por cada uno el número de escenario,
// el radio de la arena (0 = sin borde), cuántas columnas y cada una (x, z, radio, alto). Unidades del juego: los
// luchadores miden ~20, saltan ~16.5 y los modelos del escenario van a escala 20.
#include "cpu.h"
#include "../win32/win32.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <stdlib.h>

#define PASO 4.0f    // una tapa hasta esto más arriba de los pies es escalón, no pared
#define CUERPO 3.0f  // radio del luchador contra las paredes
#define MAX_COL 64
#define MAX_ESC 8
#define MAX_OBJ 16

typedef struct { float x, z, r, alto; } Columna;
typedef struct { uint32_t etapa, n; float radio; Columna col[MAX_COL]; } Terreno;

static Terreno terrenos[MAX_ESC];
static int n_terrenos = -1;    // -1: falta leer dat/suelo.dat
static const Terreno *activo;  // el del escenario cargado (NULL: piso plano, como el original)
static uint32_t centro_fijo;   // vector del invitado (0, -0.5, 0): el centro que ven los modelos fijos
static float suelo_guardado;   // del gancho que compara al que pisa la altura justo después

// ---- dat/suelo.dat desde los .dat del juego (el mismo orden de búsqueda que el juego) ----
static uint8_t *leer_entrada(const char *archivo, const char *nombre, uint32_t *tam) {
  char g[64], buf[600];
  snprintf(g, sizeof g, "C:\\HINOKAKERA\\%s", archivo);
  const char *r = ruta_host(g, 0, buf, sizeof buf);
  SDL_IOStream *io = r ? SDL_IOFromFile(r, "rb") : NULL;
  if (!io) return NULL;
  uint8_t *dato = NULL, cab[8];
  if (SDL_ReadIO(io, cab, 8) == 8) {
    uint32_t n = cab[4] | cab[5] << 8 | cab[6] << 16 | (uint32_t)cab[7] << 24;
    for (uint32_t i = 0; i < n; i++) {
      uint32_t l, off, t;
      char nom[300];
      if (SDL_ReadIO(io, &l, 4) != 4 || l >= sizeof nom || SDL_ReadIO(io, nom, l) != l) break;
      nom[l] = 0;
      if (SDL_ReadIO(io, &off, 4) != 4 || SDL_ReadIO(io, &t, 4) != 4) break;
      if (strcmp(nom, nombre)) continue;
      dato = malloc(t ? t : 1);
      if (SDL_SeekIO(io, off, SDL_IO_SEEK_SET) < 0 || SDL_ReadIO(io, dato, t) != t) { free(dato); dato = NULL; }
      else *tam = t;
      break;
    }
  }
  SDL_CloseIO(io);
  return dato;
}

static void cargar(void) {
  n_terrenos = 0;
  static const char *archivos[] = {"vsobj2.dat", "vsobj.dat", "vsdat.dat"};
  uint32_t tam = 0;
  uint8_t *d = NULL;
  for (int i = 0; i < 3 && !d; i++) d = leer_entrada(archivos[i], "dat/suelo.dat", &tam);
  if (!d) return;
  // Todo son u32 y f32 little endian, como en x86 y ARM: se copian tal cual.
  const uint8_t *p = d + 8, *fin = d + tam;
  uint32_t n = 0;
  if (tam >= 8 && !memcmp(d, "sue1", 4)) memcpy(&n, d + 4, 4);
  for (uint32_t e = 0; e < n && n_terrenos < MAX_ESC && p + 12 <= fin; e++) {
    Terreno *t = &terrenos[n_terrenos];
    memcpy(&t->etapa, p, 4); memcpy(&t->radio, p + 4, 4); memcpy(&t->n, p + 8, 4);
    p += 12;
    if (t->n > MAX_COL || p + sizeof(Columna) * t->n > fin) break;
    memcpy(t->col, p, sizeof(Columna) * t->n);
    p += sizeof(Columna) * t->n;
    n_terrenos++;
  }
  free(d);
  fprintf(stderr, "desniveles: %d escenarios en dat/suelo.dat\n", n_terrenos);
}

// ---- el terreno ----
// Tapa más alta debajo de (x, z) a la que se puede estar parado desde la altura y (0 si no hay).
static float suelo(float x, float z, float y) {
  float g = 0;
  if (!activo) return 0;
  for (uint32_t k = 0; k < activo->n; k++) {
    const Columna *c = &activo->col[k];
    float dx = x - c->x, dz = z - c->z;
    if (dx * dx + dz * dz <= c->r * c->r && c->alto <= y + PASO && c->alto > g) g = c->alto;
  }
  return g;
}
// Saca (x, z) de las columnas que a la altura y son pared y lo deja adentro de la arena. 1 si lo movió.
static int resolver(float *x, float *z, float y) {
  int cambio = 0;
  if (!activo) return 0;
  for (int vuelta = 0; vuelta < 4; vuelta++) {
    int toco = 0;
    for (uint32_t k = 0; k < activo->n; k++) {
      const Columna *c = &activo->col[k];
      if (c->alto <= y + PASO) continue;
      float dx = *x - c->x, dz = *z - c->z, R = c->r + CUERPO, d2 = dx * dx + dz * dz;
      if (d2 >= R * R) continue;
      float d = sqrtf(d2);
      if (d < 1e-3f) { dx = 1; dz = 0; d = 1; }
      *x = c->x + dx / d * (R + 0.01f); *z = c->z + dz / d * (R + 0.01f);
      toco = 1;
    }
    float d = sqrtf(*x * *x + *z * *z), R = activo->radio;
    if (R > 0 && d > R) { *x *= R / d; *z *= R / d; toco = 1; }
    if (!toco) break;
    cambio = 1;
  }
  return cambio;
}

// Objeto del juego con la posición en +A4 (x, y, z).
#define POS_X(o) MF32((o) + 0xa4)
#define POS_Y(o) MF32((o) + 0xa8)
#define POS_Z(o) MF32((o) + 0xac)
#define mem MEM

// Altura de cada objeto al entrar a la física (f_0046b600): cae sobre una tapa si la cruzó en este cuadro.
static struct { uint32_t obj; float y; } entrada[MAX_OBJ];
static float y_entrada(uint32_t o, float y) {
  for (int i = 0; i < MAX_OBJ; i++) if (entrada[i].obj == o) return entrada[i].y;
  return y;
}

// ---- ganchos (analisis/ganchos.txt) ----
// Carga de field.dat (0x4984b0, tercer argumento: el número de escenario).
void g_escenario(Ctx *c) {
  if (n_terrenos < 0) cargar();
  uint32_t etapa = M32(c->esp + 12);
  activo = NULL;
  for (int i = 0; i < n_terrenos; i++) if (terrenos[i].etapa == etapa) activo = &terrenos[i];
  if (activo) fprintf(stderr, "desniveles: escenario %u, %u columnas, arena de radio %g\n", etapa, activo->n, activo->radio);
}

// En el lazo que lleva cada modelo del escenario al centro de la pelea (0x498c22, call 004059C0 con el centro ya
// en la pila): los mod_w* reciben (0, -0.5, 0) y quedan fijos en el mundo.
void g_modelo_fijo(Ctx *c) {
  const char *nombre = (const char *)(MEM + M32(c->esi + 0x5c) + c->ebp);
  if (strncmp(nombre, "mod_w", 5)) return;
  if (!centro_fijo) {
    centro_fijo = mem_bloque(16);
    MF32(centro_fijo + 4) = -0.5f;
  }
  M32(c->esp) = centro_fijo;
}

// Física (f_0046b600, this en ecx): se anota la altura con que entra.
void g_fisica_entra(Ctx *c) {
  int libre = -1;
  for (int i = 0; i < MAX_OBJ; i++) {
    if (entrada[i].obj == c->ecx) { libre = i; break; }
    if (libre < 0 && !entrada[i].obj) libre = i;
  }
  if (libre < 0) libre = (int)(c->ecx >> 4) % MAX_OBJ;
  entrada[libre].obj = c->ecx; entrada[libre].y = POS_Y(c->ecx);
}

// La comparación de la altura (en ST0) con 0 pasa a ser contra el piso de ese lugar.
static void comparar_con_suelo(Ctx *c, uint32_t o) {
  if (!activo) return;
  suelo_guardado = suelo(POS_X(o), POS_Z(o), POS_Y(o));
  ST(0) -= suelo_guardado;
}
void g_y_esi(Ctx *c) { comparar_con_suelo(c, c->esi); }
void g_y_ecx(Ctx *c) { comparar_con_suelo(c, c->ecx); }

// La caída de la física (0x46bb00): la altura ya avanzó; se cae sobre una tapa que estaba debajo al empezar el cuadro.
void g_y_cae(Ctx *c) {
  if (!activo) return;
  uint32_t o = c->esi;
  float y = POS_Y(o), ye = y_entrada(o, y);
  suelo_guardado = suelo(POS_X(o), POS_Z(o), ye > y ? ye : y);
  ST(0) -= suelo_guardado;
}
// Antes de un "altura = 0" suelto (0x468c70, después de copiar otra posición): el piso de la posición nueva.
void g_y_mide(Ctx *c) { if (activo) suelo_guardado = suelo(POS_X(c->esi), POS_Z(c->esi), POS_Y(c->esi)); }
// Después de un "altura = 0": al piso medido (si quedó dentro de una columna, la física lo saca después).
void g_y_pisa(Ctx *c) { if (activo) POS_Y(c->esi) = suelo_guardado; }

// Sombras (0x4062d5, call D3DXMatrixShadow(out, luz, plano) con los argumentos ya en la pila; ebx es el modelo, con
// su posición en +0C): el plano y = 0 pasa a la tapa que el modelo tiene debajo.
void g_sombra(Ctx *c) {
  if (!activo) return;
  uint32_t m = c->ebx;
  MF32(M32(c->esp + 8) + 12) = suelo(MF32(m + 0xc), MF32(m + 0x14), MF32(m + 0x10));
}

// Antes de pasar la posición al modelo (0x46bb79 en la física, this en esi) y al terminar SetPosition (0x46f786,
// this en ecx): las columnas que son pared sacan al luchador y la arena lo frena en el borde.
static void choque(uint32_t o, uint32_t movido) {
  if (!activo) return;
  float x = POS_X(o), z = POS_Z(o);
  if (resolver(&x, &z, POS_Y(o))) { POS_X(o) = x; POS_Z(o) = z; M32(o + movido) = 1; }
}
void g_choque_fisica(Ctx *c) { choque(c->esi, 0x10c); }
void g_choque_posicion(Ctx *c) { choque(c->ecx, 0x10c); }
