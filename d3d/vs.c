// Traduce el ensamblador de vertex shaders vs_1_x de D3D9 (el texto que el juego arma con D3DXAssembleShader)
// a un cuerpo de GLSL. Entradas vN, temporales rN, constantes c[N] (o def), salidas oPos/oD0/oD1/oTn/oFog.
#include "d3d9_int.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>

uint32_t vs_nuevo_id(void);

typedef struct { char *s; size_t n, cap; } Txt;
static void sa(Txt *t, const char *fmt, ...) {
  va_list va;
  for (;;) {
    va_start(va, fmt);
    int w = vsnprintf(t->s ? t->s + t->n : NULL, t->s ? t->cap - t->n : 0, fmt, va);
    va_end(va);
    if (t->s && w >= 0 && (size_t)w < t->cap - t->n) { t->n += (size_t)w; return; }
    t->cap = t->cap ? t->cap * 2 : 4096;
    t->s = realloc(t->s, t->cap);
    if (t->n == 0) t->s[0] = 0;
  }
}

static int definida[256];
static float defs[256][4];

// Swizzle de D3D (xyzw o rgba, se repite el último) normalizado a 4 componentes.
static void swizzle(const char *s, char out[5]) {
  static const char *xyzw = "xyzw", *rgba = "rgba";
  int n = 0;
  for (; s[n] && n < 4; n++) {
    const char *p = strchr(xyzw, s[n]);
    out[n] = p ? *p : strchr(rgba, s[n]) ? xyzw[strchr(rgba, s[n]) - rgba] : 'x';
  }
  for (int i = n; i < 4; i++) out[i] = n ? out[n - 1] : xyzw[i];
  out[4] = 0;
}

static void mascara(const char *s, char out[5]) {
  static const char *xyzw = "xyzw", *rgba = "rgba";
  int n = 0;
  for (int i = 0; s[i] && n < 4; i++) {
    const char *p = strchr(xyzw, s[i]);
    if (!p && strchr(rgba, s[i])) p = xyzw + (strchr(rgba, s[i]) - rgba);
    if (p) out[n++] = *p;
  }
  if (!n) strcpy(out, "xyzw"); else out[n] = 0;
}

// Registro base (sin swizzle). Devuelve 0 si no se reconoce.
static int registro(const char *r, char *out, size_t tam, int *num_c) {
  *num_c = -1;
  if (r[0] == 'c' && r[1] == '[') {
    const char *in = r + 2;
    char tmp[64];
    size_t k = 0;
    while (*in && *in != ']' && k < sizeof tmp - 1) tmp[k++] = *in++;
    tmp[k] = 0;
    if (strstr(tmp, "a0")) {
      const char *mas = strchr(tmp, '+');
      snprintf(out, tam, "c[a0 + %d]", mas ? atoi(mas + 1) : 0);
      return 1;
    }
    *num_c = atoi(tmp);
  } else if (r[0] == 'c' && isdigit((unsigned char)r[1])) *num_c = atoi(r + 1);
  if (*num_c >= 0) {
    if (*num_c < 256 && definida[*num_c]) snprintf(out, tam, "d%d", *num_c);
    else snprintf(out, tam, "c[%d]", *num_c);
    return 1;
  }
  if ((r[0] == 'r' || r[0] == 'v') && isdigit((unsigned char)r[1])) { snprintf(out, tam, "%c%d", r[0], atoi(r + 1)); return 1; }
  if (!SDL_strncasecmp(r, "oPos", 4)) { snprintf(out, tam, "oPos"); return 1; }
  if (!SDL_strncasecmp(r, "oD", 2)) { snprintf(out, tam, "oD%d", atoi(r + 2)); return 1; }
  if (!SDL_strncasecmp(r, "oT", 2)) { snprintf(out, tam, "oT%d", atoi(r + 2)); return 1; }
  if (!SDL_strncasecmp(r, "oFog", 4)) { snprintf(out, tam, "oFog"); return 1; }
  if (!SDL_strncasecmp(r, "oPts", 4)) { snprintf(out, tam, "oPts"); return 1; }
  if (!strncmp(r, "a0", 2)) { snprintf(out, tam, "a0"); return 1; }
  return 0;
}

