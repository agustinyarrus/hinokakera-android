// Lobby de salas (win32/salas.c), dibujado dentro de la imagen del juego encima de su pantalla de red y con las cosas
// de esa pantalla: el fondo (tex_sys19), el título, las palabras, los estados y la barra que late (tex_sys79), las
// ayudas y la letra de los nombres para lo nuevo (tex_sala.bmt, de tools/sala/letras.mjs, en vsobj2.dat) y la MS
// Gothic de 24 con sombra del texto del juego, en los mismos lugares. Va antes de las capturas, así sale en ellas.
// Se maneja con las teclas del juego (jugador 1 y 2 de config.dat), las flechas, Enter y Escape, un mando, los
// controles táctiles o tocando las filas; los nombres se escriben con el teclado (en Android, el de pantalla).
#include "d3d9_int.h"
#include "../win32/salas.h"
#include "../win32/teclas.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

const char *ruta_host(const char *g, int escribir, char *buf, size_t tam); // k32_archivos.c
uint32_t gdi_glifo(const uint8_t lf[60], uint32_t ch, uint32_t formato, const uint8_t mat2[16], uint8_t gm[20], uint8_t *buf, uint32_t tam);
uint32_t cuadros_presentados(void);
extern uint8_t teclas_virtuales[256];
extern int raton_x, raton_y;
extern uint32_t raton_botones;
extern void (*oyente_eventos)(const SDL_Event *e);

// ---------------- texturas del juego (dat1 + BMT_, como tools/dat1.mjs y tools/escenario/bmt.mjs) ----------------
static uint8_t *de_dat(const char *nombre, size_t *tam) {
  static const char *const DAT[] = {"C:\\HINOKAKERA\\vsobj2.dat", "C:\\HINOKAKERA\\vsobj.dat"}; // el orden del juego
  for (int k = 0; k < 2; k++) {
    char ruta[700];
    const char *r = ruta_host(DAT[k], 0, ruta, sizeof ruta);
    SDL_IOStream *io = r ? SDL_IOFromFile(r, "rb") : NULL;
    if (!io) continue;
    uint8_t cab[8];
    uint32_t n = 0;
    if (SDL_ReadIO(io, cab, 8) == 8 && !memcmp(cab, "dat1", 4)) memcpy(&n, cab + 4, 4);
    for (uint32_t i = 0; i < n; i++) {
      uint32_t l, ofs, t;
      char nom[260];
      if (SDL_ReadIO(io, &l, 4) != 4 || l >= sizeof nom || SDL_ReadIO(io, nom, l) != l || SDL_ReadIO(io, &ofs, 4) != 4 || SDL_ReadIO(io, &t, 4) != 4) break;
      nom[l] = 0; // el nombre puede venir con ceros de relleno
      if (SDL_strcasecmp(nom, nombre)) continue;
      uint8_t *d = t < (64u << 20) ? malloc(t + 1) : NULL;
      if (d && SDL_SeekIO(io, ofs, SDL_IO_SEEK_SET) >= 0 && SDL_ReadIO(io, d, t) == t) {
        d[t] = 0;
        SDL_CloseIO(io);
        *tam = t;
        return d;
      }
      free(d);
      break;
    }
    SDL_CloseIO(io);
  }
  fprintf(stderr, "sala: no encontré %s en los datos\n", nombre);
  return NULL;
}

// BMT_: u16 ancho, u16 alto, u8 n, n bytes = colores - 1, paleta ARGB y un índice de n bytes por píxel. Sale RGBA
// con el alfa multiplicado (se mezcla con ONE, ONE_MINUS_SRC_ALPHA y el filtro lineal no deja bordes oscuros).
static GLuint textura_bmt(const char *nombre, int *w, int *h, GLint envoltura) {
  size_t n = 0;
  uint8_t *b = de_dat(nombre, &n);
  if (!b || n < 9 || memcmp(b, "BMT_", 4)) { free(b); return 0; }
  int W = b[4] | b[5] << 8, H = b[6] | b[7] << 8, k = b[8];
  uint32_t colores = 0;
  for (int i = 0; i < k && i < 4; i++) colores |= (uint32_t)b[9 + i] << (8 * i);
  colores++;
  size_t pal = 9 + (size_t)k, ind = pal + (size_t)colores * 4;
  if (k < 1 || k > 4 || W <= 0 || H <= 0 || ind + (size_t)W * H * k > n) { free(b); return 0; }
  uint8_t *o = malloc((size_t)W * H * 4);
  for (size_t i = 0; i < (size_t)W * H; i++) {
    uint32_t x = 0, c;
    for (int j = 0; j < k; j++) x |= (uint32_t)b[ind + i * k + j] << (8 * j);
    memcpy(&c, b + pal + (x < colores ? x : 0) * 4, 4);
    uint32_t a = c >> 24;
    o[4 * i] = (uint8_t)(((c >> 16) & 255) * a / 255); o[4 * i + 1] = (uint8_t)(((c >> 8) & 255) * a / 255);
    o[4 * i + 2] = (uint8_t)((c & 255) * a / 255); o[4 * i + 3] = (uint8_t)a;
  }
  free(b);
  GLuint t;
  glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, o);
  free(o);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, envoltura);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  *w = W; *h = H;
  return t;
}

