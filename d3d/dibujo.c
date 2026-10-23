// Dibujo de Direct3D 9 sobre GLES 3: programas generados para la función fija (y para los vs_1_x traducidos),
// vértices tal cual vienen (D3DCOLOR se reordena en el shader), estado de GL por llamada, Clear y Present.
//
// Convenciones: todo destino es un FBO con la fila 0 arriba (igual que la memoria de D3D), así las texturas
// render target quedan en el orden de D3D. Por eso y se invierte en clip y el sentido de giro también.
// D3D9 centra los píxeles en coordenadas enteras: la geometría se corre medio píxel (+0,5) en ventana.
#include "d3d9_int.h"
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct { int presente; uint32_t stream, offset; uint8_t tipo; } Atrib;
enum { A_POS, A_NOR, A_DIF, A_ESP, A_PSZ, A_T0, A_N = A_T0 + 8 };

typedef struct {
  // vértice
  uint32_t vs_id;
  uint8_t rhw, nor, dif, esp, ntex, luz, nluces, tipos_luz[8], especular, local, normalizar, fuentes, color_vtx;
  uint8_t niebla_vtx, niebla_rango, tci[8], ttf[8], tamtex[8];
  uint16_t bgra;          // atributos D3DCOLOR (por ubicación)
  // fragmento
  uint8_t op[8][9];       // colorop, arg0..2, alphaop, arg0..2, resultarg
  uint8_t tex[8];         // textura puesta en la etapa
  uint8_t alfa_test, niebla_tabla, niebla_on, suma_especular;
} Clave;

typedef struct {
  Clave k;
  GLuint prog;
  GLint u_wv, u_proj, u_nm, u_view, u_vp, u_pix, u_tex[8], u_mat[4], u_pot, u_amb, u_luz[8][7], u_niebla, u_niebla_col,
      u_tfactor, u_const, u_ref, u_s[8], u_c, u_sesgo;
} Programa;

#define MAX_PROG 512
static Programa programas[MAX_PROG];
static int n_prog;
static GLuint vao, vbo[16], ibo;

void dibujo_iniciar(void) {
  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);
  glGenBuffers(16, vbo);
  glGenBuffers(1, &ibo);
  glFrontFace(GL_CCW);
}

// ---------------- destino ----------------
static GLuint fbo_actual, prof_actual;
void aplicar_destino(void) {
  if (!disp) return;
  GLuint f = fbo_de(disp->rt);
  glBindFramebuffer(GL_FRAMEBUFFER, f);
  GLuint p = disp->ds ? disp->ds->prof_rb : 0;
  if (f != fbo_actual || p != prof_actual) {
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, p);
    fbo_actual = f; prof_actual = p;
  }
}

// ---------------- formato de vértices ----------------
static int tam_tipo(uint8_t t, GLint *comp, GLenum *gl, GLboolean *norm) {
  static const struct { GLint c; GLenum g; GLboolean n; int b; } T[] = {
    {1, GL_FLOAT, 0, 4}, {2, GL_FLOAT, 0, 8}, {3, GL_FLOAT, 0, 12}, {4, GL_FLOAT, 0, 16}, {4, GL_UNSIGNED_BYTE, 1, 4},
    {4, GL_UNSIGNED_BYTE, 0, 4}, {2, 0x1402, 0, 4}, {4, 0x1402, 0, 8}, {4, GL_UNSIGNED_BYTE, 1, 4}, {2, 0x1402, 1, 4},
    {4, 0x1402, 1, 8}, {2, GL_UNSIGNED_SHORT, 1, 4}, {4, GL_UNSIGNED_SHORT, 1, 8},
  };
  if (t > 12) return 0;
  *comp = T[t].c; *gl = T[t].g; *norm = T[t].n;
  return T[t].b;
}

// Atributos de la función fija a partir del FVF o de la declaración.
static void formato(Atrib a[A_N], int *rhw) {
  memset(a, 0, sizeof(Atrib) * A_N);
  *rhw = 0;
  if (disp->decl) {
    for (int i = 0; i < disp->decl->n; i++) {
      const Elemento *e = &disp->decl->e[i];
      int k = -1;
      switch (e->uso) {
        case 0: k = A_POS; break;
        case 9: k = A_POS; *rhw = 1; break;
        case 3: k = A_NOR; break;
        case 4: k = A_PSZ; break;
        case 10: k = e->indice == 0 ? A_DIF : e->indice == 1 ? A_ESP : -1; break;
        case 5: k = e->indice < 8 ? A_T0 + e->indice : -1; break;
      }
      if (k >= 0 && (e->uso != 0 || e->indice == 0)) a[k] = (Atrib){1, e->stream, e->offset, e->tipo};
    }
    return;
  }
  uint32_t f = disp->fvf, off = 0;
  switch (f & 0x400e) {
    case 0x002: a[A_POS] = (Atrib){1, 0, off, 2}; off += 12; break;
    case 0x004: a[A_POS] = (Atrib){1, 0, off, 3}; off += 16; *rhw = 1; break;
    case 0x4002: a[A_POS] = (Atrib){1, 0, off, 3}; off += 16; break;
    default: // XYZB1..B5: posición y pesos de mezcla (los pesos no se usan: el juego no mezcla en el device)
      a[A_POS] = (Atrib){1, 0, off, 2};
      off += 12 + 4u * (((f & 0xe) - 4) / 2);
  }
  if (f & 0x10) { a[A_NOR] = (Atrib){1, 0, off, 2}; off += 12; }
  if (f & 0x20) { a[A_PSZ] = (Atrib){1, 0, off, 0}; off += 4; }
  if (f & 0x40) { a[A_DIF] = (Atrib){1, 0, off, 4}; off += 4; }
  if (f & 0x80) { a[A_ESP] = (Atrib){1, 0, off, 4}; off += 4; }
  uint32_t nt = (f >> 8) & 15;
  for (uint32_t i = 0; i < nt && i < 8; i++) {
    uint32_t cod = (f >> (16 + 2 * i)) & 3;
    uint8_t tipo = cod == 0 ? 1 : cod == 1 ? 2 : cod == 2 ? 3 : 0;
    a[A_T0 + i] = (Atrib){1, 0, off, tipo};
    off += cod == 0 ? 8 : cod == 1 ? 12 : cod == 2 ? 16 : 4;
  }
}

// ---------------- generación de GLSL ----------------
typedef struct { char *s; size_t n, cap; } Texto;
static void ap(Texto *t, const char *fmt, ...) {
  va_list va;
  for (;;) {
    va_start(va, fmt);
    int w = vsnprintf(t->s + t->n, t->cap - t->n, fmt, va);
    va_end(va);
    if (w >= 0 && (size_t)w < t->cap - t->n) { t->n += (size_t)w; return; }
    t->cap = t->cap ? t->cap * 2 : 8192;
    t->s = realloc(t->s, t->cap);
  }
}

static const char *CABECERA = "#version 300 es\nprecision highp float;\nprecision highp int;\n";

// D3D clip -> GL: y invertida (FBO con la fila 0 arriba), medio píxel y z de [0,1] a [-1,1]. Antes, x * u_pix.z +
// w * u_pix.w: el modo ancho (ver ancho_iniciar).
static const char *A_GL =
  "vec4 a_gl(vec4 p) { p.x = p.x * u_pix.z + p.w * u_pix.w; return vec4(p.x + p.w * u_pix.x, -p.y + p.w * u_pix.y, 2.0 * p.z - p.w, p.w); }\n";