// Operando fuente: [-]reg[.swz] -> expresión vec4.
static int fuente(const char *op, char *out, size_t tam) {
  int neg = 0;
  while (*op == ' ') op++;
  if (*op == '-') { neg = 1; op++; }
  char reg[64], swz[5];
  size_t k = 0;
  const char *p = op;
  int corchete = 0;
  while (*p && (corchete || *p != '.') && k < sizeof reg - 1) { if (*p == '[') corchete = 1; if (*p == ']') corchete = 0; reg[k++] = *p++; }
  reg[k] = 0;
  while (k && reg[k - 1] == ' ') reg[--k] = 0;
  swizzle(*p == '.' ? p + 1 : "", swz);
  char base[64];
  int nc;
  if (!registro(reg, base, sizeof base, &nc)) return 0;
  snprintf(out, tam, "%s%s.%s", neg ? "-" : "", base, swz);
  return 1;
}

static int destino(const char *op, char *base, size_t tam, char masc[5]) {
  char reg[64];
  size_t k = 0;
  const char *p = op;
  while (*p == ' ') p++;
  while (*p && *p != '.' && *p != ' ' && k < sizeof reg - 1) reg[k++] = *p++;
  reg[k] = 0;
  mascara(*p == '.' ? p + 1 : "", masc);
  int nc;
  return registro(reg, base, tam, &nc);
}

static const struct { const char *n; uint8_t uso; } USOS[] = {
  {"positiont", 9}, {"position", 0}, {"blendweight", 1}, {"blendindices", 2}, {"normal", 3}, {"psize", 4}, {"texcoord", 5},
  {"tangent", 6}, {"binormal", 7}, {"tessfactor", 8}, {"color", 10}, {"fog", 11}, {"depth", 12}, {"sample", 13},
};