// ---------------- dibujo: cuadriláteros en coordenadas del juego (800x600, el modo ancho corre la x) ----------------
enum { T_SOLIDO, T_FONDO, T_RED, T_LETRAS, T_GOTICA, T_N };
typedef struct { float x, y, u, v, r, g, b, a, t; } Vertice;
static Vertice vtx[6 * 1500];
static int nv, listo, corrido; // corrido: columnas del modo ancho a la izquierda del juego
static GLuint prog, vao, vbo, tex[T_N];
static GLint u_tam;
static int tw[T_N], th[T_N];

static void quad(int t, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, const float c[4]) {
  if (nv + 6 > (int)SDL_arraysize(vtx)) return;
  float a = c[3], r = c[0] * a, g = c[1] * a, b = c[2] * a; // color con el alfa multiplicado
  const float X[4] = {x0, x1, x1, x0}, Y[4] = {y0, y0, y1, y1}, U[4] = {u0, u1, u1, u0}, V[4] = {v0, v0, v1, v1};
  static const int ORDEN[6] = {0, 1, 2, 0, 2, 3};
  for (int k = 0; k < 6; k++) {
    int i = ORDEN[k];
    vtx[nv++] = (Vertice){X[i] + (float)corrido, Y[i], U[i], V[i], r, g, b, a, (float)t};
  }
}
static const float BLANCO[4] = {1, 1, 1, 1};
// Un pedazo de una textura del juego a tamaño natural (o estirado a w x h).
static void pedazo(int t, float sx, float sy, float sw, float sh, float x, float y, float w, float h, const float c[4]) {
  quad(t, x, y, x + w, y + h, sx / (float)tw[t], sy / (float)th[t], (sx + sw) / (float)tw[t], (sy + sh) / (float)th[t], c);
}
// Palabras de la pantalla de red (tex_sys79), en sus rectángulos.
typedef struct { int x, y, w, h; } Rect;
static const Rect TITULO = {0, 0, 256, 40}, PAGE = {0, 40, 64, 30}, SERVER_OFF = {110, 40, 140, 30}, SERVER_ON = {110, 70, 140, 30},
                  PORT = {110, 100, 80, 30}, DELAY = {110, 130, 80, 30}, UNKNOWN = {0, 70, 100, 30}, STANDBY = {0, 100, 100, 30},
                  BUSY = {0, 130, 100, 30};
static void palabra(Rect r, float x, float y, float alfa) {
  float c[4] = {1, 1, 1, alfa};
  pedazo(T_RED, (float)r.x, (float)r.y, (float)r.w, (float)r.h, x, y, (float)r.w, (float)r.h, c);
}

// La letra de los nombres (sala.txt).
typedef struct { int ax, ay, w, h, cx, cy; float sw; } Letra;
static Letra letras[128];
static float separacion = 0.5f, espacio = 8;
static float ancho_letras(const char *s, float k) {
  float x = 0;
  for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
    if (*p == ' ') { x += espacio * k; continue; }
    const Letra *g = &letras[*p == 0xb7 ? '`' : *p & 127]; // el punto del medio va en el lugar de '`'
    if (g->w) x += (g->sw + separacion) * k;
  }
  return x;
}
static float escribir(const char *s, float x, float base, float k, const float c[4]) {
  for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
    if (*p == ' ') { x += espacio * k; continue; }
    const Letra *g = &letras[*p == 0xb7 ? '`' : *p & 127];
    if (!g->w) continue;
    quad(T_LETRAS, x + (float)g->cx * k, base + (float)g->cy * k, x + (float)(g->cx + g->w) * k, base + (float)(g->cy + g->h) * k,
         (float)g->ax / (float)tw[T_LETRAS], (float)g->ay / (float)th[T_LETRAS], (float)(g->ax + g->w) / (float)tw[T_LETRAS],
         (float)(g->ay + g->h) / (float)th[T_LETRAS], c);
    x += (g->sw + separacion) * k;
  }
  return x;
}
#define PUNTOS "\xb7\xb7\xb7" // "Button C···Create Room", como las ayudas del juego
static const float BLANCO_AYUDA[4] = {1, 1, 1, 1};
static void ayuda(const char *t, int linea) { escribir(t, 552, 36.0f + 20.0f * (float)linea, 0.8f, BLANCO_AYUDA); } // arriba a la derecha