static void vertice_ffp(Texto *t, const Clave *k) {
  ap(t, "%s", CABECERA);
  ap(t, "layout(location=0) in vec4 a_pos;\nlayout(location=1) in vec3 a_nor;\nlayout(location=2) in vec4 a_dif;\n"
        "layout(location=3) in vec4 a_esp;\n");
  for (int i = 0; i < 8; i++) ap(t, "layout(location=%d) in vec4 a_t%d;\n", A_T0 + i, i);
  ap(t, "uniform mat4 u_wv, u_proj, u_nm, u_view, u_tex[8];\nuniform vec4 u_vp, u_pix, u_mat[4], u_amb, u_niebla;\n"
        "uniform float u_pot;\nuniform vec4 u_luz[56];\n");
  ap(t, "out vec4 v_dif, v_esp;\nout vec4 v_t[8];\nout float v_niebla;\n%s", A_GL);
  ap(t, "void main() {\n");
  ap(t, "  vec4 dif = %s, esp = %s;\n", k->dif ? (k->bgra & (1 << A_DIF) ? "a_dif.zyxw" : "a_dif") : "vec4(1.0)",
     k->esp ? (k->bgra & (1 << A_ESP) ? "a_esp.zyxw" : "a_esp") : "vec4(0.0)");
  ap(t, "  vec3 pv = vec3(0.0), nv = vec3(0.0);\n");
  if (k->rhw) {
    ap(t, "  float w = a_pos.w == 0.0 ? 1.0 : 1.0 / a_pos.w;\n"
          "  vec2 ndc = (a_pos.xy + 0.5 - u_vp.xy) * 2.0 * u_vp.zw - 1.0;\n"
          "  ndc.x = ndc.x * u_pix.z + u_pix.w;\n"
          "  gl_Position = vec4(ndc * w, (2.0 * a_pos.z - 1.0) * w, w);\n");
  } else {
    ap(t, "  vec4 p4 = u_wv * vec4(a_pos.xyz, 1.0);\n  pv = p4.xyz;\n  gl_Position = a_gl(u_proj * p4);\n");
    if (k->nor) ap(t, "  nv = (u_nm * vec4(a_nor, 0.0)).xyz;\n%s", k->normalizar ? "  nv = normalize(nv);\n" : "");
  }
  if (k->luz && !k->rhw) {
    // Iluminación de D3D9 por vértice, en espacio de cámara. u_luz por luz: dif, esp, amb, pos, dir, atenuación, spot.
    const char *fuente[4];
    for (int i = 0; i < 4; i++) { // difuso, ambiente, especular, emisivo
      int f = (k->fuentes >> (2 * i)) & 3;
      fuente[i] = !k->color_vtx || f == 0 ? NULL : f == 1 ? (k->dif ? "dif" : NULL) : (k->esp ? "esp" : NULL);
    }
    ap(t, "  vec4 md = %s, ma = %s, ms = %s, me = %s;\n", fuente[0] ? fuente[0] : "u_mat[0]", fuente[1] ? fuente[1] : "u_mat[1]",
       fuente[2] ? fuente[2] : "u_mat[2]", fuente[3] ? fuente[3] : "u_mat[3]");
    ap(t, "  vec3 amb = u_amb.rgb, cd = vec3(0.0), cs = vec3(0.0);\n");
    for (int i = 0; i < k->nluces; i++) {
      int b = i * 7;
      ap(t, "  {\n    vec3 l; float at = 1.0;\n");
      if (k->tipos_luz[i] == 3) ap(t, "    l = -normalize(u_luz[%d].xyz);\n", b + 4);
      else {
        ap(t, "    vec3 d = u_luz[%d].xyz - pv; float dd = length(d); l = d / dd;\n", b + 3);
        ap(t, "    at = dd > u_luz[%d].x ? 0.0 : 1.0 / (u_luz[%d].y + u_luz[%d].z * dd + u_luz[%d].w * dd * dd);\n", b + 5, b + 5, b + 5, b + 5);
        if (k->tipos_luz[i] == 2) {
          ap(t, "    float rho = dot(-l, normalize(u_luz[%d].xyz));\n", b + 4);
          ap(t, "    at *= rho > u_luz[%d].y ? 1.0 : rho <= u_luz[%d].z ? 0.0 : pow((rho - u_luz[%d].z) / (u_luz[%d].y - u_luz[%d].z), u_luz[%d].x);\n",
             b + 6, b + 6, b + 6, b + 6, b + 6, b + 6);
        }
      }
      ap(t, "    amb += u_luz[%d].rgb * at;\n", b + 2);
      ap(t, "    float nl = max(dot(nv, l), 0.0);\n    cd += u_luz[%d].rgb * nl * at;\n", b);
      if (k->especular)
        ap(t, "    vec3 h = normalize(%s + l);\n    if (nl > 0.0) cs += u_luz[%d].rgb * pow(max(dot(nv, h), 0.0), u_pot) * at;\n",
           k->local ? "normalize(-pv)" : "vec3(0.0, 0.0, -1.0)", b + 1);
      ap(t, "  }\n");
    }
    ap(t, "  dif = vec4(me.rgb + ma.rgb * amb + md.rgb * cd, md.a);\n");
    ap(t, "  esp = %s;\n", k->especular ? "vec4(ms.rgb * cs, ms.a)" : "vec4(0.0)");
  }
  ap(t, "  v_dif = clamp(dif, 0.0, 1.0);\n  v_esp = clamp(esp, 0.0, 1.0);\n");
  for (int i = 0; i < 8; i++) {
    if (!k->tex[i] && !k->ttf[i]) { ap(t, "  v_t[%d] = vec4(0.0);\n", i); continue; }
    int modo = k->tci[i] >> 4, cual = k->tci[i] & 7;
    const char *src = modo == 1 ? "vec4(nv, 1.0)" : modo == 2 ? "vec4(pv, 1.0)" : modo == 3 ? "vec4(reflect(normalize(pv), nv), 1.0)" : NULL;
    char base[64];
    if (!src) {
      int tam = k->tamtex[cual];
      snprintf(base, sizeof base, tam == 1 ? "vec4(a_t%d.x, 1.0, 0.0, 0.0)" : tam == 2 ? "vec4(a_t%d.xy, 1.0, 0.0)" : tam == 3 ? "vec4(a_t%d.xyz, 1.0)" : "a_t%d", cual);
      src = base;
    }
    if (k->ttf[i] & 7) ap(t, "  v_t[%d] = u_tex[%d] * %s;\n", i, i, src);
    else ap(t, "  v_t[%d] = %s;\n", i, src);
  }
  // Niebla por vértice: factor 1 = sin niebla. En vértices transformados la trae el alfa del especular.
  if (!k->niebla_on || k->niebla_tabla) ap(t, "  v_niebla = 1.0;\n");
  else if (k->rhw) ap(t, "  v_niebla = esp.a;\n");
  else {
    ap(t, "  float dz = %s;\n", k->niebla_rango ? "length(pv)" : "abs(pv.z)");
    if (k->niebla_vtx == 3) ap(t, "  v_niebla = clamp((u_niebla.y - dz) / (u_niebla.y - u_niebla.x), 0.0, 1.0);\n");
    else if (k->niebla_vtx == 1) ap(t, "  v_niebla = exp(-dz * u_niebla.z);\n");
    else if (k->niebla_vtx == 2) ap(t, "  v_niebla = exp(-(dz * u_niebla.z) * (dz * u_niebla.z));\n");
    else ap(t, "  v_niebla = 1.0;\n");
  }
  ap(t, "}\n");
}

// Argumento de una etapa de textura.
static void argumento(char *out, size_t tam, int a, int alfa) {
  const char *base;
  switch (a & 7) {
    case 0: base = "dif"; break;
    case 1: base = "cur"; break;
    case 2: base = "tx"; break;
    case 3: base = "u_tfactor"; break;
    case 4: base = "esp"; break;
    case 5: base = "tmp"; break;
    default: base = "cst"; break;
  }
  char x[48];
  if (a & 0x20 || alfa) snprintf(x, sizeof x, "vec4(%s.a)", base);
  else snprintf(x, sizeof x, "%s", base);
  if (a & 0x10) snprintf(out, tam, "(vec4(1.0) - %s)", x);
  else snprintf(out, tam, "%s", x);
}

static void operacion(Texto *t, int op, const char *a0, const char *a1, const char *a2, const char *dest, const char *mascara) {
  char e[512];
  switch (op) {
    case 2: snprintf(e, sizeof e, "%s", a1); break;
    case 3: snprintf(e, sizeof e, "%s", a2); break;
    case 4: snprintf(e, sizeof e, "%s * %s", a1, a2); break;
    case 5: snprintf(e, sizeof e, "%s * %s * 2.0", a1, a2); break;
    case 6: snprintf(e, sizeof e, "%s * %s * 4.0", a1, a2); break;
    case 7: snprintf(e, sizeof e, "%s + %s", a1, a2); break;
    case 8: snprintf(e, sizeof e, "%s + %s - 0.5", a1, a2); break;
    case 9: snprintf(e, sizeof e, "(%s + %s - 0.5) * 2.0", a1, a2); break;
    case 10: snprintf(e, sizeof e, "%s - %s", a1, a2); break;
    case 11: snprintf(e, sizeof e, "%s + %s * (vec4(1.0) - %s)", a1, a2, a1); break;
    case 12: snprintf(e, sizeof e, "mix(%s, %s, dif.a)", a2, a1); break;
    case 13: snprintf(e, sizeof e, "mix(%s, %s, tx.a)", a2, a1); break;
    case 14: snprintf(e, sizeof e, "mix(%s, %s, u_tfactor.a)", a2, a1); break;
    case 15: snprintf(e, sizeof e, "%s + %s * (1.0 - tx.a)", a1, a2); break;
    case 16: snprintf(e, sizeof e, "mix(%s, %s, cur.a)", a2, a1); break;
    case 18: snprintf(e, sizeof e, "vec4(%s.rgb + %s.a * %s.rgb, %s.a)", a1, a1, a2, a1); break;
    case 19: snprintf(e, sizeof e, "vec4(%s.rgb * %s.rgb + %s.a, %s.a)", a1, a2, a1, a1); break;
    case 20: snprintf(e, sizeof e, "vec4((1.0 - %s.a) * %s.rgb + %s.rgb, %s.a)", a1, a2, a1, a1); break;
    case 21: snprintf(e, sizeof e, "vec4((vec3(1.0) - %s.rgb) * %s.rgb + %s.a, %s.a)", a1, a2, a1, a1); break;
    case 24: snprintf(e, sizeof e, "vec4(dot((%s.rgb - 0.5) * 2.0, (%s.rgb - 0.5) * 2.0))", a1, a2); break;
    case 25: snprintf(e, sizeof e, "%s + %s * %s", a0, a1, a2); break;
    case 26: snprintf(e, sizeof e, "mix(%s, %s, %s)", a2, a1, a0); break;
    default: snprintf(e, sizeof e, "%s", a1); break; // PREMODULATE, BUMPENVMAP*: no aparecen en el juego
  }
  ap(t, "    %s%s = clamp((%s)%s, 0.0, 1.0);\n", dest, mascara, e, mascara);
}

