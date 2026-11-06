// Modelos animados de los mods de escenario (tools/escenario/zombis.mjs). Si los primeros vértices de un vertex
// buffer traen la firma, son datos (ningún triángulo los usa) y el resto son figuras en pose de descanso, cada una en
// su espacio local con la misma malla. En cada cuadro se suben colocadas y animadas; el VB del juego no se toca.
//
// Los datos van en la posición y la uv de los vértices de datos (5 floats por vértice, en ese orden):
//   firma 7777 -3131 2468, versión 1, vértices de datos, figuras F, vértices por figura V, huesos H,
//   H x (pivote x y z, padre), F x 12 (x z rumbo escala tipo fase vel radio brazo incl cojera sentido), V x hueso.
// Huesos de los zombis: 0 cadera (raíz), 1 torso, 2 cabeza, 3 y 4 brazos izq./der., 5 y 6 piernas izq./der.
// Con un hueso (sombras) se coloca la figura entera; con dos (fuegos), el 1 es la llama que titila.
// Tipo de figura: 0 quieta (empuja la barricada), 1 camina en círculo de radio `radio`, 2 fuego (`radio` = alto de
// la base de la llama). Con un hueso (tools/escenario/caldera.mjs): 3 corriente (quieta; la uv corre `radio` en u y
// `vel` en v por segundo: lava), 4 giro (`vel` vueltas por segundo sobre su eje y, a la altura `incl` y flotando
// `radio` arriba y abajo: el cristal), 5 brasa (sube `radio` en 1/`vel` segundos desde la altura `cojera`, se mece
// `incl` y se achica hasta desaparecer; vuelve a empezar).
#include "d3d9_int.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_HUESOS 8
#define PI 3.14159265f
typedef struct { float x, z, rumbo, escala, tipo, fase, vel, radio, brazo, incl, cojera, sentido; } Figura;
typedef float Mat[3][4]; // rotación 3x3 | traslación

struct Animado {
  uint32_t datos, n_fig, n_v, n_h, stride, off_pos, cuadro, off_tex;
  int off_nor; // -1 sin normales
  float piv[MAX_HUESOS][4];
  Figura *fig;
  uint8_t *hueso;  // por vértice de la malla común
  float *reposo;   // posición y normal (6 floats) de cada vértice real
  float *uv;       // uv de cada vértice real (para las corrientes)
  uint8_t *vb;     // copia del VB con las figuras animadas
};

uint32_t cuadros_presentados(void);

// M = A * B (como transformaciones afines).
static void mul(Mat m, Mat a, Mat b) {
  Mat r;
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 4; j++) r[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j];
    r[i][3] += a[i][3];
  }
  memcpy(m, r, sizeof r);
}
// Rotaciones en el orden y·x·z (rumbo, cabeceo hacia adelante, rolido) alrededor de un pivote, con traslación previa.
static void rot(Mat m, float y, float x, float z, const float *piv, float tx, float ty, float tz) {
  float cy = cosf(y), sy = sinf(y), cx = cosf(x), sx = sinf(x), cz = cosf(z), sz = sinf(z);
  // Ry * Rx * Rz; Rx con x > 0 lleva +y hacia +z (inclinarse adelante), y los miembros que cuelgan (-y) hacia atrás.
  float r[3][3] = {
    {cy * cz + sy * sx * sz, -cy * sz + sy * sx * cz, sy * cx},
    {cx * sz, cx * cz, -sx},
    {-sy * cz + cy * sx * sz, sy * sz + cy * sx * cz, cy * cx},
  };
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) m[i][j] = r[i][j];
    m[i][3] = piv[i] - (r[i][0] * piv[0] + r[i][1] * piv[1] + r[i][2] * piv[2]);
  }
  m[0][3] += tx; m[1][3] += ty; m[2][3] += tz;
}