// MS Gothic de 24 con la matriz {2,0,0,1} del juego (glifos al doble de ancho, dibujados a la mitad), como su texto.
#define GOT_ALTO 24
#define GOT_W 1024
#define GOT_H 128 // cuatro filas de 32
typedef struct { float u0, v0, u1, v1; int w, h, ox, oy, avance; } Gotica;
static Gotica gotica[128];
static void armar_gotica(void) {
  uint8_t lf[60] = {0}, gm[20], mat[16] = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0};
  int32_t alto = GOT_ALTO, peso = 400;
  memcpy(lf, &alto, 4); memcpy(lf + 16, &peso, 4); lf[23] = 0x80;
  memcpy(lf + 28, MEM + 0x568024, 31); // el nombre de la fuente, tal cual está en el exe
  uint8_t *pix = calloc(GOT_W * GOT_H, 1);
  static uint8_t buf[16384];
  int x = 0, y = 0;
  for (uint32_t ch = 0x21; ch < 0x7f; ch++) {
    uint32_t n = gdi_glifo(lf, ch, 5, mat, gm, buf, sizeof buf);
    Gotica *g = &gotica[ch];
    int16_t av;
    memcpy(&g->w, gm, 4); memcpy(&g->h, gm + 4, 4); memcpy(&g->ox, gm + 8, 4); memcpy(&g->oy, gm + 12, 4); memcpy(&av, gm + 16, 2);
    g->avance = av;
    if (x + g->w > GOT_W) { x = 0; y += 32; }
    if (n == 0xffffffffu || !n || y + 32 > GOT_H || g->h > 32) { g->w = 0; continue; }
    int paso = (g->w + 3) & ~3; // filas alineadas a 4 bytes, 17 niveles (0..16)
    for (int f = 0; f < g->h; f++)
      for (int c = 0; c < g->w; c++) pix[(y + f) * GOT_W + x + c] = (uint8_t)SDL_min(255, buf[f * paso + c] * 255 / 16);
    g->u0 = (float)x / GOT_W; g->u1 = (float)(x + g->w) / GOT_W; g->v0 = (float)y / GOT_H; g->v1 = (float)(y + g->h) / GOT_H;
    x += g->w + 2;
  }
  gotica[' '].avance = gotica['a'].avance ? gotica['a'].avance : 24;
  glGenTextures(1, &tex[T_GOTICA]);
  glBindTexture(GL_TEXTURE_2D, tex[T_GOTICA]);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, GOT_W, GOT_H, 0, GL_RED, GL_UNSIGNED_BYTE, pix);
  free(pix);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}
static void gotica_pasada(const char *s, float x, float base, const float c[4]) {
  for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
    const Gotica *g = &gotica[*p & 127];
    if (g->w) {
      float x0 = x + (float)g->ox * 0.5f, y0 = base - (float)g->oy;
      quad(T_GOTICA, x0, y0, x0 + (float)g->w * 0.5f, y0 + (float)g->h, g->u0, g->v0, g->u1, g->v1, c);
    }
    x += (float)g->avance * 0.5f;
  }
}
static void texto(const char *s, float x, float base, const float c[4]) {
  float sombra[4] = {0, 0, 0, c[3] * 0.85f};
  gotica_pasada(s, x + 1, base + 1, sombra);
  gotica_pasada(s, x, base, c);
}
static void numero(int n, float x, float base, const float c[4]) { char t[16]; snprintf(t, sizeof t, "%d", n); escribir(t, x, base, 1, c); }

static const char *VS =
    "#version 300 es\n"
    "layout(location = 0) in vec2 a_p;\n"
    "layout(location = 1) in vec2 a_uv;\n"
    "layout(location = 2) in vec4 a_col;\n"
    "layout(location = 3) in float a_t;\n"
    "uniform vec4 u_tam;\n"
    "out vec2 v_uv; out vec4 v_col; flat out int v_t;\n"
    "void main() {\n"
    "  v_uv = a_uv; v_col = a_col; v_t = int(a_t + 0.5);\n"
    "  gl_Position = vec4(a_p.x / u_tam.x * 2.0 - 1.0, a_p.y / u_tam.y * 2.0 - 1.0, 0.0, 1.0);\n" // fila 0 arriba, como el backbuffer
    "}\n";