static void fragmento(Texto *t, const Clave *k) {
  ap(t, "%s", CABECERA);
  ap(t, "in vec4 v_dif, v_esp;\nin vec4 v_t[8];\nin float v_niebla;\nout vec4 o_col;\n");
  ap(t, "uniform sampler2D u_s[8];\nuniform vec4 u_tfactor, u_const[8], u_niebla_col, u_niebla;\nuniform float u_ref, u_sesgo[8];\n");
  ap(t, "void main() {\n  vec4 dif = v_dif, esp = v_esp, cur = v_dif, tmp = vec4(0.0), tx, cst;\n");
  for (int i = 0; i < 8; i++) {
    const uint8_t *o = k->op[i];
    if (o[0] == 1) break; // D3DTOP_DISABLE corta la cascada
    ap(t, "  {\n");
    if (k->tex[i]) {
      // Con el sesgo de LOD de D3DSAMP_MIPMAPLODBIAS (GLES 3 no lo tiene como parámetro de textura).
      if (k->ttf[i] & 0x10) ap(t, "    tx = textureProj(u_s[%d], v_t[%d].xyw, u_sesgo[%d]);\n", i, i, i);
      else ap(t, "    tx = texture(u_s[%d], v_t[%d].xy, u_sesgo[%d]);\n", i, i, i);
    } else ap(t, "    tx = vec4(1.0);\n");
    ap(t, "    cst = u_const[%d];\n", i);
    char a0[64], a1[64], a2[64];
    const char *dest = o[8] == 5 ? "tmp" : "cur";
    ap(t, "    vec4 ant = %s;\n", dest);
    argumento(a0, sizeof a0, o[1], 0); argumento(a1, sizeof a1, o[2], 0); argumento(a2, sizeof a2, o[3], 0);
    operacion(t, o[0], a0, a1, a2, dest, ".rgb");
    if (o[4] != 1) {
      argumento(a0, sizeof a0, o[5], 1); argumento(a1, sizeof a1, o[6], 1); argumento(a2, sizeof a2, o[7], 1);
      operacion(t, o[4], a0, a1, a2, dest, ".a");
    } else ap(t, "    %s.a = ant.a;\n", dest);
    if (o[0] == 24) ap(t, "    %s.a = %s.r;\n", dest, dest); // DOTPRODUCT3 replica en el alfa
    ap(t, "  }\n");
  }
  if (k->suma_especular) ap(t, "  cur.rgb = clamp(cur.rgb + esp.rgb, 0.0, 1.0);\n");
  if (k->niebla_on) {
    if (k->niebla_tabla) {
      ap(t, "  float dz = 1.0 / gl_FragCoord.w, f;\n");
      if (k->niebla_tabla == 3) ap(t, "  f = clamp((u_niebla.y - dz) / (u_niebla.y - u_niebla.x), 0.0, 1.0);\n");
      else if (k->niebla_tabla == 1) ap(t, "  f = exp(-dz * u_niebla.z);\n");
      else ap(t, "  f = exp(-(dz * u_niebla.z) * (dz * u_niebla.z));\n");
      ap(t, "  cur.rgb = mix(u_niebla_col.rgb, cur.rgb, f);\n");
    } else ap(t, "  cur.rgb = mix(u_niebla_col.rgb, cur.rgb, clamp(v_niebla, 0.0, 1.0));\n");
  }
  if (k->alfa_test && k->alfa_test != 8) {
    static const char *cmp[] = {"", "false", "<", "==", "<=", ">", "!=", ">=", "true"};
    if (k->alfa_test == 1) ap(t, "  discard;\n");
    else ap(t, "  if (!(floor(cur.a * 255.0 + 0.5) %s u_ref)) discard;\n", cmp[k->alfa_test]);
  }
  ap(t, "  o_col = cur;\n}\n");
}

GLuint compilar(GLenum tipo, const char *src) {
  GLuint s = glCreateShader(tipo);
  glShaderSource(s, 1, &src, NULL);
  glCompileShader(s);
  GLint ok;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[4096];
    glGetShaderInfoLog(s, sizeof log, NULL, log);
    fprintf(stderr, "error compilando shader:\n%s\n--- fuente ---\n%s\n", log, src);
    exit(4);
  }
  return s;
}

static Programa *programa(const Clave *k) {
  for (int i = 0; i < n_prog; i++) if (!memcmp(&programas[i].k, k, sizeof *k)) return &programas[i];
  if (n_prog >= MAX_PROG) { fprintf(stderr, "demasiados programas GL\n"); exit(4); }
  Programa *p = &programas[n_prog++];
  memset(p, 0, sizeof *p);
  p->k = *k;
  Texto v = {0}, f = {0};
  if (k->vs_id) {
    ShaderV *vs = disp->vs;
    ap(&v, "%s", CABECERA);
    for (int i = 0; i < 16; i++) if (vs->uso[i] != 0xff) ap(&v, "layout(location=%d) in vec4 a_v%d;\n", i, i);
    ap(&v, "uniform vec4 c[256];\nuniform vec4 u_pix;\nout vec4 v_dif, v_esp;\nout vec4 v_t[8];\nout float v_niebla;\n%s", A_GL);
    ap(&v, "void main() {\n");
    for (int i = 0; i < 16; i++) if (vs->uso[i] != 0xff) ap(&v, "  vec4 v%d = a_v%d%s;\n", i, i, k->bgra & (1 << i) ? ".zyxw" : "");
    ap(&v, "%s", vs->glsl);
    ap(&v, "}\n");
  } else vertice_ffp(&v, k);
  fragmento(&f, k);
  GLuint vsh = compilar(GL_VERTEX_SHADER, v.s), fsh = compilar(GL_FRAGMENT_SHADER, f.s);
  p->prog = glCreateProgram();
  glAttachShader(p->prog, vsh);
  glAttachShader(p->prog, fsh);
  glLinkProgram(p->prog);
  GLint ok;
  glGetProgramiv(p->prog, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[4096];
    glGetProgramInfoLog(p->prog, sizeof log, NULL, log);
    fprintf(stderr, "error enlazando:\n%s\n%s\n%s\n", log, v.s, f.s);
    exit(4);
  }
  free(v.s); free(f.s);
  GLuint g = p->prog;
#define U(campo, nombre) p->campo = glGetUniformLocation(g, nombre)
  U(u_wv, "u_wv"); U(u_proj, "u_proj"); U(u_nm, "u_nm"); U(u_view, "u_view"); U(u_vp, "u_vp"); U(u_pix, "u_pix");
  U(u_pot, "u_pot"); U(u_amb, "u_amb"); U(u_niebla, "u_niebla"); U(u_niebla_col, "u_niebla_col"); U(u_tfactor, "u_tfactor");
  U(u_const, "u_const"); U(u_ref, "u_ref"); U(u_c, "c"); U(u_sesgo, "u_sesgo");
  p->u_mat[0] = glGetUniformLocation(g, "u_mat");
  p->u_luz[0][0] = glGetUniformLocation(g, "u_luz");
  p->u_tex[0] = glGetUniformLocation(g, "u_tex");
  glUseProgram(g);
  GLint s = glGetUniformLocation(g, "u_s");
  if (s >= 0) for (int i = 0; i < 8; i++) glUniform1i(s + i, i);
  return p;
}

// ---------------- estado de GL ----------------
static GLenum gl_cmp(uint32_t f) { return f >= 1 && f <= 8 ? GL_NEVER + f - 1 : GL_ALWAYS; }
static GLenum gl_blend(uint32_t b) {
  static const GLenum T[] = {GL_ONE, GL_ZERO, GL_ONE, GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                             GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA, GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR, GL_SRC_ALPHA_SATURATE,
                             GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_CONSTANT_COLOR, GL_ONE_MINUS_CONSTANT_COLOR};
  return b < 16 ? T[b] : GL_ONE;
}
static GLenum gl_op(uint32_t o) {
  static const GLenum T[] = {GL_FUNC_ADD, GL_FUNC_ADD, GL_FUNC_SUBTRACT, GL_FUNC_REVERSE_SUBTRACT, GL_MIN, GL_MAX};
  return o < 6 ? T[o] : GL_FUNC_ADD;
}
static GLenum gl_stencil(uint32_t o) {
  static const GLenum T[] = {GL_KEEP, GL_KEEP, GL_ZERO, GL_REPLACE, GL_INCR, GL_DECR, GL_INVERT, GL_INCR_WRAP, GL_DECR_WRAP};
  return o < 9 ? T[o] : GL_KEEP;
}
static void color4(uint32_t c, float *f) { f[0] = ((c >> 16) & 255) / 255.0f; f[1] = ((c >> 8) & 255) / 255.0f; f[2] = (c & 255) / 255.0f; f[3] = (c >> 24) / 255.0f; }