static void soltar(Buffer *b) {
  Animado *a = b->anim;
  if (a) { free(a->fig); free(a->hueso); free(a->reposo); free(a->uv); free(a->vb); free(a); }
  b->anim = NULL; b->anim_estado = 0;
}
void animado_soltar(Buffer *b) { soltar(b); }

// Float i de los datos: 5 por vértice, posición y uv.
static float dato(const Buffer *b, uint32_t stride, uint32_t off_pos, uint32_t off_tex, uint32_t i) {
  uint32_t k = i % 5;
  return bits_f(M32(b->va + (i / 5) * stride + (k < 3 ? off_pos + 4 * k : off_tex + 4 * (k - 3))));
}

static void detectar(Buffer *b, uint32_t stride, uint32_t off_pos, int off_nor, uint32_t off_tex) {
  b->anim_estado = 1;
  if (stride < 20 || b->tam < stride * 4) return;
  uint32_t n_vb = b->tam / stride;
  #define F(i) dato(b, stride, off_pos, off_tex, (i))
  if (F(0) != 7777.0f || F(1) != -3131.0f || F(2) != 2468.0f || F(3) != 1.0f) return;
  uint32_t datos = (uint32_t)F(4), n_fig = (uint32_t)F(5), n_v = (uint32_t)F(6), n_h = (uint32_t)F(7);
  if (n_h < 1 || n_h > MAX_HUESOS || datos + (uint64_t)n_fig * n_v > n_vb || 8 + 4 * n_h + 12 * n_fig + n_v > datos * 5) {
    fprintf(stderr, "animado: cabecera inválida (%u datos, %u figuras, %u vértices, %u huesos)\n", datos, n_fig, n_v, n_h);
    return;
  }
  Animado *a = calloc(1, sizeof *a);
  *a = (Animado){datos, n_fig, n_v, n_h, stride, off_pos, 0, off_tex, off_nor};
  uint32_t p = 8;
  for (uint32_t h = 0; h < n_h; h++) for (int k = 0; k < 4; k++) a->piv[h][k] = F(p++);
  a->fig = malloc(n_fig * sizeof *a->fig);
  for (uint32_t f = 0; f < n_fig; f++) for (int k = 0; k < 12; k++) ((float *)&a->fig[f])[k] = F(p++);
  a->hueso = malloc(n_v);
  for (uint32_t v = 0; v < n_v; v++) { uint32_t h = (uint32_t)F(p++); a->hueso[v] = (uint8_t)(h < n_h ? h : 0); }
  #undef F
  a->reposo = malloc((size_t)n_fig * n_v * 6 * sizeof(float));
  a->uv = malloc((size_t)n_fig * n_v * 2 * sizeof(float));
  for (uint32_t i = 0; i < n_fig * n_v; i++) {
    uint32_t va = b->va + (datos + i) * stride;
    for (int k = 0; k < 3; k++) a->reposo[6 * i + k] = bits_f(M32(va + off_pos + 4 * k));
    for (int k = 0; k < 3; k++) a->reposo[6 * i + 3 + k] = off_nor >= 0 ? bits_f(M32(va + (uint32_t)off_nor + 4 * k)) : 0;
    for (int k = 0; k < 2; k++) a->uv[2 * i + k] = bits_f(M32(va + off_tex + 4 * k));
  }
  a->vb = malloc(b->tam);
  memcpy(a->vb, MEM + b->va, b->tam);
  b->anim = a; b->anim_estado = 2;
  fprintf(stderr, "animado: %u figuras de %u vértices y %u huesos\n", n_fig, n_v, n_h);
}