static const char *FS =
    "#version 300 es\n"
    "precision mediump float;\n"
    "in vec2 v_uv; in vec4 v_col; flat in int v_t;\n"
    "uniform sampler2D u_fondo, u_red, u_letras, u_gotica;\n"
    "out vec4 o;\n"
    "void main() {\n"
    "  vec4 c = vec4(1.0);\n"
    "  if (v_t == 1) c = texture(u_fondo, v_uv);\n"
    "  else if (v_t == 2) c = texture(u_red, v_uv);\n"
    "  else if (v_t == 3) c = texture(u_letras, v_uv);\n"
    "  else if (v_t == 4) c = vec4(texture(u_gotica, v_uv).r);\n"
    "  o = c * v_col;\n"
    "}\n";

static void leer_medidas(void) {
  size_t n = 0;
  char *t = (char *)de_dat("obj/sala.txt", &n);
  int nl = 0;
  for (char *l = t, *sig; l && *l; l = sig) {
    sig = strchr(l, '\n');
    if (sig) *sig++ = 0;
    int c, ax, ay, w, h, cx, cy;
    float sw;
    if (sscanf(l, "letras %d %f %f", &c, &separacion, &espacio) == 3) continue;
    if (sscanf(l, "%d %d %d %d %d %d %d %f", &c, &ax, &ay, &w, &h, &cx, &cy, &sw) == 8 && c > 0 && c < 256) {
      letras[c == 0xb7 ? '`' : c & 127] = (Letra){ax, ay, w, h, cx, cy, sw};
      nl++;
    }
  }
  free(t);
  fprintf(stderr, "sala: %d letras del juego\n", nl);
}

static void crear_gl(void) {
  listo = -1;
  prog = glCreateProgram();
  glAttachShader(prog, compilar(GL_VERTEX_SHADER, VS));
  glAttachShader(prog, compilar(GL_FRAGMENT_SHADER, FS));
  glLinkProgram(prog);
  GLint ok;
  glGetProgramiv(prog, GL_LINK_STATUS, &ok);
  if (!ok) { char log[2048]; glGetProgramInfoLog(prog, sizeof log, NULL, log); fprintf(stderr, "sala: %s\n", log); return; }
  u_tam = glGetUniformLocation(prog, "u_tam");
  glUseProgram(prog);
  static const char *const MUESTRA[] = {"u_fondo", "u_red", "u_letras", "u_gotica"};
  for (int i = 0; i < 4; i++) glUniform1i(glGetUniformLocation(prog, MUESTRA[i]), i + 1);
  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);
  glGenBuffers(1, &vbo);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  static const int TAM[4] = {2, 2, 4, 1}, DESDE[4] = {0, 8, 16, 32};
  for (int i = 0; i < 4; i++) {
    glEnableVertexAttribArray((GLuint)i);
    glVertexAttribPointer((GLuint)i, TAM[i], GL_FLOAT, GL_FALSE, sizeof(Vertice), (const void *)(uintptr_t)DESDE[i]);
  }
  glBindVertexArray(0);
  tex[T_FONDO] = textura_bmt("obj/tex_sys19.bmt", &tw[T_FONDO], &th[T_FONDO], GL_MIRRORED_REPEAT); // en el modo ancho se refleja
  tex[T_RED] = textura_bmt("obj/tex_sys79.bmt", &tw[T_RED], &th[T_RED], GL_CLAMP_TO_EDGE);
  tex[T_LETRAS] = textura_bmt("obj/tex_sala.bmt", &tw[T_LETRAS], &th[T_LETRAS], GL_CLAMP_TO_EDGE);
  leer_medidas();
  armar_gotica();
  if (!tex[T_FONDO] || !tex[T_RED] || !tex[T_LETRAS]) fprintf(stderr, "sala: faltan texturas (sin mods/vsobj2.dat?): el lobby sale incompleto\n");
  listo = 1;
}

// ---------------- entrada ----------------
enum { B_IZQ, B_DER, B_ARR, B_ABA, B_A, B_B, B_C, B_D, B_N };
static uint8_t dik[2][B_N + 1] = {{0x22, 0x24, 0x15, 0x23, 0x2c, 0x2d, 0x2e, 0x2f, 0x3b}, {0xcb, 0xcd, 0xc8, 0xd0, 0x33, 0x34, 0x35, 0x2b, 0x3c}};
static int antes[B_N], apretado[B_N];
static uint32_t desde[B_N], repetido[B_N];
static SDL_Gamepad *mandos[4];

static void leer_config(void) {
  char buf[700];
  size_t n = 0;
  const char *r = ruta_host("C:\\HINOKAKERA\\config.dat", 0, buf, sizeof buf);
  uint8_t *cfg = r ? SDL_LoadFile(r, &n) : NULL;
  if (cfg && n >= 0xb4 && !memcmp(cfg, "cfg_", 4))
    for (int j = 0; j < 2; j++) for (int k = 0; k <= B_N; k++) { uint32_t v; memcpy(&v, cfg + 0x50 + 0x40 * j + 4 * k, 4); if (v && v < 256) dik[j][k] = (uint8_t)v; }
  SDL_free(cfg);
}