// ---------------- modo ancho ----------------
// El backbuffer real tiene ancho_extra columnas más que el del juego (800) y el juego se dibuja centrado: la x del
// clip se multiplica por 800 / ancho real, así lo 2D queda a 1:1 y lo 3D muestra más escenario a los costados (la
// proyección del juego ya se extiende fuera de su pantalla). Lo 2D que ocupa todo el ancho (fondos, bandas del
// HUD, fundidos) se extiende además a los costados; el resto de lo 2D se recorta a la zona del juego.
int ancho_extra;
static int ancho_auto; // el ancho salió del tamaño de la pantalla (y se rehace si cambia)
static float ancho_a = 1, ancho_b = 0; // x' = x * a + w * b, por pasada (u_pix.zw)

// Ancho de la pantalla llevado a la altura del juego (par), si es más ancha que el juego y está en pantalla
// completa. HINO_ANCHO=0 lo apaga (para comparar píxel a píxel con el original, que es 4:3); HINO_ANCHO=1332 lo
// fuerza a ese ancho (para probarlo en una ventana).
void ancho_iniciar(SDL_Window *w, int pantalla_completa, uint32_t bw, uint32_t bh) {
  ancho_extra = 0;
  const char *e = getenv("HINO_ANCHO");
  if (e && atoi(e) > (int)bw) { ancho_extra = (atoi(e) - (int)bw) & ~1; return; }
  if (!pantalla_completa || (e && !strcmp(e, "0")) || !bh) return;
  int sw = 0, sh = 0;
#ifdef SDL_PLATFORM_ANDROID
  SDL_GetWindowSizeInPixels(w, &sw, &sh);
#else
  const SDL_DisplayMode *m = SDL_GetDesktopDisplayMode(SDL_GetDisplayForWindow(w));
  if (m) { sw = m->w; sh = m->h; }
#endif
  if (sw <= 0 || sh <= 0) return;
  ancho_auto = 1;
  int ancho = (int)((double)bh * sw / sh) & ~1;
  ancho_extra = SDL_max(0, ancho - (int)bw);
  fprintf(stderr, "modo ancho: %ux%u se dibuja en %ux%u (pantalla %dx%d)\n", bw, bh, bw + (uint32_t)ancho_extra, bh, sw, sh);
}

// Si cambia el tamaño de la pantalla se rehace el ancho: Android arranca mostrando las barras del sistema y recién
// después pasa a pantalla completa (en un Galaxy A52, de 2186x1017 a 2400x1080), y con el ancho viejo quedaban
// franjas negras. Se reasignan el backbuffer y su profundidad; el juego sigue viendo 800x600.
static void ancho_ajustar(int sw, int sh) {
  Dispositivo *d = disp;
  if (!ancho_auto || sw <= 0 || sh <= 0) return;
  int extra = SDL_max(0, ((int)((double)d->backbuffer->h * sw / sh) & ~1) - (int)d->backbuffer->w);
  if (extra == ancho_extra) return;
  ancho_extra = extra;
  Superficie *sup[2] = {d->backbuffer, d->prof_auto};
  for (int i = 0; i < 2; i++) {
    if (!sup[i]) continue;
    sup[i]->w_gl = sup[i]->w + (uint32_t)extra;
    glBindRenderbuffer(GL_RENDERBUFFER, sup[i]->profundidad ? sup[i]->prof_rb : sup[i]->color_rb);
    glRenderbufferStorage(GL_RENDERBUFFER, sup[i]->profundidad ? GL_DEPTH24_STENCIL8 : GL_RGBA8, (GLsizei)sup[i]->w_gl, (GLsizei)sup[i]->h);
  }
  fprintf(stderr, "modo ancho: ahora %ux%u (pantalla %dx%d)\n", d->backbuffer->w_gl, d->backbuffer->h, sw, sh);
}

static int ancho_activo(void) { return ancho_extra && disp->rt == disp->backbuffer; }
static int vp_completo(void) { return disp->vp[0] == 0 && disp->vp[2] == disp->backbuffer->w; }

// [x0, x1) del juego a píxeles del backbuffer real: lo de todo el ancho se ensancha, lo demás se corre al centro.
static void a_real(int32_t *x0, int32_t *x1) {
  if (!ancho_activo()) return;
  if (*x0 <= 0 && *x1 >= (int32_t)disp->backbuffer->w) *x1 += ancho_extra;
  else { *x0 += ancho_extra / 2; *x1 += ancho_extra / 2; }
}

static void estado_gl(int rhw, int centro) {
  Dispositivo *d = disp;
  uint32_t *rs = d->rs;
  int32_t vx0 = (int32_t)d->vp[0], vx1 = vx0 + (int32_t)d->vp[2];
  a_real(&vx0, &vx1);
  glViewport(vx0, (GLint)d->vp[1], vx1 - vx0, (GLsizei)d->vp[3]);
  if (rhw) glDepthRangef(0, 1); else glDepthRangef(bits_f(d->vp[4]), bits_f(d->vp[5]));
  // Tijera del juego (llevada al backbuffer real) y, en modo ancho, el recorte de lo 2D a la zona del juego.
  int32_t t[4] = {(int32_t)d->tijera[0], (int32_t)d->tijera[1], (int32_t)(d->tijera[0] + d->tijera[2]), (int32_t)(d->tijera[1] + d->tijera[3])};
  int hay = rs[174] != 0;
  if (hay) a_real(&t[0], &t[2]);
  if (centro) {
    int32_t c0 = ancho_extra / 2, c1 = c0 + (int32_t)d->backbuffer->w;
    if (!hay) { t[0] = c0; t[1] = 0; t[2] = c1; t[3] = (int32_t)d->backbuffer->h; hay = 1; }
    else { t[0] = SDL_max(t[0], c0); t[2] = SDL_min(t[2], c1); }
  }
  if (hay) { glEnable(GL_SCISSOR_TEST); glScissor(t[0], t[1], SDL_max(0, t[2] - t[0]), SDL_max(0, t[3] - t[1])); }
  else glDisable(GL_SCISSOR_TEST);
  if (rs[27]) {
    glEnable(GL_BLEND);
    uint32_t s = rs[19], t = rs[20], sa = rs[206] ? rs[207] : s, ta = rs[206] ? rs[208] : t;
    if (s == 12) { s = 5; t = 6; }
    if (s == 13) { s = 6; t = 5; }
    if (sa == 12) { sa = 5; ta = 6; }
    if (sa == 13) { sa = 6; ta = 5; }
    glBlendFuncSeparate(gl_blend(s), gl_blend(t), gl_blend(sa), gl_blend(ta));
    glBlendEquationSeparate(gl_op(rs[171]), gl_op(rs[206] ? rs[209] : rs[171]));
    float bc[4];
    color4(rs[193], bc);
    glBlendColor(bc[0], bc[1], bc[2], bc[3]);
  } else glDisable(GL_BLEND);
  if (rs[7] && d->ds) { glEnable(GL_DEPTH_TEST); glDepthFunc(gl_cmp(rs[23])); } else glDisable(GL_DEPTH_TEST);
  glDepthMask(rs[14] && d->ds ? 1 : 0);
  if (rs[52] && d->ds) {
    glEnable(GL_STENCIL_TEST);
    // Con y invertida, la cara "horaria" de D3D es la delantera de GL.
    glStencilFuncSeparate(GL_FRONT, gl_cmp(rs[56]), (GLint)rs[57], rs[58]);
    glStencilOpSeparate(GL_FRONT, gl_stencil(rs[53]), gl_stencil(rs[54]), gl_stencil(rs[55]));
    if (rs[185]) {
      glStencilFuncSeparate(GL_BACK, gl_cmp(rs[189]), (GLint)rs[57], rs[58]);
      glStencilOpSeparate(GL_BACK, gl_stencil(rs[186]), gl_stencil(rs[187]), gl_stencil(rs[188]));
    } else {
      glStencilFuncSeparate(GL_BACK, gl_cmp(rs[56]), (GLint)rs[57], rs[58]);
      glStencilOpSeparate(GL_BACK, gl_stencil(rs[53]), gl_stencil(rs[54]), gl_stencil(rs[55]));
    }
    glStencilMaskSeparate(GL_FRONT, rs[59]);
    glStencilMaskSeparate(GL_BACK, rs[59]);
  } else glDisable(GL_STENCIL_TEST);
  if (rs[22] == 1) glDisable(GL_CULL_FACE);
  else { glEnable(GL_CULL_FACE); glCullFace(rs[22] == 2 ? GL_FRONT : GL_BACK); }
  uint32_t m = rs[168];
  glColorMask(m & 1, (m >> 1) & 1, (m >> 2) & 1, (m >> 3) & 1);
  float pend = bits_f(rs[175]), sesgo = bits_f(rs[195]);
  if (pend != 0 || sesgo != 0) { glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(pend, sesgo * 16777215.0f); }
  else glDisable(GL_POLYGON_OFFSET_FILL);
}