// Pose de cada hueso de una figura en el tiempo t (segundos).
static void pose(const Animado *a, const Figura *f, float t, Mat *m) {
  static const float cero[3] = {0, 0, 0};
  float x = f->x, z = f->z, rumbo = f->rumbo, fase = f->fase, paso = f->vel * t;
  if (f->tipo == 1) { // camina en círculo alrededor de la pelea; la cadencia sigue a la distancia recorrida
    float th = atan2f(z, x) + f->sentido * paso / f->radio;
    x = f->radio * cosf(th); z = f->radio * sinf(th);
    rumbo = atan2f(-f->sentido * sinf(th), f->sentido * cosf(th)); // tangente, hacia donde avanza
    fase += paso / 0.55f; // un ciclo (dos pasos cortos) cada 0.55: los zombis miden ~1
  } else fase += t * f->vel;
  float w = 2 * PI * fase, s = sinf(w);
  Mat raiz, esc = {{f->escala, 0, 0, 0}, {0, f->escala, 0, 0}, {0, 0, f->escala, 0}};
  if (f->tipo >= 3) { // corriente, giro o brasa: un hueso
    float y = 0, f0 = f->fase; // la fase sin avanzar (arriba ya le sumó vel)
    if (f->tipo == 4) { rumbo += 2 * PI * f->vel * t; y = f->incl + f->radio * sinf(2 * PI * (f0 + 0.11f * t)); }
    if (f->tipo == 5) {
      float vida = f0 + f->vel * t;
      vida -= floorf(vida);
      y = f->cojera + f->radio * vida;
      x += f->incl * sinf(2 * PI * (2.3f * vida + f0)); z += f->incl * cosf(2 * PI * (1.7f * vida + 3 * f0));
      float e = f->escala * sqrtf(1 - vida) * (0.75f + 0.25f * sinf(40 * vida + 9 * f0)); // titila y se apaga
      esc[0][0] = esc[1][1] = esc[2][2] = e;
    }
    rot(raiz, rumbo, 0, 0, cero, x, y, z);
    mul(m[0], raiz, esc);
    return;
  }
  if (a->n_h <= 2) { // sombras (1 hueso) o fuegos (2: el resplandor del piso quieto y la llama)
    rot(raiz, rumbo, 0, 0, cero, x, 0, z);
    mul(m[0], raiz, esc);
    if (a->n_h == 2) { // la llama titila y se hamaca alrededor de su base, a la altura `radio`
      float n = 0.5f * sinf(w * 3.1f) + 0.3f * sinf(w * 5.3f + 1.7f) + 0.2f * sinf(w * 9.7f + 0.4f), h = f->radio;
      Mat l = {{1 - 0.08f * n, 0.12f * sinf(w * 1.9f + 2.1f), 0, 0}, {0, 1 + 0.22f * n, 0, 0}, {0, 0, 1, 0}};
      l[0][3] = -l[0][1] * h; l[1][3] = h - l[1][1] * h;
      mul(m[1], m[0], l);
    }
    return;
  }
  int quieto = f->tipo == 0; // apretado contra la barricada
  float bob = quieto ? 0.01f * (1 - cosf(2 * w)) : 0.025f * (1 - cosf(2 * w)), avance = quieto ? 0.05f * s : 0;
  float m_rumbo[3] = {sinf(rumbo), 0, cosf(rumbo)};
  rot(raiz, rumbo, 0, 0, cero, x + avance * m_rumbo[0], bob, z + avance * m_rumbo[2]);
  Mat cadera, l;
  mul(cadera, raiz, esc);
  rot(l, 0, 0, (quieto ? 0.05f : 0.07f) * s, a->piv[0], 0, 0, 0);
  mul(m[0], cadera, l);
  // torso: encorvado, se balancea contra la cadera
  float jor = 0.22f + 0.1f * f->incl * f->incl;
  rot(l, -0.12f * s, jor + (quieto ? 0.1f + 0.08f * sinf(1.3f * w) : 0.05f * sinf(2 * w)), -0.05f * s, a->piv[1], 0, 0, 0);
  mul(m[1], m[0], l);
  // cabeza: caída hacia un lado, cabecea a destiempo
  float cab = 0.1f + 0.09f * sinf(2 * w + 1) + (quieto ? 0.1f * sinf(2.3f * w + fase) : 0);
  rot(l, 0.15f * sinf(0.5f * w + fase), cab, f->incl + 0.06f * sinf(w + 2), a->piv[2], 0, 0, 0);
  mul(m[2], m[1], l);
  // brazos: 0 los dos estirados adelante, 1 colgando, 2 uno y uno; 3 sin el derecho
  for (int lado = 0; lado < 2; lado++) {
    float sg = lado ? -1.f : 1.f, desfase = lado ? PI : 0, ang, abre;
    int estira = f->brazo == 0 || (f->brazo == 2 && lado == 0) || (f->brazo == 3 && lado == 0);
    if (estira) {
      ang = quieto ? -1.75f + 0.28f * sinf(1.7f * w + desfase) : -1.55f + 0.12f * sinf(w + desfase); // el torso ya va encorvado
      abre = sg * (0.12f + 0.06f * sinf(0.9f * w + desfase));
    } else { ang = 0.28f * sinf(w + PI + desfase) + 0.05f; abre = sg * 0.1f; }
    rot(l, 0, ang, abre, a->piv[3 + lado], 0, 0, 0);
    mul(m[3 + lado], m[1], l);
    if (f->brazo == 3 && lado == 1) memset(m[4], 0, sizeof m[4]); // brazo arrancado: se colapsa
  }
  // piernas: arrastra los pies; la de la cojera se mueve menos y queda atrás
  float amp = quieto ? 0.06f : 0.32f;
  for (int lado = 0; lado < 2; lado++) {
    float k = lado == 1 ? 1 - 0.7f * f->cojera : 1, ang = (lado ? -1 : 1) * amp * k * s - (lado == 1 ? 0.12f * f->cojera : 0);
    rot(l, 0, -ang, 0, a->piv[5 + lado], 0, 0, 0);
    mul(m[5 + lado], m[0], l);
  }
}