static int tecla_abajo(const bool *k, uint8_t d) {
  for (const Tecla *t = TECLAS; t->sdl; t++) if (t->dik == d && k[t->sdl]) return 1;
  return 0;
}
// Escribiendo un nombre, las teclas del teclado son letras: valen Enter, Escape y las flechas (y los botones táctiles,
// los del mando y el guion de pruebas, que van por teclas_virtuales).
static void leer_botones(int letras_son_botones) {
  const bool *k = SDL_GetKeyboardState(NULL);
  int b[B_N] = {0};
  for (int j = 0; j < 2; j++) {
    for (int i = 0; i < B_N; i++) b[i] |= (letras_son_botones && tecla_abajo(k, dik[j][i])) || teclas_virtuales[dik[j][i]];
    b[B_B] |= (letras_son_botones && tecla_abajo(k, dik[j][B_N])) || teclas_virtuales[dik[j][B_N]]; // Pausa (el Atrás de Android) = B
  }
  b[B_IZQ] |= k[SDL_SCANCODE_LEFT]; b[B_DER] |= k[SDL_SCANCODE_RIGHT]; b[B_ARR] |= k[SDL_SCANCODE_UP]; b[B_ABA] |= k[SDL_SCANCODE_DOWN];
  b[B_A] |= k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_KP_ENTER]; b[B_B] |= k[SDL_SCANCODE_ESCAPE];
  int n = 0;
  SDL_JoystickID *ids = SDL_GetGamepads(&n);
  for (int i = 0; i < n && i < 4; i++) {
    if (!mandos[i] || SDL_GetGamepadID(mandos[i]) != ids[i]) mandos[i] = SDL_OpenGamepad(ids[i]);
    SDL_Gamepad *g = mandos[i];
    if (!g) continue;
    static const SDL_GamepadButton M[B_N] = {SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN,
                                             SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH};
    for (int j = 0; j < B_N; j++) b[j] |= SDL_GetGamepadButton(g, M[j]);
    int ex = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTX), ey = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTY);
    b[B_IZQ] |= ex < -20000; b[B_DER] |= ex > 20000; b[B_ARR] |= ey < -20000; b[B_ABA] |= ey > 20000;
  }
  SDL_free(ids);
  uint32_t ahora = (uint32_t)SDL_GetTicks();
  for (int i = 0; i < B_N; i++) { // flanco, y las direcciones se repiten si se mantienen
    apretado[i] = b[i] && !antes[i];
    if (apretado[i]) { desde[i] = ahora; repetido[i] = ahora; }
    else if (b[i] && i <= B_ABA && ahora - desde[i] > 400 && ahora - repetido[i] > 110) { apretado[i] = 1; repetido[i] = ahora; }
    antes[i] = b[i];
  }
}

// ---------------- pantallas ----------------
// Formularios con el teclado (en Android, el de pantalla): ajustes (nombre y servidor), crear sala (nombre y clave) y
// la clave de una sala privada.
enum { F_NADA, F_AJUSTES, F_CREAR, F_CLAVE };
static int form, campo, n_campos, sala_clave, sel, toque_fila = -2, toque_previo;
static char valor[2][64];
static size_t limite[2];
static SDL_Window *ventana;
static void (*oyente_anterior)(const SDL_Event *e);

static void abrir(int f) {
  form = f;
  campo = 0;
  memset(valor, 0, sizeof valor);
  if (f == F_AJUSTES) {
    n_campos = 2; limite[0] = 16; limite[1] = 47;
    SDL_strlcpy(valor[0], salas.nombre, sizeof valor[0]);
    SDL_strlcpy(valor[1], salas.servidor, sizeof valor[1]);
    campo = !salas.servidor[0] && salas.estado != SL_BUSCANDO ? 1 : 0;
  } else if (f == F_CREAR) {
    n_campos = 2; limite[0] = 24; limite[1] = 16;
    snprintf(valor[0], sizeof valor[0], "%s's room", salas.nombre);
  } else if (f == F_CLAVE) {
    n_campos = 1; limite[0] = 16;
  }
  if (ventana) { if (f) SDL_StartTextInput(ventana); else SDL_StopTextInput(ventana); }
}
// Lo que se escribe va al campo del formulario abierto (también desde HINO_TEXTO, en dinput8.c).
void sala_escribir(const char *t) {
  if (!form) return;
  for (const unsigned char *p = (const unsigned char *)t; *p; p++) {
    size_t l = strlen(valor[campo]);
    if (*p >= 0x20 && *p < 0x7f && *p != '\t' && l < limite[campo]) { valor[campo][l] = (char)*p; valor[campo][l + 1] = 0; }
  }
}
static void evento(const SDL_Event *e) {
  if (oyente_anterior) oyente_anterior(e);
  if (!form || !salas.visible) return;
  if (e->type == SDL_EVENT_TEXT_INPUT) sala_escribir(e->text.text);
  else if (e->type == SDL_EVENT_KEY_DOWN) {
    size_t l = strlen(valor[campo]);
    if (e->key.scancode == SDL_SCANCODE_BACKSPACE && l) valor[campo][l - 1] = 0;
    if (e->key.scancode == SDL_SCANCODE_TAB) campo = (campo + 1) % n_campos;
  }
}