static void texturas(const Clave *k) {
  for (int i = 0; i < 8; i++) {
    if (!k->tex[i]) continue;
    Textura *t = disp->tex[i];
    uint32_t *s = disp->ss[i];
    glActiveTexture(GL_TEXTURE0 + (GLenum)i);
    glBindTexture(GL_TEXTURE_2D, t->gl);
    GLint mag = s[5] >= 2 ? GL_LINEAR : GL_NEAREST, min;
    int lin = s[6] >= 2;
    if (s[7] == 0 || t->niveles == 1) min = lin ? GL_LINEAR : GL_NEAREST;
    else if (s[7] == 1) min = lin ? GL_LINEAR_MIPMAP_NEAREST : GL_NEAREST_MIPMAP_NEAREST;
    else min = lin ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_LINEAR;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min);
    static const GLint dir[] = {GL_REPEAT, GL_REPEAT, GL_MIRRORED_REPEAT, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, GL_MIRRORED_REPEAT};
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, dir[s[1] < 6 ? s[1] : 1]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, dir[s[2] < 6 ? s[2] : 1]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, (GLint)(s[9] < t->niveles ? s[9] : 0));
  }
  glActiveTexture(GL_TEXTURE0);
}

// Producto de matrices de D3D (fila por columna, como D3DXMatrixMultiply).
static void mul(const float *a, const float *b, float *r) {
  for (int f = 0; f < 4; f++) for (int k = 0; k < 4; k++)
    r[f * 4 + k] = a[f * 4] * b[k] + a[f * 4 + 1] * b[4 + k] + a[f * 4 + 2] * b[8 + k] + a[f * 4 + 3] * b[12 + k];
}
// Inversa traspuesta de la parte 3x3 (para las normales), dentro de una 4x4.
static void normal_de(const float *m, float *r) {
  float a = m[0], b = m[1], cc = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
  float det = a * (e * i - f * h) - b * (d * i - f * g) + cc * (d * h - e * g);
  float id = det != 0 ? 1.0f / det : 0;
  memset(r, 0, 64);
  r[0] = (e * i - f * h) * id; r[1] = -(d * i - f * g) * id; r[2] = (d * h - e * g) * id;
  r[4] = -(b * i - cc * h) * id; r[5] = (a * i - cc * g) * id; r[6] = -(a * h - b * g) * id;
  r[8] = (b * f - cc * e) * id; r[9] = -(a * f - cc * d) * id; r[10] = (a * e - b * d) * id;
  r[15] = 1;
}

// Medio píxel (del viewport real) y la x del modo ancho.
static void subir_pix(Programa *p) {
  Dispositivo *d = disp;
  int32_t vx0 = (int32_t)d->vp[0], vx1 = vx0 + (int32_t)d->vp[2];
  a_real(&vx0, &vx1);
  float pix[4] = {1.0f / (float)(vx1 - vx0), 1.0f / (float)d->vp[3], ancho_a, ancho_b};
  if (p->u_pix >= 0) glUniform4fv(p->u_pix, 1, pix);
}

static void uniformes(Programa *p, const Clave *k) {
  Dispositivo *d = disp;
  subir_pix(p);
  if (k->vs_id) { glUniform4fv(p->u_c, 256, d->vsc[0]); }
  else {
    float vp[4] = {(float)d->vp[0], (float)d->vp[1], 1.0f / (float)d->vp[2], 1.0f / (float)d->vp[3]};
    if (p->u_vp >= 0) glUniform4fv(p->u_vp, 1, vp);
    float wv[16], nm[16];
    mul(d->mat[T_WORLD], d->mat[T_VIEW], wv);
    if (p->u_wv >= 0) glUniformMatrix4fv(p->u_wv, 1, 0, wv);
    if (p->u_proj >= 0) glUniformMatrix4fv(p->u_proj, 1, 0, d->mat[T_PROJ]);
    if (p->u_nm >= 0) { normal_de(wv, nm); glUniformMatrix4fv(p->u_nm, 1, 0, nm); }
    if (p->u_tex[0] >= 0) glUniformMatrix4fv(p->u_tex[0], 8, 0, d->mat[T_TEX0]);
    if (p->u_mat[0] >= 0) glUniform4fv(p->u_mat[0], 4, d->material.diffuse);
    if (p->u_pot >= 0) glUniform1f(p->u_pot, d->material.power);
    float amb[4];
    color4(d->rs[139], amb);
    if (p->u_amb >= 0) glUniform4fv(p->u_amb, 1, amb);
    if (p->u_luz[0][0] >= 0) {
      float lz[56][4];
      memset(lz, 0, sizeof lz);
      int n = 0;
      for (int i = 0; i < 8 && n < 8; i++) {
        if (!d->luz_on[i]) continue;
        const Luz *l = &d->luces[i];
        memcpy(lz[n * 7], l->diffuse, 16); memcpy(lz[n * 7 + 1], l->specular, 16); memcpy(lz[n * 7 + 2], l->ambient, 16);
        // Posición y dirección a espacio de cámara.
        const float *v = d->mat[T_VIEW];
        for (int c3 = 0; c3 < 3; c3++) {
          lz[n * 7 + 3][c3] = l->pos[0] * v[c3] + l->pos[1] * v[4 + c3] + l->pos[2] * v[8 + c3] + v[12 + c3];
          lz[n * 7 + 4][c3] = l->dir[0] * v[c3] + l->dir[1] * v[4 + c3] + l->dir[2] * v[8 + c3];
        }
        lz[n * 7 + 5][0] = l->range; lz[n * 7 + 5][1] = l->at0; lz[n * 7 + 5][2] = l->at1; lz[n * 7 + 5][3] = l->at2;
        lz[n * 7 + 6][0] = l->falloff; lz[n * 7 + 6][1] = cosf(l->theta / 2); lz[n * 7 + 6][2] = cosf(l->phi / 2);
        n++;
      }
      glUniform4fv(p->u_luz[0][0], 56, lz[0]);
    }
  }
  float niebla[4] = {bits_f(d->rs[36]), bits_f(d->rs[37]), bits_f(d->rs[38]), 0}, col[4];
  if (p->u_niebla >= 0) glUniform4fv(p->u_niebla, 1, niebla);
  color4(d->rs[34], col);
  if (p->u_niebla_col >= 0) glUniform4fv(p->u_niebla_col, 1, col);
  color4(d->rs[60], col);
  if (p->u_tfactor >= 0) glUniform4fv(p->u_tfactor, 1, col);
  if (p->u_const >= 0) {
    float cs[8][4];
    for (int i = 0; i < 8; i++) color4(d->tss[i][32], cs[i]);
    glUniform4fv(p->u_const, 8, cs[0]);
  }
  if (p->u_ref >= 0) glUniform1f(p->u_ref, (float)(d->rs[24] & 255));
  if (p->u_sesgo >= 0) {
    float s[8];
    for (int i = 0; i < 8; i++) s[i] = bits_f(d->ss[i][8]);
    glUniform1fv(p->u_sesgo, 8, s);
  }
}

// ---------------- dibujar ----------------
static GLenum modo_gl(uint32_t t, uint32_t n, uint32_t *cuenta) {
  switch (t) {
    case 1: *cuenta = n; return GL_POINTS;
    case 2: *cuenta = 2 * n; return GL_LINES;
    case 3: *cuenta = n + 1; return GL_LINE_STRIP;
    case 4: *cuenta = 3 * n; return GL_TRIANGLES;
    case 5: *cuenta = n + 2; return GL_TRIANGLE_STRIP;
    case 6: *cuenta = n + 2; return GL_TRIANGLE_FAN;
    default: *cuenta = 0; return GL_TRIANGLES;
  }
}