static void animar(Animado *a, float t) {
  Mat m[MAX_HUESOS];
  for (uint32_t f = 0; f < a->n_fig; f++) {
    pose(a, &a->fig[f], t, m);
    for (uint32_t v = 0; v < a->n_v; v++) {
      uint32_t i = f * a->n_v + v;
      const float *r = a->reposo + 6 * i;
      float (*h)[4] = m[a->hueso[v]];
      uint8_t *o = a->vb + (size_t)(a->datos + i) * a->stride;
      for (int k = 0; k < 3; k++) {
        float p = h[k][0] * r[0] + h[k][1] * r[1] + h[k][2] * r[2] + h[k][3];
        memcpy(o + a->off_pos + 4 * k, &p, 4);
      }
      if (a->off_nor >= 0) for (int k = 0; k < 3; k++) {
        float n = h[k][0] * r[3] + h[k][1] * r[4] + h[k][2] * r[5], e = a->fig[f].escala;
        n /= e > 0 ? e : 1; // la escala es pareja: alcanza con sacarla
        memcpy(o + a->off_nor + 4 * k, &n, 4);
      }
      if (a->fig[f].tipo == 3) for (int k = 0; k < 2; k++) { // la uv corre (repetida: se resta lo entero)
        float d = (k ? a->fig[f].vel : a->fig[f].radio) * t, uv = a->uv[2 * i + k] + d - floorf(d);
        memcpy(o + a->off_tex + 4 * k, &uv, 4);
      }
    }
  }
}

// Lo que hay que subir desde el byte `desde` de un VB (posición y uv en float3/float2 en esos lugares), o NULL si
// no es un modelo animado.
const uint8_t *animado_vertices(Buffer *b, uint32_t stride, uint32_t off_pos, int off_nor, uint32_t off_tex, uint32_t desde) {
  if (!b->anim_estado) detectar(b, stride, off_pos, off_nor, off_tex);
  if (b->anim_estado != 2 || b->anim->stride != stride) return NULL;
  Animado *a = b->anim;
  uint32_t c = cuadros_presentados() + 1;
  if (a->cuadro != c) { animar(a, (float)c / 60.0f); a->cuadro = c; } // 60 cuadros = 1 s, igual en cada corrida
  return a->vb + desde;
}