static int fila_tocada(void) { // un toque (o clic) que se suelta sobre una fila: 0..7, -1 la línea del nombre
  int ahora = (raton_botones & SDL_BUTTON_LMASK) != 0, f = -2;
  if (ahora && !toque_previo) {
    if (raton_y >= 100 && raton_y < 128) toque_fila = -1;
    else toque_fila = raton_y >= 172 && raton_y < 172 + 45 * 8 ? (raton_y - 172) / 45 : -2;
  }
  if (!ahora && toque_previo) f = toque_fila;
  toque_previo = ahora;
  return f;
}

static const float DORADO[4] = {1, 0.75f, 0, 1}, GRIS[4] = {0.75f, 0.75f, 0.75f, 1}; // los mensajes del juego van en dorado

static void barra(int fila) { // la barra blanca que late, de borde a borde (en el modo ancho también)
  float a = (sinf((float)cuadros_presentados() * 0.1f) + 2) / 8, c[4] = {1, 1, 1, a}, y = 198.0f + 45.0f * (float)fila;
  quad(T_RED, -(float)corrido, y - 40, 800.0f + (float)corrido, y + 40, 0.5f / 256, 220.5f / 256, 255.5f / 256, 255.5f / 256, c);
}

// La lista de salas: cada una con su nombre, el anfitrión, cuántos hay y si tiene clave.
static void lobby(void) {
  int n = salas.n_salas, abiertas = 0, conectado = salas.estado == SL_LOBBY;
  for (int i = 0; i < n; i++) abiertas += salas.salas[i].estado == 'e';
  if (sel >= n) sel = n - 1;
  if (sel < 0) sel = 0;
  int pagina = sel / 8, paginas = n ? (n + 7) / 8 : 1;
  int t = fila_tocada();
  if (t == -1) abrir(F_AJUSTES);
  else if (t >= 0 && pagina * 8 + t < n) { if (sel == pagina * 8 + t) apretado[B_A] = 1; else sel = pagina * 8 + t; }
  if (apretado[B_ARR] && n) sel = (sel + n - 1) % n;
  if (apretado[B_ABA] && n) sel = (sel + 1) % n;
  if (apretado[B_IZQ] && n) sel = SDL_max(0, sel - 8);
  if (apretado[B_DER] && n) sel = SDL_min(n - 1, sel + 8);
  if (apretado[B_A] && n && salas.salas[sel].estado == 'e' && conectado) {
    if (salas.salas[sel].privada) { sala_clave = salas.salas[sel].id; abrir(F_CLAVE); }
    else salas_unirse(salas.salas[sel].id, "");
  }
  if (apretado[B_C] && conectado) abrir(F_CREAR);
  if (apretado[B_D]) abrir(F_AJUSTES);
  if (apretado[B_B]) salas_volver();
  if (form) return;
  int linea = 0;
  if (n) ayuda("Button A" PUNTOS "Enter Room", linea++);
  ayuda("Button C" PUNTOS "Create Room", linea++);
  ayuda("Button D" PUNTOS "Settings", linea++);
  palabra(conectado ? SERVER_ON : SERVER_OFF, 20, 70, 1);
  if (conectado) {
    float x = escribir("Players:", 189, 90, 1, BLANCO);
    numero(salas.conectados, x + 6, 90, BLANCO);
    x = escribir("Rooms:", 357, 90, 1, BLANCO);
    numero(n, x + 6, 90, BLANCO);
  }
  char l[120];
  snprintf(l, sizeof l, "%s / %s", salas.nombre, salas.servidor[0] ? salas.servidor : "(no server)");
  texto(l, 50, 120, BLANCO);
  palabra(PAGE, 20, 140, 1);
  char pg[8];
  snprintf(pg, sizeof pg, "%d", pagina + 1);
  escribir(pg, 113 - ancho_letras(pg, 1), 160, 1, BLANCO); // como el juego: "Page  1/  1"
  escribir("/", 114, 160, 1, BLANCO);
  numero(paginas, 143, 160, BLANCO);
  palabra(STANDBY, 640, 140, 1);
  numero(abiertas, 751, 160, BLANCO);
  if (n) barra(sel % 8);
  for (int i = 0; i < 8 && pagina * 8 + i < n; i++) {
    const FilaSala *s = &salas.salas[pagina * 8 + i];
    float y = 172.0f + 45.0f * (float)i, base = y + 23;
    palabra(s->estado == 'e' ? STANDBY : BUSY, 40, y, 1);
    texto(s->nombre[0] ? s->nombre : s->anfitrion, 143, base, BLANCO);
    texto(s->anfitrion, 452, base, GRIS);
    snprintf(l, sizeof l, "%d/2", s->invitado[0] ? 2 : 1);
    texto(l, 652, base, BLANCO);
    if (s->privada) texto("Key", 706, base, DORADO);
  }
  if (!n && conectado) texto("No rooms yet: press C to create one.", 143, 195, GRIS);
}

