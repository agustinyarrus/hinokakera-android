// Controles táctiles (Android; en la PC con HINO_TACTIL=1, tocando con el mouse): cruz a la izquierda, A B C D y
// Start a la derecha. Se acomodan en cada cuadro según la pantalla: tamaño físico (en cm, por la densidad), zona
// segura (cámara, notch) y lo que deja libre la imagen del juego a los costados; si ahí no entran (16:9,
// tablets) van encima de la imagen, más transparentes. Escriben teclas_virtuales[] con las teclas del jugador 1
// de config.dat: el juego los ve como el teclado. Tocar la imagen es el mouse (el modo historia es una novela
// visual que se juega con clics) y el botón Atrás de Android es Pausa.
#include "d3d9_int.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define PI 3.14159265f

extern uint8_t teclas_virtuales[256];             // dinput8.c
extern void (*oyente_eventos)(const SDL_Event *e); // user32.c
extern uint32_t raton_botones;
void raton_juego(int x, int y, uint32_t botones);
const char *ruta_host(const char *g, int escribir, char *buf, size_t tam); // k32_archivos.c
uint32_t gdi_glifo(const uint8_t lf[60], uint32_t ch, uint32_t formato, const uint8_t mat2[16], uint8_t gm[20], uint8_t *buf, uint32_t tam);
uint32_t cuadros_presentados(void);

enum { C_IZQ, C_DER, C_ARR, C_ABA, C_A, C_B, C_C, C_D, C_PAUSA, C_N };
// Teclas del jugador 1 en config.dat (desde 0x50: izquierda, derecha, arriba, abajo, A, B, C, D, pausa); estas
// son las del original, por si falta.
static uint8_t dik[C_N] = {0x22, 0x24, 0x15, 0x23, 0x2c, 0x2d, 0x2e, 0x2f, 0x3b};
static const char *ETIQUETA[C_N] = {0, 0, 0, 0, "A", "B", "C", "D", "START"};

typedef struct { float x, y, r; } Circulo;
static Circulo cruz, boton[C_N]; // boton[C_A .. C_PAUSA]
static int activo, encima[2];    // por costado (cruz, botones): no hubo lugar al lado de la imagen
static SDL_Window *ventana;

// Cada dedo queda atado a lo que tocó al apoyarse: la cruz lo sigue aunque se salga; entre botones se puede
// deslizar; sobre la imagen es el mouse.
enum { DEDO_CRUZ, DEDO_BOTON, DEDO_RATON };
typedef struct { SDL_FingerID id; int tipo, que; float x, y; } Dedo;
static Dedo dedos[10];
static int n_dedos, atras;
static uint8_t pulsado[C_N];
static uint32_t hasta[C_N], raton_hasta;
static int raton_suelto_pendiente;

static void acomodar(int W, int H) {
  float dens = SDL_GetWindowPixelDensity(ventana), sx = 0, sy = 0, sw = (float)W, sh = (float)H;
  SDL_Rect s;
  if (SDL_GetWindowSafeArea(ventana, &s) && s.w > 0 && s.h > 0) { sx = s.x * dens; sy = s.y * dens; sw = s.w * dens; sh = s.h * dens; }
#ifdef SDL_PLATFORM_ANDROID
  const float dpi_base = 160; // la escala de contenido de Android es dpi / 160
#else
  const float dpi_base = 96;
#endif
  float esc = SDL_GetDisplayContentScale(SDL_GetDisplayForWindow(ventana));
  float cm = esc > 0 ? dpi_base * esc / 2.54f : (float)H / 7;
  float gx = (float)imagen_juego[0], gw = (float)imagen_juego[2];
  float izq = gx - sx, der = sx + sw - (gx + gw);
  // Tamaño normal (cm): cruz de 2,3 de diámetro; botones de 1,1 separados 0,25; Start de 0,8; márgenes de 0,3.
  // Cada costado se achica hasta el 70 % para entrar al lado de la imagen (la cámara come uno de los dos); si ni
  // así entra, va encima a tamaño normal.
  const float R0 = 1.15f * cm, rb0 = 0.55f * cm, sep0 = 0.25f * cm, rp0 = 0.4f * cm, m0 = 0.3f * cm;
  float fc = izq / (2 * R0 + 2 * m0), fb = der / (4 * rb0 + sep0 + 2 * m0), alto = sh / (2 * R0 + 2 * m0 + 2 * rp0);
  encima[0] = fc < 0.7f; encima[1] = fb < 0.7f;
  fc = SDL_min(encima[0] ? 1 : fc, SDL_min(1, alto)); // y que entren a lo alto (pantallas bajas)
  fb = SDL_min(encima[1] ? 1 : fb, SDL_min(1, alto));
  float R = R0 * fc, mc = m0 * fc, rb = rb0 * fb, sep = sep0 * fb, rp = rp0 * fb, mb = m0 * fb, d = rb + sep / 2;
  float cy = sy + sh * 0.62f; // a la altura de los pulgares
  float cyc = SDL_min(cy, sy + sh - mc - R), cyb = SDL_min(cy, sy + sh - mb - d - rb);
  float cx = encima[0] ? sx + mc + R : sx + izq / 2, bx = encima[1] ? sx + sw - mb - d - rb : sx + sw - der / 2;
  cruz = (Circulo){cx, cyc, R};
  boton[C_C] = (Circulo){bx - d, cyb - d, rb}; boton[C_D] = (Circulo){bx + d, cyb - d, rb};
  boton[C_A] = (Circulo){bx - d, cyb + d, rb}; boton[C_B] = (Circulo){bx + d, cyb + d, rb};
  boton[C_PAUSA] = (Circulo){bx, sy + mb + rp, rp};
}

