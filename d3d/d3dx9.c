// D3DX9_37: las 18 funciones que importa el juego. La matemática va en float con el mismo orden de
// operaciones que la implementación de referencia; las texturas salen del BMP de 32 bits que arma el juego.
#include "d3d9_int.h"
#include <math.h>
#include <stdlib.h>

#define F(va) MF32(va)
typedef struct { float x, y, z; } V3;
static V3 v3(uint32_t p) { return (V3){F(p), F(p + 4), F(p + 8)}; }
static void pon3(uint32_t p, V3 v) { F(p) = v.x; F(p + 4) = v.y; F(p + 8) = v.z; }
static V3 cruz(V3 a, V3 b) { return (V3){a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
static float punto(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static V3 normal(V3 v) {
  float n = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
  return n ? (V3){v.x / n, v.y / n, v.z / n} : (V3){0, 0, 0};
}
static float argf(Ctx *c, int n) { return MF32(c->esp + 4 + 4 * n); }
#define MM(p, f, k) F((p) + 4u * (uint32_t)((f) * 4 + (k)))

API(D3DXVec3Normalize) { pon3(ARG(0), normal(v3(ARG(1)))); VOLVER(2, ARG(0)); }

API(D3DXMatrixMultiply) {
  float a[16], b[16], r[16];
  memcpy(a, MEM + ARG(1), 64); memcpy(b, MEM + ARG(2), 64);
  for (int f = 0; f < 4; f++) for (int k = 0; k < 4; k++)
    r[f * 4 + k] = a[f * 4] * b[k] + a[f * 4 + 1] * b[4 + k] + a[f * 4 + 2] * b[8 + k] + a[f * 4 + 3] * b[12 + k];
  memcpy(MEM + ARG(0), r, 64);
  VOLVER(3, ARG(0));
}

API(D3DXMatrixScaling) {
  uint32_t o = ARG(0);
  memset(MEM + o, 0, 64);
  MM(o, 0, 0) = argf(c, 1); MM(o, 1, 1) = argf(c, 2); MM(o, 2, 2) = argf(c, 3); MM(o, 3, 3) = 1;
  VOLVER(4, o);
}

API(D3DXMatrixTranspose) {
  float m[16], r[16];
  memcpy(m, MEM + ARG(1), 64);
  for (int f = 0; f < 4; f++) for (int k = 0; k < 4; k++) r[f * 4 + k] = m[k * 4 + f];
  memcpy(MEM + ARG(0), r, 64);
  VOLVER(2, ARG(0));
}

API(D3DXVec4Transform) {
  float v[4], m[16], r[4];
  memcpy(v, MEM + ARG(1), 16); memcpy(m, MEM + ARG(2), 64);
  for (int k = 0; k < 4; k++) r[k] = m[k] * v[0] + m[4 + k] * v[1] + m[8 + k] * v[2] + m[12 + k] * v[3];
  memcpy(MEM + ARG(0), r, 16);
  VOLVER(3, ARG(0));
}

API(D3DXVec3Transform) {
  float v[3], m[16], r[4];
  memcpy(v, MEM + ARG(1), 12); memcpy(m, MEM + ARG(2), 64);
  for (int k = 0; k < 4; k++) r[k] = m[k] * v[0] + m[4 + k] * v[1] + m[8 + k] * v[2] + m[12 + k];
  memcpy(MEM + ARG(0), r, 16);
  VOLVER(3, ARG(0));
}

API(D3DXMatrixInverse) {
  float m[16], inv[16];
  memcpy(m, MEM + ARG(2), 64);
  inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
  inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
  inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
  inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
  inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
  inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
  inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
  inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
  inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
  inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
  inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
  inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
  inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
  inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
  inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
  inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
  float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
  if (ARG(1)) MF32(ARG(1)) = det;
  if (det == 0) VOLVER(3, 0);
  float id = 1.0f / det;
  for (int i = 0; i < 16; i++) inv[i] *= id;
  memcpy(MEM + ARG(0), inv, 64);
  VOLVER(3, ARG(0));
}

API(D3DXMatrixLookAtLH) {
  uint32_t o = ARG(0);
  V3 ojo = v3(ARG(1)), a = v3(ARG(2)), arriba = v3(ARG(3));
  V3 z = normal((V3){a.x - ojo.x, a.y - ojo.y, a.z - ojo.z});
  V3 der = cruz(arriba, z), up = cruz(z, der);
  V3 x = normal(der), y = normal(up);
  MM(o, 0, 0) = x.x; MM(o, 1, 0) = x.y; MM(o, 2, 0) = x.z; MM(o, 3, 0) = -punto(x, ojo);
  MM(o, 0, 1) = y.x; MM(o, 1, 1) = y.y; MM(o, 2, 1) = y.z; MM(o, 3, 1) = -punto(y, ojo);
  MM(o, 0, 2) = z.x; MM(o, 1, 2) = z.y; MM(o, 2, 2) = z.z; MM(o, 3, 2) = -punto(z, ojo);
  MM(o, 0, 3) = 0; MM(o, 1, 3) = 0; MM(o, 2, 3) = 0; MM(o, 3, 3) = 1;
  VOLVER(4, o);
}

API(D3DXMatrixPerspectiveFovLH) {
  uint32_t o = ARG(0);
  float fov = argf(c, 1), asp = argf(c, 2), zn = argf(c, 3), zf = argf(c, 4);
  memset(MEM + o, 0, 64);
  MM(o, 0, 0) = 1.0f / (asp * tanf(fov / 2.0f));
  MM(o, 1, 1) = 1.0f / tanf(fov / 2.0f);
  MM(o, 2, 2) = zf / (zf - zn);
  MM(o, 2, 3) = 1.0f;
  MM(o, 3, 2) = (zf * zn) / (zn - zf);
  VOLVER(5, o);
}

API(D3DXMatrixShadow) {
  uint32_t o = ARG(0);
  float l[4], p[4];
  memcpy(l, MEM + ARG(1), 16); memcpy(p, MEM + ARG(2), 16);
  float n = sqrtf(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
  if (n) for (int i = 0; i < 4; i++) p[i] /= n; else memset(p, 0, 16);
  float dot = p[0] * l[0] + p[1] * l[1] + p[2] * l[2] + p[3] * l[3];
  for (int f = 0; f < 4; f++) for (int k = 0; k < 4; k++) MM(o, f, k) = (f == k ? dot : 0.0f) - p[f] * l[k];
  VOLVER(3, o);
}

static void quat_de_matriz(uint32_t o, const float m[16]) {
#define E(f, k) m[(f) * 4 + (k)]
  float tr = E(0, 0) + E(1, 1) + E(2, 2) + 1.0f, s, q[4];
  if (tr > 1.0f) {
    s = 2.0f * sqrtf(tr);
    q[0] = (E(1, 2) - E(2, 1)) / s; q[1] = (E(2, 0) - E(0, 2)) / s; q[2] = (E(0, 1) - E(1, 0)) / s; q[3] = 0.25f * s;
  } else {
    int mi = 0;
    float md = E(0, 0);
    for (int i = 1; i < 3; i++) if (E(i, i) > md) { mi = i; md = E(i, i); }
    if (mi == 0) {
      s = 2.0f * sqrtf(1.0f + E(0, 0) - E(1, 1) - E(2, 2));
      q[0] = 0.25f * s; q[1] = (E(0, 1) + E(1, 0)) / s; q[2] = (E(0, 2) + E(2, 0)) / s; q[3] = (E(1, 2) - E(2, 1)) / s;
    } else if (mi == 1) {
      s = 2.0f * sqrtf(1.0f + E(1, 1) - E(0, 0) - E(2, 2));
      q[0] = (E(0, 1) + E(1, 0)) / s; q[1] = 0.25f * s; q[2] = (E(1, 2) + E(2, 1)) / s; q[3] = (E(2, 0) - E(0, 2)) / s;
    } else {
      s = 2.0f * sqrtf(1.0f + E(2, 2) - E(0, 0) - E(1, 1));
      q[0] = (E(0, 2) + E(2, 0)) / s; q[1] = (E(1, 2) + E(2, 1)) / s; q[2] = 0.25f * s; q[3] = (E(0, 1) - E(1, 0)) / s;
    }
  }
#undef E
  memcpy(MEM + o, q, 16);
}

API(D3DXMatrixDecompose) {
  uint32_t esc = ARG(0), rot = ARG(1), tra = ARG(2);
  float m[16], n[16];
  memcpy(m, MEM + ARG(3), 64);
  float s[3];
  for (int f = 0; f < 3; f++) s[f] = sqrtf(m[f * 4] * m[f * 4] + m[f * 4 + 1] * m[f * 4 + 1] + m[f * 4 + 2] * m[f * 4 + 2]);
  F(esc) = s[0]; F(esc + 4) = s[1]; F(esc + 8) = s[2];
  F(tra) = m[12]; F(tra + 4) = m[13]; F(tra + 8) = m[14];
  if (s[0] == 0 || s[1] == 0 || s[2] == 0) VOLVER(4, D3DERR_INVALIDCALL);
  memcpy(n, m, 64);
  for (int f = 0; f < 3; f++) for (int k = 0; k < 3; k++) n[f * 4 + k] = m[f * 4 + k] / s[f];
  quat_de_matriz(rot, n);
  VOLVER(4, 0);
}

API(D3DXMatrixRotationQuaternion) {
  uint32_t o = ARG(0);
  float x = F(ARG(1)), y = F(ARG(1) + 4), z = F(ARG(1) + 8), w = F(ARG(1) + 12);
  memset(MEM + o, 0, 64);
  MM(o, 0, 0) = 1.0f - 2.0f * (y * y + z * z);
  MM(o, 0, 1) = 2.0f * (x * y + z * w);
  MM(o, 0, 2) = 2.0f * (x * z - y * w);
  MM(o, 1, 0) = 2.0f * (x * y - z * w);
  MM(o, 1, 1) = 1.0f - 2.0f * (x * x + z * z);
  MM(o, 1, 2) = 2.0f * (y * z + x * w);
  MM(o, 2, 0) = 2.0f * (x * z + y * w);
  MM(o, 2, 1) = 2.0f * (y * z - x * w);
  MM(o, 2, 2) = 1.0f - 2.0f * (x * x + y * y);
  MM(o, 3, 3) = 1.0f;
  VOLVER(2, o);
}

API(D3DXQuaternionRotationAxis) {
  uint32_t o = ARG(0);
  V3 v = normal(v3(ARG(1)));
  float a = argf(c, 2);
  F(o) = sinf(a / 2.0f) * v.x; F(o + 4) = sinf(a / 2.0f) * v.y; F(o + 8) = sinf(a / 2.0f) * v.z; F(o + 12) = cosf(a / 2.0f);
  VOLVER(3, o);
}

API(D3DXQuaternionMultiply) {
  float a[4], b[4], r[4];
  memcpy(a, MEM + ARG(1), 16); memcpy(b, MEM + ARG(2), 16);
  r[0] = b[3] * a[0] + b[0] * a[3] + b[1] * a[2] - b[2] * a[1];
  r[1] = b[3] * a[1] - b[0] * a[2] + b[1] * a[3] + b[2] * a[0];
  r[2] = b[3] * a[2] + b[0] * a[1] - b[1] * a[0] + b[2] * a[3];
  r[3] = b[3] * a[3] - b[0] * a[0] - b[1] * a[1] - b[2] * a[2];
  memcpy(MEM + ARG(0), r, 16);
  VOLVER(3, ARG(0));
}

API(D3DXQuaternionSlerp) {
  float a[4], b[4], r[4], t = argf(c, 3);
  memcpy(a, MEM + ARG(1), 16); memcpy(b, MEM + ARG(2), 16);
  float tmp = 1.0f - t, dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
  if (dot < 0.0f) { t = -t; dot = -dot; }
  if (1.0f - dot > 0.001f) {
    float th = acosf(dot);
    tmp = sinf(th * tmp) / sinf(th);
    t = sinf(th * t) / sinf(th);
  }
  for (int i = 0; i < 4; i++) r[i] = tmp * a[i] + t * b[i];
  memcpy(MEM + ARG(0), r, 16);
  VOLVER(4, ARG(0));
}

API(D3DXQuaternionRotationYawPitchRoll) {
  uint32_t o = ARG(0);
  float y = argf(c, 1), p = argf(c, 2), r = argf(c, 3);
  float sy = sinf(y / 2.0f), cy = cosf(y / 2.0f), sp = sinf(p / 2.0f), cp = cosf(p / 2.0f), sr = sinf(r / 2.0f), cr = cosf(r / 2.0f);
  F(o) = sy * cp * sr + cy * sp * cr;
  F(o + 4) = sy * cp * cr - cy * sp * sr;
  F(o + 8) = cy * cp * sr - sy * sp * cr;
  F(o + 12) = cy * cp * cr + sy * sp * sr;
  VOLVER(4, o);
}

// ---------------- ID3DXBuffer ----------------
typedef struct { Recurso r; uint32_t va, tam; } BufferX;
API(B_QI) { M32(ARG(2)) = 0; VOLVER(3, E_NOINTERFACE); }
API(B_AddRef) { BufferX *b = com_host(ARG(0)); VOLVER(1, ++b->r.refs); }
API(B_Release) {
  BufferX *b = com_host(ARG(0));
  if (--b->r.refs > 0) VOLVER(1, (uint32_t)b->r.refs);
  heap_free(0, b->va); com_borrar(b->r.obj); free(b);
  VOLVER(1, 0);
}
API(B_GetBufferPointer) { BufferX *b = com_host(ARG(0)); VOLVER(1, b->va); }
API(B_GetBufferSize) { BufferX *b = com_host(ARG(0)); VOLVER(1, b->tam); }
static const Metodo M_BUF[] = {{"QueryInterface", B_QI}, {"AddRef", B_AddRef}, {"Release", B_Release},
                               {"GetBufferPointer", B_GetBufferPointer}, {"GetBufferSize", B_GetBufferSize}};
static uint32_t buffer_x(uint32_t tam) {
  static uint32_t vt;
  if (!vt) vt = com_vtabla("ID3DXBuffer", M_BUF, 5);
  BufferX *b = calloc(1, sizeof *b);
  b->r.refs = 1; b->tam = tam; b->va = heap_alloc(0, tam, 1);
  return b->r.obj = com_objeto(vt, b);
}

// "Ensambla" guardando el texto: versión, comentario 'LTXT' con la fuente y fin. CreateVertexShader lo traduce.
API(D3DXAssembleShader) {
  uint32_t src = ARG(0), n = ARG(1), salida = ARG(5), errores = ARG(6);
  uint32_t dw = (n + 1 + 3) / 4 + 1; // magia + texto con su cero, en dwords
  uint32_t obj = buffer_x(4 * (dw + 3));
  BufferX *b = com_host(obj);
  uint32_t p = b->va;
  M32(p) = 0xfffe0101u;
  M32(p + 4) = 0xfffeu | (dw << 16);
  M32(p + 8) = 0x5458544cu;
  memcpy(MEM + p + 12, MEM + src, n);
  M8(p + 12 + n) = 0;
  M32(p + 4 + 4 * (dw + 1)) = 0x0000ffffu;
  if (salida) M32(salida) = obj;
  if (errores) M32(errores) = 0;
  VOLVER(7, 0);
}

// ---------------- texturas desde memoria ----------------
static int leer_imagen(const uint8_t *d, uint32_t n, uint32_t *w, uint32_t *h, uint8_t **argb) {
  if (n < 54 || d[0] != 'B' || d[1] != 'M') return 0;
  uint32_t off = d[10] | d[11] << 8 | d[12] << 16 | (uint32_t)d[13] << 24;
  int32_t ww = (int32_t)(d[18] | d[19] << 8 | d[20] << 16 | (uint32_t)d[21] << 24);
  int32_t hh = (int32_t)(d[22] | d[23] << 8 | d[24] << 16 | (uint32_t)d[25] << 24);
  uint32_t bpp = d[28] | d[29] << 8, comp = d[30] | d[31] << 8 | d[32] << 16 | (uint32_t)d[33] << 24;
  if (ww <= 0 || hh == 0 || (bpp != 32 && bpp != 24) || (comp != 0 && comp != 3)) return 0;
  int abajo_arriba = hh > 0;
  uint32_t aw = (uint32_t)ww, ah = (uint32_t)(hh > 0 ? hh : -hh), pitch = ((aw * bpp / 8) + 3) & ~3u;
  if (off + (uint64_t)pitch * ah > n) return 0;
  uint8_t *o = malloc((size_t)aw * ah * 4);
  for (uint32_t y = 0; y < ah; y++) {
    const uint8_t *fila = d + off + (size_t)pitch * (abajo_arriba ? ah - 1 - y : y);
    for (uint32_t x = 0; x < aw; x++) {
      uint8_t *px = o + ((size_t)y * aw + x) * 4;
      px[0] = fila[x * bpp / 8]; px[1] = fila[x * bpp / 8 + 1]; px[2] = fila[x * bpp / 8 + 2];
      px[3] = bpp == 32 ? fila[x * 4 + 3] : 255;
    }
  }
  *w = aw; *h = ah; *argb = o;
  return 1;
}

// Reducción a la mitad con caja de 2x2 (D3DX_FILTER_BOX), con redondeo.
static uint8_t *mitad(const uint8_t *s, uint32_t w, uint32_t h, uint32_t *nw, uint32_t *nh) {
  *nw = w > 1 ? w / 2 : 1; *nh = h > 1 ? h / 2 : 1;
  uint8_t *o = malloc((size_t)*nw * *nh * 4);
  for (uint32_t y = 0; y < *nh; y++) for (uint32_t x = 0; x < *nw; x++) for (int k = 0; k < 4; k++) {
    uint32_t x0 = x * 2, y0 = y * 2, x1 = SDL_min(x0 + 1, w - 1), y1 = SDL_min(y0 + 1, h - 1);
    uint32_t sum = s[(y0 * w + x0) * 4 + k] + s[(y0 * w + x1) * 4 + k] + s[(y1 * w + x0) * 4 + k] + s[(y1 * w + x1) * 4 + k];
    o[(y * *nw + x) * 4 + k] = (uint8_t)((sum + 2) / 4);
  }
  return o;
}

API(D3DXCreateTextureFromFileInMemoryEx) {
  uint32_t datos = ARG(1), tam = ARG(2), ancho = ARG(3), alto = ARG(4), niveles = ARG(5), uso = ARG(6), formato = ARG(7);
  uint32_t pool = ARG(8), clave = ARG(11), info = ARG(12), salida = ARG(14);
  uint32_t w, h;
  uint8_t *img;
  if (!leer_imagen(MEM + datos, tam, &w, &h, &img)) {
    fprintf(stderr, "D3DXCreateTextureFromFileInMemoryEx: formato no soportado (%02x %02x ...)\n", M8(datos), M8(datos + 1));
    VOLVER(15, D3DERR_INVALIDCALL);
  }
  if (ancho == 0 || ancho >= 0xfffffffeu) ancho = w;
  if (alto == 0 || alto >= 0xfffffffeu) alto = h;
  if (ancho != w || alto != h) fprintf(stderr, "D3DX: se pidió %ux%u para una imagen de %ux%u (sin reescalar)\n", ancho, alto, w, h);
  if (formato == 0 || formato == 0xffffffffu) formato = 21;
  if (niveles == 0xffffffffu) niveles = 0;
  if (clave) for (size_t i = 0; i < (size_t)w * h; i++) {
    uint8_t *p = img + 4 * i;
    uint32_t argb = (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0];
    if (argb == clave) p[0] = p[1] = p[2] = p[3] = 0;
  }
  Textura *t = textura_nueva(w, h, niveles, uso, formato, pool);
  // El formato pedido decide cómo queda el dato; se convierte desde A8R8G8B8 por la conversión de subida.
  uint32_t fmt_real = t->formato;
  t->formato = 21;
  uint8_t *nivel = img;
  uint32_t lw = w, lh = h;
  for (uint32_t i = 0; i < t->niveles; i++) {
    textura_subir(t, (int)i, nivel);
    if (i + 1 < t->niveles) {
      uint32_t nw, nh;
      uint8_t *sig = mitad(nivel, lw, lh, &nw, &nh);
      if (nivel != img) free(nivel);
      nivel = sig; lw = nw; lh = nh;
    }
  }
  if (nivel != img) free(nivel);
  free(img);
  t->formato = fmt_real;
  if (info) {
    M32(info) = w; M32(info + 4) = h; M32(info + 8) = 1; M32(info + 12) = 1; M32(info + 16) = 21; M32(info + 20) = 3; M32(info + 24) = 0;
  }
  M32(salida) = obj_textura(t);
  VOLVER(15, 0);
}

const Exportada EXP_D3DX9[] = {
  {"D3DXAssembleShader", D3DXAssembleShader}, {"D3DXMatrixLookAtLH", D3DXMatrixLookAtLH},
  {"D3DXVec3Normalize", D3DXVec3Normalize}, {"D3DXMatrixMultiply", D3DXMatrixMultiply},
  {"D3DXMatrixScaling", D3DXMatrixScaling}, {"D3DXVec4Transform", D3DXVec4Transform},
  {"D3DXMatrixInverse", D3DXMatrixInverse}, {"D3DXMatrixTranspose", D3DXMatrixTranspose},
  {"D3DXMatrixShadow", D3DXMatrixShadow}, {"D3DXMatrixDecompose", D3DXMatrixDecompose},
  {"D3DXQuaternionRotationAxis", D3DXQuaternionRotationAxis}, {"D3DXVec3Transform", D3DXVec3Transform},
  {"D3DXMatrixRotationQuaternion", D3DXMatrixRotationQuaternion}, {"D3DXQuaternionSlerp", D3DXQuaternionSlerp},
  {"D3DXCreateTextureFromFileInMemoryEx", D3DXCreateTextureFromFileInMemoryEx},
  {"D3DXQuaternionMultiply", D3DXQuaternionMultiply}, {"D3DXQuaternionRotationYawPitchRoll", D3DXQuaternionRotationYawPitchRoll},
  {"D3DXMatrixPerspectiveFovLH", D3DXMatrixPerspectiveFovLH},
  {0, 0},
};