// La sala: los dos jugadores, el camino directo y lo que puede hacer cada uno.
static void sala_propia(void) {
  int anf = salas.soy_anfitrion, rival = salas.invitado[0] != 0, ok = rival && salas.enlace == EN_LISTO, quieta = salas.estado == SL_SALA;
  if (quieta) {
    if (apretado[B_A] && anf && ok) salas_empezar();
    if (apretado[B_C] && anf && rival) salas_echar();
    if (apretado[B_D] && anf) salas_cambiar_delay();
    if (apretado[B_B]) salas_salir();
    int t = fila_tocada(); // tocar las filas de los jugadores es como A
    if ((t == 0 || t == 1) && anf && ok) salas_empezar();
  }
  int linea = 0;
  if (quieta && anf) {
    if (ok) ayuda("Button A" PUNTOS "Start", linea++);
    ayuda("Button B" PUNTOS "Close Room", linea++);
    if (rival) ayuda("Button C" PUNTOS "Kick", linea++);
    ayuda("Button D" PUNTOS "Select Delay", linea++);
  } else if (quieta) ayuda("Button B" PUNTOS "Leave Room", linea++);
  if (anf) palabra(SERVER_ON, 20, 70, 1);
  else escribir("Client", 22, 90, 1, BLANCO);
  palabra(PORT, 187, 70, 1);
  numero(salas.puerto ? salas.puerto : 10996, 253, 90, BLANCO);
  palabra(DELAY, 357, 70, 1);
  numero(salas.delay, 444, 90, BLANCO);
  char l[120];
  snprintf(l, sizeof l, "%s%s", salas.sala_nombre[0] ? salas.sala_nombre : salas.anfitrion, salas.sala_privada ? "  (with password)" : "");
  texto(l, 50, 120, BLANCO);
  float x = escribir("Room", 22, 160, 1, BLANCO);
  numero(salas.sala_id, x + 8, 160, BLANCO);
  if (rival && salas.enlace == EN_LISTO) {
    x = escribir("Ping", 640, 160, 1, BLANCO);
    snprintf(l, sizeof l, "%d", salas.ping);
    x = escribir(l, x + 10, 160, 1, BLANCO);
    escribir("ms", x + 4, 160, 1, BLANCO);
  }
  barra(anf ? 0 : 1);
  palabra(STANDBY, 40, 172, 1);
  snprintf(l, sizeof l, "1P  %s", salas.anfitrion);
  texto(l, 143, 195, BLANCO);
  texto(anf ? "(you, host)" : "(host)", 354, 195, GRIS);
  palabra(!rival ? UNKNOWN : salas.enlace == EN_LISTO ? STANDBY : BUSY, 40, 217, 1);
  snprintf(l, sizeof l, "2P  %s", rival ? salas.invitado : "---");
  texto(l, 143, 240, BLANCO);
  texto(!anf ? "(you)" : "", 354, 240, GRIS);
}

static void formulario(void) {
  if (apretado[B_ARR]) campo = (campo + n_campos - 1) % n_campos;
  if (apretado[B_ABA]) campo = (campo + 1) % n_campos;
  int t = fila_tocada();
  if (t >= 0 && t < n_campos) campo = t;
  if (apretado[B_A]) {
    if (form == F_AJUSTES) salas_configurar(valor[0], valor[1]);
    else if (form == F_CREAR) salas_crear(valor[0], valor[1]);
    else salas_unirse(sala_clave, valor[0]);
    abrir(F_NADA);
    return;
  }
  if (apretado[B_B]) { abrir(F_NADA); return; }
  static const char *const TITULO_F[] = {"", "Settings", "Create Room", "Enter Password"};
  static const char *const PISTA[] = {"", "Your name and the lobby server (address:port).",
                                      "Name your room. With a password, only who knows it can come in.", "This room is private."};
  static const char *const CAMPO[][2] = {{"", ""}, {"Player", "Server"}, {"Room", "Password"}, {"Password", ""}};
  ayuda("Button A" PUNTOS "OK", 0);
  ayuda("Button B" PUNTOS "Cancel", 1);
  escribir(TITULO_F[form], 22, 90, 1, BLANCO);
  texto(PISTA[form], 50, 120, BLANCO);
  barra(campo);
  int cursor = (cuadros_presentados() / 20) % 2;
  for (int i = 0; i < n_campos; i++) {
    float y = 172.0f + 45.0f * (float)i;
    escribir(CAMPO[form][i], 42, y + 20, 1, BLANCO);
    char l[80];
    snprintf(l, sizeof l, "%s%s", valor[i], i == campo && cursor ? "_" : "");
    texto(l, 190, y + 23, BLANCO);
    if (form == F_CREAR && i == 1 && !valor[1][0] && i != campo) texto("(none: anyone can come in)", 190, y + 23, GRIS);
  }
}