static int en_imagen(float x, float y) {
  return x >= imagen_juego[0] && x < imagen_juego[0] + imagen_juego[2] && y >= imagen_juego[1] && y < imagen_juego[1] + imagen_juego[3];
}
static int boton_en(float x, float y) {
  int mejor = -1;
  float md = 1.6f; // hasta 1,26 radios
  for (int i = C_A; i <= C_PAUSA; i++) {
    float ex = x - boton[i].x, ey = y - boton[i].y, d2 = (ex * ex + ey * ey) / (boton[i].r * boton[i].r);
    if (d2 <= md) { mejor = i; md = d2; }
  }
  return mejor;
}

// Ocho direcciones por el ángulo desde el centro, con zona muerta de un cuarto del radio.
static void direccion(const Dedo *d, uint8_t *q) {
  float dx = d->x - cruz.x, dy = d->y - cruz.y;
  if (dx * dx + dy * dy < 0.0625f * cruz.r * cruz.r) return;
  int s = (int)lroundf(atan2f(-dy, dx) / (PI / 4)) & 7; // 0 derecha, 2 arriba, 4 izquierda, 6 abajo
  q[C_DER] |= s == 7 || s <= 1;
  q[C_ARR] |= s >= 1 && s <= 3;
  q[C_IZQ] |= s >= 3 && s <= 5;
  q[C_ABA] |= s >= 5;
}

// Un toque cortísimo (apoyar y soltar en el mismo cuadro) dura al menos dos cuadros: si no, el juego no lo ve.
static void aplicar(void) {
  uint8_t q[C_N] = {0};
  for (int i = 0; i < n_dedos; i++) {
    if (dedos[i].tipo == DEDO_CRUZ) direccion(&dedos[i], q);
    else if (dedos[i].tipo == DEDO_BOTON && dedos[i].que >= 0) q[dedos[i].que] = 1;
  }
  q[C_PAUSA] |= (uint8_t)atras;
  uint32_t c = cuadros_presentados();
  for (int k = 0; k < C_N; k++) {
    if (q[k] && !pulsado[k]) hasta[k] = c + 2;
    uint8_t nuevo = q[k] || (pulsado[k] && c < hasta[k]);
    if (nuevo != pulsado[k]) teclas_virtuales[dik[k]] = nuevo; // solo al cambiar: no pisa el guion de HINO_TECLAS
    pulsado[k] = nuevo;
  }
  if (raton_suelto_pendiente && c >= raton_hasta) {
    raton_suelto_pendiente = 0;
    extern int raton_x, raton_y;
    raton_juego(raton_x, raton_y, raton_botones & ~SDL_BUTTON_LMASK);
  }
}

static void raton(const Dedo *d, int abajo) {
  float x = SDL_clamp(d->x, imagen_juego[0], imagen_juego[0] + imagen_juego[2] - 1);
  float y = SDL_clamp(d->y, imagen_juego[1], imagen_juego[1] + imagen_juego[3] - 1);
  int gx = (int)((x - imagen_juego[0]) * imagen_juego[4] / imagen_juego[2]) - imagen_juego[6];
  int gy = (int)((y - imagen_juego[1]) * imagen_juego[5] / imagen_juego[3]);
  if (!abajo && cuadros_presentados() < raton_hasta) { raton_juego(gx, gy, raton_botones); raton_suelto_pendiente = 1; return; }
  if (abajo && !(raton_botones & SDL_BUTTON_LMASK)) raton_hasta = cuadros_presentados() + 2;
  raton_juego(gx, gy, abajo ? raton_botones | SDL_BUTTON_LMASK : raton_botones & ~SDL_BUTTON_LMASK);
}