static void armar_clave(Clave *k, const Atrib *a, int rhw) {
  Dispositivo *d = disp;
  memset(k, 0, sizeof *k);
  uint32_t *rs = d->rs;
  if (d->vs) k->vs_id = d->vs->id;
  else {
    k->rhw = (uint8_t)rhw; k->nor = (uint8_t)a[A_NOR].presente; k->dif = (uint8_t)a[A_DIF].presente; k->esp = (uint8_t)a[A_ESP].presente;
    k->luz = rs[137] && !rhw;
    if (k->luz) {
      for (int i = 0; i < 8; i++) if (d->luz_on[i]) k->tipos_luz[k->nluces++] = (uint8_t)d->luces[i].tipo;
      k->especular = (uint8_t)(rs[29] != 0); k->local = (uint8_t)(rs[142] != 0);
      k->fuentes = (uint8_t)((rs[145] & 3) | (rs[147] & 3) << 2 | (rs[146] & 3) << 4 | (rs[148] & 3) << 6);
      k->color_vtx = (uint8_t)(rs[141] != 0);
    }
    k->normalizar = (uint8_t)(rs[143] != 0);
    if (rs[28] && !rs[35]) { k->niebla_vtx = (uint8_t)rs[140]; k->niebla_rango = (uint8_t)(rs[48] != 0); }
    for (int i = 0; i < 8; i++) {
      uint32_t tci = d->tss[i][11];
      k->tci[i] = (uint8_t)((tci & 7) | ((tci >> 16) & 7) << 4);
      k->ttf[i] = (uint8_t)((d->tss[i][24] & 7) | (d->tss[i][24] & 0x100 ? 0x10 : 0));
      int tam = a[A_T0 + i].presente ? (a[A_T0 + i].tipo <= 3 ? a[A_T0 + i].tipo + 1 : 2) : 0;
      k->tamtex[i] = (uint8_t)tam;
    }
  }
  for (int i = 0; i < 8; i++) {
    uint32_t *t = d->tss[i];
    uint8_t o[9] = {(uint8_t)t[1], (uint8_t)t[26], (uint8_t)t[2], (uint8_t)t[3], (uint8_t)t[4], (uint8_t)t[27], (uint8_t)t[5], (uint8_t)t[6], (uint8_t)t[28]};
    memcpy(k->op[i], o, 9);
    k->tex[i] = d->tex[i] != NULL;
    if (t[1] == 1) { for (int j = i; j < 8; j++) { k->op[j][0] = 1; k->tex[j] = 0; } break; }
  }
  if (!d->vs) for (int i = 0; i < 8; i++) if (!k->tex[i]) k->ttf[i] = 0, k->tci[i] = (uint8_t)i;
  k->alfa_test = (uint8_t)(rs[15] ? rs[25] : 0);
  k->niebla_on = (uint8_t)(rs[28] != 0);
  if (rs[28] && rs[35]) k->niebla_tabla = (uint8_t)rs[35];
  if (rs[28] && !rs[35] && !rhw && !d->vs && !rs[140]) k->niebla_on = 0;
  k->suma_especular = (uint8_t)(rs[29] != 0);
}

static uint32_t vs_ids;

static uint32_t cuadro;
// "120,300" contiene a n (listas de HINO_CAPTURAS y HINO_LOG_DIBUJOS).
int en_lista(const char *lista, uint32_t n) {
  if (!lista) return 0;
  char buscado[16];
  snprintf(buscado, sizeof buscado, "%u", n);
  size_t l = strlen(buscado);
  for (const char *p = lista; (p = strstr(p, buscado)); p++)
    if ((p == lista || p[-1] == ',') && (p[l] == ',' || !p[l])) return 1;
  return 0;
}
// HINO_LOG_DIBUJOS="cuadro,...": cada llamada de esos cuadros (para diseñar el modo ancho).
int registrando_dibujos(void) { return en_lista(getenv("HINO_LOG_DIBUJOS"), cuadro + 1); }

// Lo 2D del juego: vértices ya en pantalla (pretransformados o "mov oPos, v0") o con proyección ortogonal.
static int es_2d(int rhw) {
  Dispositivo *d = disp;
  if (rhw) return 1;
  if (d->vs) {
    if (d->vs->pos_tipo == 2) return 1;
    if (d->vs->pos_tipo != 1 || d->vs->pos_c > 252) return 0;
    const float *w = d->vsc[d->vs->pos_c + 3];
    return w[0] == 0 && w[1] == 0 && w[2] == 0 && w[3] == 1;
  }
  const float *pr = d->mat[T_PROJ];
  return pr[3] == 0 && pr[7] == 0 && pr[11] == 0 && pr[15] == 1;
}

// Una capa de pantalla completa casi transparente en los bordes (el texto del modo historia, efectos) no se
// extiende a los costados: se mira el alfa de las franjas de su textura que irían a parar ahí (un sexto por lado).
static int bordes_transparentes(const Atrib *a, uint32_t up_v, uint32_t up_stride, uint32_t v_ini, uint32_t v_n) {
  Dispositivo *d = disp;
  Textura *t = d->tex[0];
  if (!t || !t->alfa || !a[A_T0].presente || a[A_T0].tipo != 1) return 0; // sin textura o sin uv FLOAT2: opaca
  uint32_t s = a[A_T0].stream, stride = up_v ? up_stride : d->stream[s].stride;
  uint32_t va = up_v ? up_v : d->stream[s].b ? d->stream[s].b->va + d->stream[s].off : 0;
  if (!va) return 0;
  float u0 = 1e9f, u1 = -1e9f, v0 = 1e9f, v1 = -1e9f;
  for (uint32_t i = 0; i < v_n; i++) {
    uint32_t p = va + (v_ini + i) * stride + a[A_T0].offset;
    float u = MF32(p), v = MF32(p + 4);
    u0 = SDL_min(u0, u); u1 = SDL_max(u1, u); v0 = SDL_min(v0, v); v1 = SDL_max(v1, v);
  }
  u0 = SDL_clamp(u0, 0, 1); u1 = SDL_clamp(u1, 0, 1); v0 = SDL_clamp(v0, 0, 1); v1 = SDL_clamp(v1, 0, 1);
  float du = (u1 - u0) / 6;
  int cy0 = SDL_min(63, (int)(v0 * 64)), cy1 = SDL_clamp((int)SDL_ceilf(v1 * 64), cy0 + 1, 64);
  uint32_t suma = 0, n = 0;
  for (int lado = 0; lado < 2; lado++) {
    float a0 = lado ? u1 - du : u0, a1 = lado ? u1 : u0 + du;
    int cx0 = SDL_min(63, (int)(a0 * 64)), cx1 = SDL_clamp((int)SDL_ceilf(a1 * 64), cx0 + 1, 64);
    for (int cy = cy0; cy < cy1; cy++)
      for (int cx = cx0; cx < cx1; cx++) { suma += t->alfa[cy * 64 + cx]; n++; }
  }
  return n && suma / n < 128;
}

// Dónde cae el dibujo en la pantalla del juego (píxeles de su viewport: x0, y0, x1, y1), calculando en la CPU la
// posición como el shader: los del juego hacen "m4x4 oPos, v0, cN" o "mov oPos, v0"; también la función fija y
// los pretransformados. *plano = 1 si todos los vértices tienen el mismo w (2D: proyección ortogonal o ya en
// pantalla). Devuelve 0 si no se puede saber (otro shader, vértices detrás de la cámara).
static int caja_pantalla(const Atrib *a, uint32_t up_v, uint32_t up_stride, uint32_t v_ini, uint32_t v_n, int rhw, float c[4], int *plano) {
  Dispositivo *d = disp;
  uint32_t s = a[A_POS].stream, stride = up_v ? up_stride : d->stream[s].stride;
  uint32_t va = up_v ? up_v : d->stream[s].b ? d->stream[s].b->va + d->stream[s].off : 0;
  if (!a[A_POS].presente || !va || a[A_POS].tipo > 3) return 0;
  float m[16];
  const float *filas = NULL; // m4x4 del shader: filas c[N..N+3]
  if (d->vs) {
    if (d->vs->pos_tipo == 1 && d->vs->pos_c <= 252) filas = d->vsc[d->vs->pos_c];
    else if (d->vs->pos_tipo != 2) return 0;
  } else if (!rhw) {
    float wv[16];
    mul(d->mat[T_WORLD], d->mat[T_VIEW], wv);
    mul(wv, d->mat[T_PROJ], m);
  }
  float w0 = 0, vx = (float)d->vp[0], vy = (float)d->vp[1], vw = (float)d->vp[2], vh = (float)d->vp[3];
  c[0] = c[1] = 1e9f; c[2] = c[3] = -1e9f;
  *plano = 1;
  for (uint32_t i = 0; i < v_n; i++) {
    uint32_t p = va + (v_ini + i) * stride + a[A_POS].offset;
    float v[4] = {0, 0, 0, 1}, k[4];
    for (uint32_t j = 0; j <= a[A_POS].tipo; j++) v[j] = MF32(p + 4 * j);
    if (rhw) { k[0] = v[0] + 0.5f; k[1] = v[1] + 0.5f; } // ya en píxeles (centro de D3D9 corrido medio píxel)
    else {
      for (int j = 0; j < 4; j++)
        k[j] = filas ? v[0] * filas[4 * j] + v[1] * filas[4 * j + 1] + v[2] * filas[4 * j + 2] + v[3] * filas[4 * j + 3]
             : d->vs ? v[j] : v[0] * m[j] + v[1] * m[4 + j] + v[2] * m[8 + j] + v[3] * m[12 + j];
      if (k[3] <= 1e-6f) return 0;
      if (i == 0) w0 = k[3]; else if (SDL_fabsf(k[3] - w0) > 1e-4f * SDL_fabsf(w0)) *plano = 0;
      k[0] = vx + (k[0] / k[3] + 1) * 0.5f * vw; k[1] = vy + (1 - k[1] / k[3]) * 0.5f * vh;
    }
    c[0] = SDL_min(c[0], k[0]); c[1] = SDL_min(c[1], k[1]); c[2] = SDL_max(c[2], k[0]); c[3] = SDL_max(c[3], k[1]);
  }
  return 1;
}

