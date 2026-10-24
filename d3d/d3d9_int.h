// Direct3D 9 sobre OpenGL ES 3: estructuras internas compartidas.
#pragma once
#include "com.h"
#include "gl.h"
#include <SDL3/SDL.h>

// Convenciones (ver dibujo.c): todo se dibuja en FBO con la fila 0 arriba, como la memoria de D3D; al
// presentar se da vuelta. La geometría se corre medio píxel porque D3D9 pone el centro del píxel en el entero.

typedef enum { R_TEXTURA = 1, R_SUPERFICIE, R_VB, R_IB, R_VS, R_PS, R_DECL, R_BLOQUE, R_CONSULTA, R_BUFFER } TipoRecurso;

typedef struct {
  TipoRecurso tipo;
  int refs;
  uint32_t obj; // objeto COM del invitado
} Recurso;

#define MAX_NIVELES 14
typedef struct Superficie Superficie;
typedef struct Textura {
  Recurso r;
  uint32_t w, h, niveles, uso, formato, pool;
  GLuint gl;
  uint8_t *copia[MAX_NIVELES];   // contenido de cada nivel en el formato de D3D (para Lock)
  uint32_t lock_va[MAX_NIVELES]; // memoria del invitado mientras el nivel está bloqueado
  Superficie *sup[MAX_NIVELES];
  GLuint fbo;                    // si es render target
  uint8_t *alfa;                 // alfa promedio del nivel 0 en 64x64 (modo ancho)
} Textura;

struct Superficie {
  Recurso r;
  Textura *tex;
  int nivel;
  uint32_t w, h, formato, uso, pool;
  uint32_t w_gl;                 // ancho real en GL (> w en el backbuffer del modo ancho)
  GLuint color_rb, prof_rb, fbo; // backbuffer / render target suelto / profundidad
  int profundidad;
  uint8_t *copia;                // superficies sueltas que se pueden bloquear
  uint32_t lock_va;
};

typedef struct {
  Recurso r;
  uint32_t tam, uso, formato, pool; // formato: FVF del VB o D3DFMT_INDEX16/32 del IB
  uint32_t va;                      // datos en memoria del invitado
} Buffer;

typedef struct { uint16_t stream, offset; uint8_t tipo, metodo, uso, indice; } Elemento;
typedef struct {
  Recurso r;
  Elemento e[32];
  int n;
} Decl;

typedef struct {
  Recurso r;
  char *glsl;              // cuerpo traducido
  uint8_t uso[16], ind[16]; // dcl de cada registro de entrada vN (uso 0xff = sin declarar)
  uint32_t id;
  uint32_t fuente_va, fuente_tam; // GetFunction
  int pos_tipo, pos_c;      // cómo sale oPos: 1 = m4x4 oPos, v0, c[pos_c]; 2 = mov oPos, v0; -1 = otra cosa
} ShaderV;

typedef struct {
  Recurso r;
  uint32_t tipo;
} Consulta;

typedef struct {
  float diffuse[4], ambient[4], specular[4], emissive[4], power;
} Material;

typedef struct {
  uint32_t tipo;
  float diffuse[4], specular[4], ambient[4], pos[3], dir[3], range, falloff, at0, at1, at2, theta, phi;
} Luz;

#define T_VIEW 2
#define T_PROJ 3
#define T_TEX0 16
#define T_WORLD 256
typedef struct Dispositivo {
  Recurso r;
  SDL_Window *ventana;
  SDL_GLContext gl;
  uint32_t hwnd, comportamiento;
  uint32_t pp[14];             // D3DPRESENT_PARAMETERS
  Superficie *backbuffer, *prof_auto;
  Superficie *rt, *ds;
  uint32_t rs[256];
  uint32_t tss[8][33];
  uint32_t ss[16][14];
  Textura *tex[16];
  float mat[260][16];          // transformaciones (WORLD = 256)
  uint32_t vp[6];              // D3DVIEWPORT9 (MinZ/MaxZ como bits de float)
  uint32_t tijera[4];
  Material material;
  Luz luces[8];
  int luz_on[8];
  float vsc[256][4];
  ShaderV *vs;
  uint32_t ps;
  Decl *decl;
  uint32_t fvf;
  struct { Buffer *b; uint32_t off, stride; } stream[16];
  Buffer *ib;
  GLuint vao, vbo[16], ibo;
  int en_escena;
} Dispositivo;

extern Dispositivo *disp; // un único device (el juego crea uno)

// Objetos COM de cada tipo (d3d9.c / recursos.c).
uint32_t obj_textura(Textura *t);
uint32_t obj_superficie(Superficie *s);
uint32_t obj_vb(Buffer *b);
uint32_t obj_ib(Buffer *b);
uint32_t obj_vs(ShaderV *s);
uint32_t obj_decl(Decl *d);
void recurso_addref(Recurso *r);
uint32_t recurso_release(Recurso *r);

// recursos.c
Textura *textura_nueva(uint32_t w, uint32_t h, uint32_t niveles, uint32_t uso, uint32_t formato, uint32_t pool);
void textura_subir(Textura *t, int nivel, const uint8_t *datos_d3d);
void textura_borrar(Textura *t);
Superficie *superficie_de(Textura *t, int nivel);
Superficie *superficie_nueva(uint32_t w, uint32_t h, uint32_t formato, int profundidad, int con_fbo, uint32_t extra);
void superficie_borrar(Superficie *s);
uint32_t bytes_por_pixel(uint32_t formato);
void convertir_a_rgba(uint32_t formato, const uint8_t *src, uint32_t w, uint32_t h, uint8_t *dst);
GLuint fbo_de(Superficie *rt);

// dibujo.c
void dibujo_iniciar(void);
void aplicar_destino(void);
// indexado: usa índices (del IB, o up_indices en los *UP). base: vértice inicial o BaseVertexIndex.
void dibujar(int indexado, uint32_t tipo_prim, uint32_t base, uint32_t n_prim, uint32_t ini_indice,
             uint32_t up_vertices, uint32_t up_stride, uint32_t up_indices, uint32_t formato_indices);
void limpiar(uint32_t n, uint32_t rects, uint32_t flags, uint32_t color, float z, uint32_t stencil);
void presentar(void);
GLuint compilar(GLenum tipo, const char *src);
// Modo ancho (dibujo.c): el backbuffer real tiene ancho_extra columnas más que el del juego, que se dibuja
// centrado; lo 3D se abre hacia los costados y los fondos 2D de todo el ancho se extienden.
extern int ancho_extra;
void ancho_iniciar(SDL_Window *w, int pantalla_completa, uint32_t bw, uint32_t bh);
int registrando_dibujos(void);
extern int imagen_juego[7];
// Controles táctiles (tactil.c).
void tactil_iniciar(SDL_Window *w);
void tactil_dibujar(SDL_Window *w, int ww, int wh);
// Lobby de salas (sala.c).
void sala_iniciar(SDL_Window *w);
void sala_dibujar(GLuint fbo, int bw, int bh, int corrimiento);

// vs.c
ShaderV *vs_traducir(const char *fuente);

static inline float bits_f(uint32_t b) { float f; memcpy(&f, &b, 4); return f; }
static inline uint32_t f_bits(float f) { uint32_t b; memcpy(&b, &f, 4); return b; }