static void evento(const SDL_Event *e) {
  int W, H;
  SDL_GetWindowSizeInPixels(ventana, &W, &H);
  switch (e->type) {
    case SDL_EVENT_FINGER_DOWN: {
      if (n_dedos >= (int)SDL_arraysize(dedos)) return;
      Dedo *d = &dedos[n_dedos++];
      d->id = e->tfinger.fingerID; d->x = e->tfinger.x * (float)W; d->y = e->tfinger.y * (float)H;
      float dx = d->x - cruz.x, dy = d->y - cruz.y;
      int b = boton_en(d->x, d->y);
      if (dx * dx + dy * dy <= 2.56f * cruz.r * cruz.r) d->tipo = DEDO_CRUZ; // hasta 1,6 radios
      else if (b >= 0) { d->tipo = DEDO_BOTON; d->que = b; }
      else if (en_imagen(d->x, d->y)) { d->tipo = DEDO_RATON; raton(d, 1); }
      else { d->tipo = DEDO_BOTON; d->que = -1; } // un costado vacío: no hace nada hasta que llegue a un botón
      break;
    }
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED: {
      int i = 0;
      while (i < n_dedos && dedos[i].id != e->tfinger.fingerID) i++;
      if (i == n_dedos) return;
      Dedo *d = &dedos[i];
      d->x = e->tfinger.x * (float)W; d->y = e->tfinger.y * (float)H;
      if (e->type == SDL_EVENT_FINGER_MOTION) {
        if (d->tipo == DEDO_BOTON) d->que = boton_en(d->x, d->y);
        else if (d->tipo == DEDO_RATON) raton(d, 1);
      } else {
        if (d->tipo == DEDO_RATON) raton(d, 0);
        dedos[i] = dedos[--n_dedos];
      }
      break;
    }
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
      if (e->key.scancode != SDL_SCANCODE_AC_BACK) return;
      atras = e->type == SDL_EVENT_KEY_DOWN;
      break;
    default:
      return;
  }
  aplicar();
}

// ---- dibujo: cuadriláteros con círculos, anillos, flechas y letras resueltos en el fragment shader ----
typedef struct { float x, y, u, v, r, g, b, a, tipo, rad; } Vertice;
enum { T_DISCO, T_ANILLO, T_LETRA, T_FLECHA };
static Vertice vtx[1200];
static int nv;
static GLuint prog, vao, vbo, atlas;
static GLint u_pant;

static void quad(float cx, float cy, float hw, float hh, float giro, float u0, float v0, float u1, float v1, const float *col, int tipo, float rad) {
  static const int ORDEN[6] = {0, 1, 2, 0, 2, 3};
  float px[4] = {-hw, hw, hw, -hw}, py[4] = {-hh, -hh, hh, hh}, us[4] = {u0, u1, u1, u0}, vs[4] = {v0, v0, v1, v1};
  float c = cosf(giro), s = sinf(giro);
  if (nv + 6 > (int)SDL_arraysize(vtx)) return;
  for (int k = 0; k < 6; k++) {
    int i = ORDEN[k];
    vtx[nv++] = (Vertice){cx + px[i] * c - py[i] * s, cy + px[i] * s + py[i] * c, us[i], vs[i], col[0], col[1], col[2], col[3], (float)tipo, rad};
  }
}
static void circulo(Circulo c, int tipo, const float *col) { quad(c.x, c.y, c.r, c.r, 0, -1, -1, 1, 1, col, tipo, c.r); }

// Letras del juego: la caché de glifos de 28 px con la matriz {2,0,0,1} que usa él (salen el doble de anchas y
// se dibujan a la mitad, o sea con el doble de muestras a lo ancho).
#define ALTO_FUENTE 28
#define ATLAS_W 512
#define ATLAS_H 32
typedef struct { float u0, u1, v1; int w, h, ox, oy, avance; } Glifo;
static Glifo glifos[128];