ShaderV *vs_traducir(const char *fuente_txt) {
  ShaderV *s = calloc(1, sizeof *s);
  s->r.tipo = R_VS; s->r.refs = 1;
  memset(s->uso, 0xff, sizeof s->uso);
  memset(definida, 0, sizeof definida);
  Txt cuerpo = {0}, pre = {0};
  sa(&cuerpo, "");
  sa(&pre, "  vec4 r0 = vec4(0.0), r1 = r0, r2 = r0, r3 = r0, r4 = r0, r5 = r0, r6 = r0, r7 = r0, r8 = r0, r9 = r0, r10 = r0, r11 = r0;\n"
           "  vec4 oPos = vec4(0.0), oD0 = vec4(1.0), oD1 = vec4(0.0), oFog = vec4(1.0), oPts = vec4(1.0);\n"
           "  vec4 oT0 = vec4(0.0, 0.0, 0.0, 1.0), oT1 = oT0, oT2 = oT0, oT3 = oT0, oT4 = oT0, oT5 = oT0, oT6 = oT0, oT7 = oT0;\n"
           "  int a0 = 0;\n");
  char *copia = SDL_strdup(fuente_txt), *estado = NULL;
  for (char *linea = SDL_strtok_r(copia, "\n", &estado); linea; linea = SDL_strtok_r(NULL, "\n", &estado)) {
    char *c2 = strstr(linea, "//");
    if (c2) *c2 = 0;
    if ((c2 = strchr(linea, ';'))) *c2 = 0;
    while (*linea == ' ' || *linea == '\t') linea++;
    if (!*linea) continue;
    char op[32];
    size_t k = 0;
    while (*linea && !isspace((unsigned char)*linea) && k < sizeof op - 1) op[k++] = (char)tolower((unsigned char)*linea++);
    op[k] = 0;
    if (!strncmp(op, "vs_", 3)) continue;
    // Operandos separados por coma (las comas dentro de corchetes no aparecen en vs_1_x).
    char *ops[5] = {0};
    int n = 0;
    char *estado2 = NULL;
    for (char *o = SDL_strtok_r(linea, ",", &estado2); o && n < 5; o = SDL_strtok_r(NULL, ",", &estado2)) {
      while (*o == ' ' || *o == '\t') o++;
      size_t l = strlen(o);
      while (l && (o[l - 1] == ' ' || o[l - 1] == '\t' || o[l - 1] == '\r')) o[--l] = 0;
      ops[n++] = o;
    }
    if (!strncmp(op, "dcl_", 4)) {
      const char *u = op + 4;
      for (size_t i = 0; i < sizeof USOS / sizeof USOS[0]; i++) {
        size_t l = strlen(USOS[i].n);
        if (!strncmp(u, USOS[i].n, l)) {
          int reg = ops[0] && ops[0][0] == 'v' ? atoi(ops[0] + 1) : -1;
          if (reg >= 0 && reg < 16) { s->uso[reg] = USOS[i].uso; s->ind[reg] = (uint8_t)atoi(u + l); }
          break;
        }
      }
      continue;
    }
    if (!strcmp(op, "def")) {
      int r = ops[0] ? atoi(ops[0] + 1) : -1;
      if (r >= 0 && r < 256 && n == 5) {
        definida[r] = 1;
        for (int i = 0; i < 4; i++) defs[r][i] = (float)atof(ops[1 + i]);
        sa(&pre, "  const vec4 d%d = vec4(%.9g, %.9g, %.9g, %.9g);\n", r, defs[r][0], defs[r][1], defs[r][2], defs[r][3]);
      }
      continue;
    }
    if (!strcmp(op, "nop")) continue;
    char dst[64], m[5], a[96] = "", b[96] = "", cc[96] = "", e[512];
    if (n < 2 || !destino(ops[0], dst, sizeof dst, m) || !fuente(ops[1], a, sizeof a) ||
        (n > 2 && !fuente(ops[2], b, sizeof b)) || (n > 3 && !fuente(ops[3], cc, sizeof cc))) {
      fprintf(stderr, "vs: no entiendo '%s'\n", op);
      free(copia); free(cuerpo.s); free(pre.s); free(s);
      return NULL;
    }
    if (!strcmp(dst, "a0")) { sa(&cuerpo, "  a0 = int(floor(%s.x));\n", a); continue; }
    int nc = -1;
    if (!strcmp(dst, "oPos")) { // para ubicar cada dibujo en la pantalla desde la CPU (modo ancho)
      int de_v0 = !strcmp(ops[1], "v0") && s->uso[0] == 0;
      if (de_v0 && !strncmp(op, "m4x4", 4) && ops[2] && ops[2][0] == 'c' && isdigit((unsigned char)ops[2][1]) && !s->pos_tipo) {
        s->pos_tipo = 1; s->pos_c = atoi(ops[2] + 1);
      } else if (de_v0 && !strcmp(op, "mov") && !s->pos_tipo) s->pos_tipo = 2;
      else s->pos_tipo = -1;
    }
    if (!strncmp(op, "m4x4", 4) || !strncmp(op, "m4x3", 4) || !strncmp(op, "m3x4", 4) || !strncmp(op, "m3x3", 4) || !strncmp(op, "m3x2", 4)) {
      // Fuente 2 es la primera fila de la matriz: hace falta el número de registro.
      const char *p = ops[2];
      while (*p == ' ' || *p == '-') p++;
      char base[64];
      if (!registro(p, base, sizeof base, &nc) || nc < 0) { fprintf(stderr, "vs: matriz no constante\n"); free(copia); free(s); return NULL; }
      int filas = op[3] - '0', cols = op[1] - '0';
      char comp[4][96];
      for (int i = 0; i < 4; i++) {
        if (i >= filas) { snprintf(comp[i], sizeof comp[i], "0.0"); continue; }
        int r2 = nc + i;
        char fila[32];
        if (r2 < 256 && definida[r2]) snprintf(fila, sizeof fila, "d%d", r2); else snprintf(fila, sizeof fila, "c[%d]", r2);
        if (cols == 4) snprintf(comp[i], sizeof comp[i], "dot(%s, %s)", a, fila);
        else snprintf(comp[i], sizeof comp[i], "dot((%s).xyz, %s.xyz)", a, fila);
      }
      snprintf(e, sizeof e, "vec4(%s, %s, %s, %s)", comp[0], comp[1], comp[2], comp[3]);
    } else if (!strcmp(op, "mov")) snprintf(e, sizeof e, "%s", a);
    else if (!strcmp(op, "add")) snprintf(e, sizeof e, "%s + %s", a, b);
    else if (!strcmp(op, "sub")) snprintf(e, sizeof e, "%s - %s", a, b);
    else if (!strcmp(op, "mul")) snprintf(e, sizeof e, "%s * %s", a, b);
    else if (!strcmp(op, "mad")) snprintf(e, sizeof e, "%s * %s + %s", a, b, cc);
    else if (!strcmp(op, "dp3")) snprintf(e, sizeof e, "vec4(dot((%s).xyz, (%s).xyz))", a, b);
    else if (!strcmp(op, "dp4")) snprintf(e, sizeof e, "vec4(dot(%s, %s))", a, b);
    else if (!strcmp(op, "min")) snprintf(e, sizeof e, "min(%s, %s)", a, b);
    else if (!strcmp(op, "max")) snprintf(e, sizeof e, "max(%s, %s)", a, b);
    else if (!strcmp(op, "slt")) snprintf(e, sizeof e, "vec4(lessThan(%s, %s))", a, b);
    else if (!strcmp(op, "sge")) snprintf(e, sizeof e, "vec4(greaterThanEqual(%s, %s))", a, b);
    else if (!strcmp(op, "rcp")) snprintf(e, sizeof e, "vec4((%s).x == 1.0 ? 1.0 : 1.0 / (%s).x)", a, a);
    else if (!strcmp(op, "rsq")) snprintf(e, sizeof e, "vec4(inversesqrt(abs((%s).x)))", a);
    else if (!strcmp(op, "exp")) snprintf(e, sizeof e, "vec4(exp2((%s).x))", a);
    else if (!strcmp(op, "log")) snprintf(e, sizeof e, "vec4(log2(abs((%s).x)))", a);
    else if (!strcmp(op, "expp")) snprintf(e, sizeof e, "vec4(exp2(floor((%s).x)), fract((%s).x), exp2((%s).x), 1.0)", a, a, a);
    else if (!strcmp(op, "logp")) snprintf(e, sizeof e, "vec4(log2(abs((%s).x)))", a);
    else if (!strcmp(op, "frc")) snprintf(e, sizeof e, "fract(%s)", a);
    else if (!strcmp(op, "abs")) snprintf(e, sizeof e, "abs(%s)", a);
    else if (!strcmp(op, "lit"))
      snprintf(e, sizeof e, "vec4(1.0, max((%s).x, 0.0), (%s).x > 0.0 ? pow(max((%s).y, 0.0), clamp((%s).w, -128.0, 128.0)) : 0.0, 1.0)", a, a, a, a);
    else if (!strcmp(op, "dst")) snprintf(e, sizeof e, "vec4(1.0, (%s).y * (%s).y, (%s).z, (%s).w)", a, b, a, b);
    else if (!strcmp(op, "nrm")) snprintf(e, sizeof e, "vec4(normalize((%s).xyz), 0.0)", a);
    else if (!strcmp(op, "pow")) snprintf(e, sizeof e, "vec4(pow(abs((%s).x), (%s).x))", a, b);
    else if (!strcmp(op, "lrp")) snprintf(e, sizeof e, "mix(%s, %s, %s)", cc, b, a);
    else {
      fprintf(stderr, "vs: instrucción no soportada '%s'\n", op);
      free(copia); free(cuerpo.s); free(pre.s); free(s);
      return NULL;
    }
    sa(&cuerpo, "  %s.%s = (%s).%s;\n", dst, m, e, m);
  }
  free(copia);
  sa(&pre, "%s", cuerpo.s);
  sa(&pre, "  gl_Position = a_gl(oPos);\n  v_dif = clamp(oD0, 0.0, 1.0);\n  v_esp = clamp(oD1, 0.0, 1.0);\n"
           "  v_t[0] = oT0; v_t[1] = oT1; v_t[2] = oT2; v_t[3] = oT3; v_t[4] = oT4; v_t[5] = oT5; v_t[6] = oT6; v_t[7] = oT7;\n"
           "  v_niebla = oFog.x;\n");
  free(cuerpo.s);
  s->glsl = pre.s;
  s->id = vs_nuevo_id();
  if (getenv("HINO_LOG_VS")) fprintf(stderr, "=== VS %u ===\n%s\n", s->id, fuente_txt);
  return s;
}