void dibujar(int indexado, uint32_t tipo, uint32_t base, uint32_t n_prim, uint32_t ini_ind,
             uint32_t up_v, uint32_t up_stride, uint32_t up_ind, uint32_t fmt_ind) {
  Dispositivo *d = disp;
  if (!n_prim) return;
  uint32_t cuenta;
  GLenum modo = modo_gl(tipo, n_prim, &cuenta);
  // Índices: se copian restando el mínimo, así solo se suben los vértices que se usan.
  uint32_t v_ini = 0, v_n = cuenta;
  uint32_t ancho_ind = 0, ind_va = 0;
  if (indexado) {
    ancho_ind = up_ind ? (fmt_ind == 102 ? 4 : 2) : (d->ib && d->ib->formato == 102 ? 4 : 2);
    ind_va = up_ind ? up_ind : (d->ib ? d->ib->va + ini_ind * ancho_ind : 0);
    if (!ind_va) return;
    uint32_t mn = 0xffffffffu, mx = 0;
    for (uint32_t i = 0; i < cuenta; i++) {
      uint32_t x = ancho_ind == 4 ? M32(ind_va + 4 * i) : M16(ind_va + 2 * i);
      if (x < mn) mn = x;
      if (x > mx) mx = x;
    }
    v_ini = mn; v_n = mx - mn + 1;
  } else if (!up_v) v_ini = base, base = 0;
  if (indexado && !up_v) v_ini += base;

  Atrib a[A_N];
  int rhw;
  formato(a, &rhw);
  if (registrando_dibujos()) {
    float c[4] = {0};
    int plano = 0, ok = caja_pantalla(a, up_v, up_stride, v_ini, v_n, rhw, c, &plano);
    Textura *t0 = d->tex[0];
    fprintf(stderr, "dib %u %s n=%u pant=%.1f,%.1f..%.1f,%.1f tex=%ux%u@%p rt=%s:%ux%u vp=%u,%u,%u,%u vs=%u blend=%u z=%u\n", cuadro + 1,
            !ok ? "??" : plano ? "2D" : "3D", n_prim, c[0], c[1], c[2], c[3], t0 ? t0->w : 0, t0 ? t0->h : 0, (void *)t0,
            d->rt == d->backbuffer ? "bb" : "rt", d->rt->w, d->rt->h, d->vp[0], d->vp[1], d->vp[2], d->vp[3], d->vs ? d->vs->id : 0, d->rs[27], d->rs[7]);
  }
  aplicar_destino();
  glBindVertexArray(vao);
  Clave k;
  // Vértices: cada stream usado se sube en el rango [v_ini, v_ini + v_n).
  uint16_t bgra = 0;
  for (int i = 0; i < 16; i++) glDisableVertexAttribArray((GLuint)i);
  int ubic[A_N];
  for (int i = 0; i < A_N; i++) ubic[i] = -1;
  if (d->vs) {
    // Registros vN según su dcl contra la declaración (o el FVF).
    for (int r = 0; r < 16; r++) {
      if (d->vs->uso[r] == 0xff) continue;
      uint8_t uso = d->vs->uso[r], ind = d->vs->ind[r];
      int k2 = uso == 0 || uso == 9 ? A_POS : uso == 3 ? A_NOR : uso == 10 ? (ind ? A_ESP : A_DIF) : uso == 5 ? A_T0 + ind : uso == 4 ? A_PSZ : -1;
      if (k2 >= 0) ubic[k2] = r;
    }
  } else {
    ubic[A_POS] = 0; ubic[A_NOR] = 1; ubic[A_DIF] = 2; ubic[A_ESP] = 3;
    for (int i = 0; i < 8; i++) ubic[A_T0 + i] = A_T0 + i;
  }
  int subido[16] = {0};
  for (int i = 0; i < A_N; i++) {
    if (!a[i].presente || ubic[i] < 0) continue;
    uint32_t s = a[i].stream, stride, va;
    if (up_v) { if (s) continue; stride = up_stride; va = up_v; }
    else { if (!d->stream[s].b) continue; stride = d->stream[s].stride; va = d->stream[s].b->va + d->stream[s].off; }
    if (!subido[s]) {
      glBindBuffer(GL_ARRAY_BUFFER, vbo[s]);
      glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(v_n * stride), MEM + va + v_ini * stride, GL_STREAM_DRAW);
      subido[s] = 1;
    } else glBindBuffer(GL_ARRAY_BUFFER, vbo[s]);
    GLint comp; GLenum tipo_gl; GLboolean norm;
    if (!tam_tipo(a[i].tipo, &comp, &tipo_gl, &norm)) continue;
    GLuint loc = (GLuint)ubic[i];
    glEnableVertexAttribArray(loc);
    glVertexAttribPointer(loc, comp, tipo_gl, norm, (GLsizei)stride, (const void *)(uintptr_t)a[i].offset);
    if (a[i].tipo == 4) bgra |= (uint16_t)(1u << loc);
  }
  if (!d->vs) {
    // Valores por defecto de los atributos ausentes.
    if (!a[A_DIF].presente) glVertexAttrib4f(2, 1, 1, 1, 1);
    if (!a[A_ESP].presente) glVertexAttrib4f(3, 0, 0, 0, 0);
  }
  // Modo ancho: lo 2D que ocupa todo el ancho se extiende a los costados; el resto de lo 2D, a la zona del juego.
  int extender = 0, centro = 0, espejo = 0;
  ancho_a = ancho_activo() && vp_completo() ? (float)d->backbuffer->w / (float)(d->backbuffer->w + (uint32_t)ancho_extra) : 1;
  ancho_b = 0;
  if (ancho_a != 1 && es_2d(rhw)) {
    float c[4];
    int plano;
    if (caja_pantalla(a, up_v, up_stride, v_ini, v_n, rhw, c, &plano)) {
      int todo_el_ancho = c[0] <= 0.5f && c[2] >= (float)d->backbuffer->w - 0.5f;
      extender = todo_el_ancho && !bordes_transparentes(a, up_v, up_stride, v_ini, v_n);
      espejo = c[3] - c[1] >= 0.9f * (float)d->vp[3]; // fondo de pantalla completa: en espejo; banda: se estira el borde
      centro = !todo_el_ancho;
    }
  }
  armar_clave(&k, a, rhw);
  k.bgra = bgra;
  Programa *p = programa(&k);
  glUseProgram(p->prog);
  uniformes(p, &k);
  estado_gl(rhw && !d->vs, centro);
  texturas(&k);
  if (indexado) {
    size_t n = (size_t)cuenta * ancho_ind;
    void *tmp = malloc(n);
    for (uint32_t i = 0; i < cuenta; i++) {
      if (ancho_ind == 4) ((uint32_t *)tmp)[i] = M32(ind_va + 4 * i) - (v_ini - base);
      else ((uint16_t *)tmp)[i] = (uint16_t)(M16(ind_va + 2 * i) - (v_ini - base));
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)n, tmp, GL_STREAM_DRAW);
    free(tmp);
  }
  // Pasadas: la del juego y, si hay que extender, las de los costados. Cada costado es x_real = a * x_juego + b
  // (en píxeles, antes del medio píxel de D3D9), recortado a [x0, x1): el píxel j muestra x_juego = (j - b) / a.
  // A la derecha se arranca un texel adentro: la última columna de las texturas no se ve en el original (el juego
  // muestra hasta la 798) y a veces trae otra cosa (en el título, una raya gris).
  struct { float a, b; int32_t x0, x1; } pas[5];
  int n_pas = 1;
  if (extender) {
    float g = (float)d->backbuffer->w;
    int32_t m = ancho_extra / 2, mr = ancho_extra - m, sr = (int32_t)d->backbuffer->w + m, wr = sr + mr;
    if (espejo) {
      // Fondos: el borde de la imagen reflejado (franja interior) y vuelto a reflejar (exterior). Así no llega a
      // los costados lo del centro de la imagen (logos, textos), que con un espejo entero quedaba al revés.
      int32_t s = m / 2, s2 = mr / 2;
      pas[n_pas++] = (__typeof__(pas[0])){-1, (float)m - 0.5f, m - s, m};
      pas[n_pas++] = (__typeof__(pas[0])){1, (float)(m - 2 * s) - 0.5f, 0, m - s};
      pas[n_pas++] = (__typeof__(pas[0])){-1, (float)sr + g - 1.5f, sr, sr + s2};
      pas[n_pas++] = (__typeof__(pas[0])){1, (float)sr - g + (float)(2 * s2) + 0.5f, sr + s2, wr};
    } else {
      // Bandas (HUD, títulos): la columna del borde estirada hasta afuera.
      pas[n_pas++] = (__typeof__(pas[0])){(float)(4 * m), (float)(-2 * m), 0, m};
      pas[n_pas++] = (__typeof__(pas[0])){(float)(4 * mr), (float)sr - (g - 1.75f) * (float)(4 * mr), sr, wr};
    }
  }
  for (int i = 0; i < n_pas; i++) {
    if (i) {
      float W = (float)(d->backbuffer->w + (uint32_t)ancho_extra), g = (float)d->backbuffer->w;
      ancho_a = pas[i].a * g / W;
      ancho_b = (pas[i].a * g + 2 * pas[i].b) / W - 1;
      subir_pix(p);
      glEnable(GL_SCISSOR_TEST);
      glScissor(pas[i].x0, 0, pas[i].x1 - pas[i].x0, (GLsizei)d->backbuffer->h);
      glDisable(GL_CULL_FACE); // el espejo da vuelta las caras
    }
    if (indexado) glDrawElements(modo, (GLsizei)cuenta, ancho_ind == 4 ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT, NULL);
    else glDrawArrays(modo, 0, (GLsizei)cuenta);
  }
  ancho_a = 1; ancho_b = 0;
}