static void armar_letras(uint8_t *pix) {
  uint8_t lf[60] = {0}, gm[20], mat[16] = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0};
  int32_t alto = ALTO_FUENTE, peso = 400;
  memcpy(lf, &alto, 4); memcpy(lf + 16, &peso, 4); lf[23] = 0x80;
  memcpy(lf + 28, MEM + 0x568024, 31); // el nombre de la fuente, tal cual está en el exe
  static uint8_t buf[16384];
  int x = 0;
  for (const char *p = "ABCDSTR"; *p; p++) {
    uint32_t n = gdi_glifo(lf, (uint32_t)*p, 5, mat, gm, buf, sizeof buf);
    if (n == 0xffffffffu || !n) continue;
    Glifo *g = &glifos[(int)*p];
    int16_t av;
    memcpy(&g->w, gm, 4); memcpy(&g->h, gm + 4, 4); memcpy(&g->ox, gm + 8, 4); memcpy(&g->oy, gm + 12, 4); memcpy(&av, gm + 16, 2);
    g->avance = av;
    if (x + g->w > ATLAS_W || g->h > ATLAS_H) { g->w = 0; continue; }
    int paso = (g->w + 3) & ~3; // filas alineadas a 4 bytes, 17 niveles (0..16)
    for (int f = 0; f < g->h; f++)
      for (int c = 0; c < g->w; c++) pix[f * ATLAS_W + x + c] = (uint8_t)SDL_min(255, buf[f * paso + c] * 255 / 16);
    g->u0 = (float)x / ATLAS_W; g->u1 = (float)(x + g->w) / ATLAS_W; g->v1 = (float)g->h / ATLAS_H;
    x += g->w + 2;
  }
}

static void texto(const char *s, float cx, float cy, float alto, const float *col) {
  float k = alto / ALTO_FUENTE, ancho = 0;
  for (const char *p = s; *p; p++) ancho += (float)glifos[(int)*p].avance * 0.5f * k;
  float x = cx - ancho / 2, base = cy + (float)glifos['A'].oy * k / 2; // centrado por la altura de las mayúsculas
  for (const char *p = s; *p; p++) {
    Glifo *g = &glifos[(int)*p];
    if (g->w) {
      float w = (float)g->w * 0.5f * k, h = (float)g->h * k, x0 = x + (float)g->ox * 0.5f * k, y0 = base - (float)g->oy * k;
      quad(x0 + w / 2, y0 + h / 2, w / 2, h / 2, 0, g->u0, 0, g->u1, g->v1, col, T_LETRA, 1);
    }
    x += (float)g->avance * 0.5f * k;
  }
}

static const char *VS =
    "#version 300 es\n"
    "layout(location = 0) in vec2 a_p;\n"
    "layout(location = 1) in vec2 a_uv;\n"
    "layout(location = 2) in vec4 a_col;\n"
    "layout(location = 3) in vec2 a_tipo;\n"
    "uniform vec4 u_pant;\n"
    "out vec2 v_uv; out vec4 v_col; out vec2 v_tipo;\n"
    "void main() {\n"
    "  v_uv = a_uv; v_col = a_col; v_tipo = a_tipo;\n"
    "  gl_Position = vec4(a_p.x / u_pant.x * 2.0 - 1.0, 1.0 - a_p.y / u_pant.y * 2.0, 0.0, 1.0);\n"
    "}\n";
// Bordes suaves: la cobertura sale de la distancia al borde en píxeles (rad = píxeles por unidad de uv).
static const char *FS =
    "#version 300 es\n"
    "precision mediump float;\n"
    "in vec2 v_uv; in vec4 v_col; in vec2 v_tipo;\n"
    "uniform sampler2D u_letras;\n"
    "out vec4 o;\n"
    "void main() {\n"
    "  float l = length(v_uv), r = v_tipo.y, c;\n"
    "  if (v_tipo.x < 0.5) c = clamp((1.0 - l) * r, 0.0, 1.0);\n"
    "  else if (v_tipo.x < 1.5) c = clamp(min(1.0 - l, l - 0.9) * r, 0.0, 1.0);\n"
    "  else if (v_tipo.x < 2.5) c = texture(u_letras, v_uv).r;\n"
    "  else c = clamp(min(1.0 - v_uv.y, (v_uv.y + 1.0) * 0.5 - abs(v_uv.x)) * r, 0.0, 1.0);\n"
    "  float a = v_col.a * c;\n"
    "  o = vec4(v_col.rgb * a, a);\n"
    "}\n";