static const char *mensaje(void) {
  static char m[120];
  if (salas.aviso[0]) return salas.aviso;
  switch (salas.estado) {
    case SL_CONECTANDO: snprintf(m, sizeof m, "Connecting to the lobby server %s...", salas.servidor); return m;
    case SL_LOBBY: return salas.n_salas ? "Choose a room and press A, or press C to create your own." : NULL;
    case SL_SALA:
      if (!salas.invitado[0]) return salas.soy_anfitrion ? "Waiting for someone to come in..." : NULL;
      if (salas.enlace == EN_BUSCANDO) { snprintf(m, sizeof m, "Opening a direct connection with %s...", salas.soy_anfitrion ? salas.invitado : salas.anfitrion); return m; }
      if (salas.enlace == EN_FALLO) return "Could not reach your challenger directly (NAT). Still trying...";
      if (salas.soy_anfitrion) { snprintf(m, sizeof m, "%s came in. Press A to start.", salas.invitado); return m; }
      snprintf(m, sizeof m, "Waiting for %s to start...", salas.anfitrion);
      return m;
    default: return NULL;
  }
}

// Desde presentar() (dibujo.c), antes de las capturas: las salas, la entrada del lobby y su dibujo en el backbuffer.
void sala_dibujar(GLuint fbo, int bw, int bh, int corrimiento) {
  if (salas.visible) leer_botones(!form);
  salas_cuadro();
  if (!salas.visible) { if (form) abrir(F_NADA); return; }
  if (!listo) crear_gl();
  if (listo < 0) return;
  corrido = corrimiento;
  nv = 0;
  // Fondo de la pantalla de red, 800x600 en el centro y reflejado a los costados.
  float u0 = -(float)corrido / 800.0f, u1 = 1 + (float)corrido / 800.0f;
  quad(T_FONDO, -(float)corrido, 0, 800.0f + (float)corrido, 600, u0, 0, u1, 1, BLANCO);
  palabra(TITULO, 20, 20, 1);
  if (form) formulario();
  else if (salas.estado == SL_SALA || salas.estado == SL_EMPEZANDO || salas.estado == SL_JUGANDO) sala_propia();
  else lobby();
  const char *m = mensaje();
  if (m) texto(m, 42, 577, salas.estado == SL_LOBBY && !salas.aviso[0] ? GRIS : DORADO);
  if (salas.opacidad < 1) { float negro[4] = {0, 0, 0, 1 - salas.opacidad}; quad(T_SOLIDO, -(float)corrido, 0, 800.0f + (float)corrido, 600, 0, 0, 1, 1, negro); }

  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glViewport(0, 0, bw, bh);
  glDisable(GL_SCISSOR_TEST); glDisable(GL_DEPTH_TEST); glDisable(GL_STENCIL_TEST); glDisable(GL_CULL_FACE); glDisable(GL_POLYGON_OFFSET_FILL);
  glColorMask(1, 1, 1, 1);
  glEnable(GL_BLEND);
  glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
  glUseProgram(prog);
  float tam[4] = {(float)bw, (float)bh, 0, 0};
  glUniform4fv(u_tam, 1, tam);
  for (int i = T_FONDO; i < T_N; i++) { glActiveTexture(GL_TEXTURE0 + (GLenum)i); glBindTexture(GL_TEXTURE_2D, tex[i]); }
  glActiveTexture(GL_TEXTURE0);
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)nv * sizeof(Vertice)), vtx, GL_STREAM_DRAW);
  glDrawArrays(GL_TRIANGLES, 0, nv);
  glBindVertexArray(0);
}

// Se llama al crear el dispositivo de Direct3D (contexto GL nuevo), después de los controles táctiles.
void sala_iniciar(SDL_Window *w) {
  ventana = w;
  listo = 0;
  leer_config();
  oyente_anterior = oyente_eventos;
  oyente_eventos = evento;
}