uint32_t vs_nuevo_id(void) { return ++vs_ids; }

// ---------------- Clear y Present ----------------
void limpiar(uint32_t n, uint32_t rects, uint32_t flags, uint32_t color, float z, uint32_t stencil) {
  Dispositivo *d = disp;
  aplicar_destino();
  GLbitfield b = 0;
  if (flags & 1) { float c4[4]; color4(color, c4); glClearColor(c4[0], c4[1], c4[2], c4[3]); glColorMask(1, 1, 1, 1); b |= GL_COLOR_BUFFER_BIT; }
  if ((flags & 2) && d->ds) { glClearDepthf(z); glDepthMask(1); b |= GL_DEPTH_BUFFER_BIT; }
  if ((flags & 4) && d->ds) { glClearStencil((GLint)stencil); glStencilMaskSeparate(GL_FRONT, 0xff); glStencilMaskSeparate(GL_BACK, 0xff); b |= GL_STENCIL_BUFFER_BIT; }
  if (!b) return;
  glEnable(GL_SCISSOR_TEST);
  // D3D9 limpia solo dentro del viewport (y de la tijera si está activa).
  int32_t vx = (int32_t)d->vp[0], vy = (int32_t)d->vp[1], vw = (int32_t)d->vp[2], vh = (int32_t)d->vp[3];
  for (uint32_t i = 0; i < (n ? n : 1); i++) {
    int32_t x0 = vx, y0 = vy, x1 = vx + vw, y1 = vy + vh;
    if (n && rects) {
      uint32_t r = rects + 16 * i;
      x0 = SDL_max(x0, (int32_t)M32(r)); y0 = SDL_max(y0, (int32_t)M32(r + 4));
      x1 = SDL_min(x1, (int32_t)M32(r + 8)); y1 = SDL_min(y1, (int32_t)M32(r + 12));
    }
    if (d->rs[174]) {
      x0 = SDL_max(x0, (int32_t)d->tijera[0]); y0 = SDL_max(y0, (int32_t)d->tijera[1]);
      x1 = SDL_min(x1, (int32_t)(d->tijera[0] + d->tijera[2])); y1 = SDL_min(y1, (int32_t)(d->tijera[1] + d->tijera[3]));
    }
    if (x1 <= x0 || y1 <= y0) continue;
    a_real(&x0, &x1);
    glScissor(x0, y0, x1 - x0, y1 - y0);
    glClear(b);
  }
}

// Capturas para comparar con el original: HINO_CAPTURAS="120,300" guarda esos cuadros en la carpeta de escritura.
void guardar_bmp(const char *ruta, const uint8_t *rgba, uint32_t w, uint32_t h);
uint32_t cuadros_presentados(void) { return cuadro; }
const char *carpeta_capturas(void);

void guion_teclas(uint32_t cuadro); // dinput8.c
// Dónde quedó el backbuffer en la ventana (x, y desde arriba, ancho, alto), su tamaño real y cuánto corre el modo
// ancho al juego: x del juego = (x - [0]) * [4] / [2] - [6].
int imagen_juego[7];
void presentar(void) {
  Dispositivo *d = disp;
  Superficie *bb = d->backbuffer;
  cuadro++;
  static uint64_t t_bloque;
  uint64_t ahora = SDL_GetTicks();
  if (!t_bloque) t_bloque = ahora;
  if (cuadro % 300 == 0) {
    fprintf(stderr, "cuadro %u (%.1f fps)\n", cuadro, 300000.0 / (double)(ahora - t_bloque + 1));
    t_bloque = ahora;
  }
  guion_teclas(cuadro);
  uint32_t bw = bb->w_gl; // el backbuffer real (más ancho que el del juego en modo ancho)
  if (en_lista(getenv("HINO_CAPTURAS"), cuadro)) {
    uint8_t *px = malloc((size_t)bw * bb->h * 4);
    glBindFramebuffer(GL_FRAMEBUFFER, bb->fbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, (GLsizei)bw, (GLsizei)bb->h, GL_RGBA, GL_UNSIGNED_BYTE, px);
    char ruta[600];
    snprintf(ruta, sizeof ruta, "%s/cuadro_%05u.bmp", carpeta_capturas(), cuadro);
    guardar_bmp(ruta, px, bw, bb->h);
    free(px);
    fprintf(stderr, "captura: %s\n", ruta);
  }
  int ww, wh;
  SDL_GetWindowSizeInPixels(d->ventana, &ww, &wh);
  glDisable(GL_SCISSOR_TEST);
  glColorMask(1, 1, 1, 1);
  // Lo más grande que entre sin deformar; si la proporción difiere menos de 1 % (el ancho del modo ancho se redondea
  // a par), se llena la pantalla entera. Con escala no entera se agranda primero al entero de arriba sin filtrar y
  // de ahí se achica con filtro lineal ("sharp bilinear"): nítido y sin píxeles de distinto ancho.
  float e = SDL_min((float)ww / (float)bw, (float)wh / (float)bb->h);
  int dw = (int)((float)bw * e), dh = (int)((float)bb->h * e), dx = (ww - dw) / 2, dy = (wh - dh) / 2;
  if (dw > ww * 99 / 100 && dh > wh * 99 / 100) { dx = dy = 0; dw = ww; dh = wh; }
  GLuint origen = bb->fbo;
  int ow = (int)bw, oh = (int)bb->h, k = (int)SDL_ceilf(e);
  if (e > 1 && e != SDL_floorf(e)) {
    static GLuint fbo_k, tex_k;
    static int kw, kh;
    if (kw != ow * k || kh != oh * k) {
      if (!fbo_k) { glGenFramebuffers(1, &fbo_k); glGenTextures(1, &tex_k); }
      kw = ow * k; kh = oh * k;
      glBindTexture(GL_TEXTURE_2D, tex_k);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kw, kh, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      glBindFramebuffer(GL_FRAMEBUFFER, fbo_k);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex_k, 0);
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, bb->fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo_k);
    glBlitFramebuffer(0, 0, ow, oh, 0, 0, kw, kh, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    origen = fbo_k; ow = kw; oh = kh;
  }
  glBindFramebuffer(GL_READ_FRAMEBUFFER, origen);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  glBlitFramebuffer(0, 0, ow, oh, dx, dy + dh, dx + dw, dy, GL_COLOR_BUFFER_BIT, e == SDL_floorf(e) && dw == (int)((float)bw * e) ? GL_NEAREST : GL_LINEAR);
  int arriba = wh - dy - dh; // GL cuenta desde abajo; el resto (toques, mouse) desde arriba
  imagen_juego[0] = dx; imagen_juego[1] = arriba; imagen_juego[2] = dw; imagen_juego[3] = dh;
  imagen_juego[4] = (int)bw; imagen_juego[5] = (int)bb->h; imagen_juego[6] = (int)(bw - bb->w) / 2;
  tactil_dibujar(d->ventana, ww, wh);
  invitado_soltar(); // mientras se espera el vsync corren los otros hilos del juego (la música)
  SDL_GL_SwapWindow(d->ventana);
  invitado_tomar();
  ancho_ajustar(ww, wh); // después del cuadro: el próximo ya se dibuja con el ancho nuevo
  fbo_actual = (GLuint)-1;
  aplicar_destino();
}