static void crear_gl(void) {
  prog = glCreateProgram();
  glAttachShader(prog, compilar(GL_VERTEX_SHADER, VS));
  glAttachShader(prog, compilar(GL_FRAGMENT_SHADER, FS));
  glLinkProgram(prog);
  GLint ok;
  glGetProgramiv(prog, GL_LINK_STATUS, &ok);
  if (!ok) { char log[2048]; glGetProgramInfoLog(prog, sizeof log, NULL, log); fprintf(stderr, "tactil: %s\n", log); activo = 0; return; }
  u_pant = glGetUniformLocation(prog, "u_pant");
  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);
  glGenBuffers(1, &vbo);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  static const int TAM[4] = {2, 2, 4, 2}, DESDE[4] = {0, 8, 16, 32};
  for (int i = 0; i < 4; i++) {
    glEnableVertexAttribArray((GLuint)i);
    glVertexAttribPointer((GLuint)i, TAM[i], GL_FLOAT, GL_FALSE, sizeof(Vertice), (const void *)(uintptr_t)DESDE[i]);
  }
  uint8_t *pix = calloc(ATLAS_W * ATLAS_H, 1);
  armar_letras(pix);
  glGenTextures(1, &atlas);
  glBindTexture(GL_TEXTURE_2D, atlas);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, ATLAS_W, ATLAS_H, 0, GL_RED, GL_UNSIGNED_BYTE, pix);
  free(pix);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

static void leer_teclas(void) {
  char buf[700];
  size_t n = 0;
  const char *r = ruta_host("C:\\HINOKAKERA\\config.dat", 0, buf, sizeof buf);
  uint8_t *cfg = r ? SDL_LoadFile(r, &n) : NULL;
  if (cfg && n >= 0x74 && !memcmp(cfg, "cfg_", 4))
    for (int k = 0; k < C_N; k++) {
      uint32_t v;
      memcpy(&v, cfg + 0x50 + 4 * k, 4);
      if (v && v < 256) dik[k] = (uint8_t)v;
    }
  SDL_free(cfg);
}

// Se llama al crear el dispositivo de Direct3D (contexto GL nuevo: los objetos de GL se rehacen).
void tactil_iniciar(SDL_Window *w) {
#ifndef SDL_PLATFORM_ANDROID
  if (!getenv("HINO_TACTIL")) return;
  SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "1"); // probarlo con el mouse
#endif
  ventana = w;
  activo = 1;
  oyente_eventos = evento;
  leer_teclas();
  crear_gl();
}

void tactil_dibujar(SDL_Window *w, int W, int H) {
  if (!activo || w != ventana) return;
  acomodar(W, H);
  aplicar(); // suelta los toques cortos que ya cumplieron sus dos cuadros
  if (SDL_HasGamepad()) return; // con mando conectado no se dibujan (siguen andando si se tocan)
  nv = 0;
  // Colores por costado: encima de la imagen, más transparentes.
  float col[2][4][4];
  for (int l = 0; l < 2; l++) {
    float op = encima[l] ? 0.6f : 1.0f, a[4] = {0.10f, 0.40f, 0.45f, 0.9f}; // fondo, fondo apretado, borde, letra
    for (int k = 0; k < 4; k++) { col[l][k][0] = col[l][k][1] = col[l][k][2] = 1; col[l][k][3] = a[k] * op; }
  }
  circulo(cruz, T_DISCO, col[0][0]);
  circulo(cruz, T_ANILLO, col[0][2]);
  static const int FLECHA[4] = {C_ARR, C_DER, C_ABA, C_IZQ};
  for (int i = 0; i < 4; i++) {
    float a = (float)i * PI / 2, r = cruz.r * 0.62f, t = cruz.r * 0.2f;
    quad(cruz.x + sinf(a) * r, cruz.y - cosf(a) * r, t, t, a, -1, -1, 1, 1, col[0][pulsado[FLECHA[i]] ? 3 : 2], T_FLECHA, t);
  }
  for (int i = C_A; i <= C_PAUSA; i++) {
    circulo(boton[i], T_DISCO, col[1][pulsado[i] ? 1 : 0]);
    circulo(boton[i], T_ANILLO, col[1][2]);
    texto(ETIQUETA[i], boton[i].x, boton[i].y, boton[i].r * (i == C_PAUSA ? 0.55f : 1.1f), col[1][3]);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, W, H);
  glDisable(GL_SCISSOR_TEST); glDisable(GL_DEPTH_TEST); glDisable(GL_STENCIL_TEST); glDisable(GL_CULL_FACE); glDisable(GL_POLYGON_OFFSET_FILL);
  glEnable(GL_BLEND);
  glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
  glUseProgram(prog);
  float pant[4] = {(float)W, (float)H, 0, 0};
  glUniform4fv(u_pant, 1, pant);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, atlas);
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)nv * sizeof(Vertice)), vtx, GL_STREAM_DRAW);
  glDrawArrays(GL_TRIANGLES, 0, nv);
  glBindVertexArray(0); // el dibujo de D3D vuelve a poner el suyo en cada llamada
}
