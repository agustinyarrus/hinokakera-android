// DSOUND: mezclador propio sobre un stream de SDL. Los buffers viven en memoria del invitado (Lock devuelve
// punteros ahí); el hilo de audio de SDL los mezcla con volumen y paneo en centésimas de dB como DirectSound,
// remuestreo lineal y avisos de posición (IDirectSoundNotify) que disparan eventos del juego.
#include "win32.h"
#include "../d3d/com.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <stdlib.h>

typedef struct Sonido {
  int refs, primario;
  uint32_t va, tam, flags;       // datos en el invitado
  uint32_t frec_orig, frec, canales, bits, alinear;
  double pos;                    // posición de lectura en bytes (fraccional por el remuestreo)
  int sonando, bucle;
  int32_t vol, pan;
  struct { uint32_t ofs, evento; } avisos[64];
  int n_avisos;
  uint32_t obj, obj_aviso;
  struct Sonido *original;       // DuplicateSoundBuffer comparte los datos
} Sonido;

#define MAX_SONIDOS 1024
static Sonido *sonidos[MAX_SONIDOS];
static SDL_Mutex *cerrojo;
static SDL_AudioStream *salida;
static int frec_salida = 44100;

void evento_senalar(uint32_t h); // k32_hilos.c

static float ganancia(int32_t cent_db) { return cent_db <= -10000 ? 0.0f : powf(10.0f, (float)cent_db / 2000.0f); }

static float muestra(const Sonido *s, uint32_t byte, int canal) {
  uint32_t i = byte + (uint32_t)(s->canales > 1 ? canal : 0) * (s->bits / 8);
  if (i + s->bits / 8 > s->tam) return 0;
  if (s->bits == 8) return ((float)M8(s->va + i) - 128.0f) / 128.0f;
  return (float)(int16_t)M16(s->va + i) / 32768.0f;
}

static void mezclar(void *ud, SDL_AudioStream *st, int adicional, int total) {
  (void)ud; (void)total;
  int marcos = adicional / 4;
  if (marcos <= 0) return;
  float *acc = calloc((size_t)marcos * 2, sizeof(float));
  SDL_LockMutex(cerrojo);
  for (int k = 0; k < MAX_SONIDOS; k++) {
    Sonido *s = sonidos[k];
    if (!s || !s->sonando || s->primario || !s->tam || !s->alinear) continue;
    float g = ganancia(s->vol), gi = g * (s->pan > 0 ? ganancia(-s->pan) : 1.0f), gd = g * (s->pan < 0 ? ganancia(s->pan) : 1.0f);
    double paso = (double)(s->frec ? s->frec : s->frec_orig) / frec_salida * s->alinear;
    for (int i = 0; i < marcos && s->sonando; i++) {
      uint32_t b = (uint32_t)s->pos / s->alinear * s->alinear;
      double fr = (s->pos - b) / s->alinear;
      uint32_t b2 = b + s->alinear;
      if (b2 >= s->tam) b2 = s->bucle ? 0 : b;
      float l = muestra(s, b, 0) * (float)(1 - fr) + muestra(s, b2, 0) * (float)fr;
      float r = muestra(s, b, 1) * (float)(1 - fr) + muestra(s, b2, 1) * (float)fr;
      acc[2 * i] += l * gi;
      acc[2 * i + 1] += r * gd;
      double antes = s->pos;
      s->pos += paso;
      for (int a = 0; a < s->n_avisos; a++) {
        uint32_t o = s->avisos[a].ofs;
        if (o != 0xffffffffu && antes <= o && s->pos > o) evento_senalar(s->avisos[a].evento);
      }
      if (s->pos >= s->tam) {
        if (s->bucle) s->pos -= s->tam;
        else {
          s->pos = 0; s->sonando = 0;
          for (int a = 0; a < s->n_avisos; a++) if (s->avisos[a].ofs == 0xffffffffu) evento_senalar(s->avisos[a].evento); // DSBPN_OFFSETSTOP
        }
      }
    }
  }
  SDL_UnlockMutex(cerrojo);
  int16_t *o = malloc((size_t)marcos * 4);
  for (int i = 0; i < marcos * 2; i++) {
    float v = acc[i] * 32767.0f;
    o[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
  }
  SDL_PutAudioStreamData(st, o, marcos * 4);
  free(o); free(acc);
}

static void abrir_salida(void) {
  if (salida) return;
  cerrojo = SDL_CreateMutex();
  SDL_AudioSpec spec = {SDL_AUDIO_S16, 2, frec_salida};
  salida = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, mezclar, NULL);
  if (!salida) { fprintf(stderr, "audio: %s (sigue sin sonido)\n", SDL_GetError()); return; }
  SDL_ResumeAudioStreamDevice(salida);
}

static uint32_t obj_sonido(Sonido *s);

static Sonido *registrar(Sonido *s) {
  SDL_LockMutex(cerrojo);
  for (int i = 0; i < MAX_SONIDOS; i++) if (!sonidos[i]) { sonidos[i] = s; break; }
  SDL_UnlockMutex(cerrojo);
  return s;
}

// ---------------- IDirectSound8 ----------------
static int refs_ds = 1;
API(DS_QueryInterface) { M32(ARG(2)) = ARG(0); refs_ds++; VOLVER(3, 0); }
API(DS_AddRef) { VOLVER(1, (uint32_t)++refs_ds); }
API(DS_Release) { VOLVER(1, refs_ds > 1 ? (uint32_t)--refs_ds : 1); }
API(DS_CreateSoundBuffer) {
  uint32_t d = ARG(1), fl = M32(d + 4), wfx = M32(d + 16);
  Sonido *s = calloc(1, sizeof *s);
  s->refs = 1; s->flags = fl; s->primario = fl & 1;
  if (!s->primario) {
    s->tam = M32(d + 8);
    s->va = heap_alloc(0, s->tam, 1);
    if (!s->va) { free(s); VOLVER(4, E_OUTOFMEMORY); }
  }
  if (wfx) {
    s->canales = M16(wfx + 2); s->frec_orig = M32(wfx + 4); s->alinear = M16(wfx + 12); s->bits = M16(wfx + 14);
    if (M16(wfx) != 1) fprintf(stderr, "DirectSound: formato %u no es PCM\n", M16(wfx));
  }
  if (s->bits == 8 && s->va) memset(MEM + s->va, 0x80, s->tam);
  registrar(s);
  M32(ARG(2)) = obj_sonido(s);
  VOLVER(4, 0);
}
API(DS_GetCaps) {
  uint32_t p = ARG(1);
  memset(MEM + p + 4, 0, 92);
  M32(p + 4) = 0x0f5f; M32(p + 8) = 100; M32(p + 12) = 200000; M32(p + 16) = 1;
  VOLVER(2, 0);
}
API(DS_DuplicateSoundBuffer) {
  Sonido *o = com_host(ARG(1));
  if (!o) VOLVER(3, D3DERR_INVALIDCALL);
  Sonido *s = calloc(1, sizeof *s);
  *s = *o;
  s->refs = 1; s->sonando = 0; s->pos = 0; s->n_avisos = 0; s->obj = 0; s->obj_aviso = 0;
  s->original = o->original ? o->original : o;
  s->original->refs++;
  registrar(s);
  M32(ARG(2)) = obj_sonido(s);
  VOLVER(3, 0);
}
API(DS_SetCooperativeLevel) { VOLVER(3, 0); }
API(DS_Compact) { VOLVER(1, 0); }
API(DS_GetSpeakerConfig) { M32(ARG(1)) = 0x00000004u; VOLVER(2, 0); }
API(DS_SetSpeakerConfig) { VOLVER(2, 0); }
static const Metodo M_DS[] = {
  {"QueryInterface", DS_QueryInterface}, {"AddRef", DS_AddRef}, {"Release", DS_Release},
  {"CreateSoundBuffer", DS_CreateSoundBuffer}, {"GetCaps", DS_GetCaps}, {"DuplicateSoundBuffer", DS_DuplicateSoundBuffer},
  {"SetCooperativeLevel", DS_SetCooperativeLevel}, {"Compact", DS_Compact}, {"GetSpeakerConfig", DS_GetSpeakerConfig},
  {"SetSpeakerConfig", DS_SetSpeakerConfig}, {"Initialize", NULL}, {"VerifyCertification", NULL},
};
API(DirectSoundCreate8) {
  abrir_salida();
  static uint32_t obj;
  if (!obj) obj = com_objeto(com_vtabla("IDirectSound8", M_DS, 12), NULL);
  M32(ARG(1)) = obj;
  VOLVER(3, 0);
}

// ---------------- IDirectSoundBuffer8 ----------------
#define SON ((Sonido *)com_host(ARG(0)))
API(SB_QueryInterface) {
  Sonido *s = SON;
  uint32_t iid = M32(ARG(1));
  if (iid == 0xb0210783u) { // IID_IDirectSoundNotify
    static uint32_t vt;
    extern const Metodo M_NOTIF[];
    if (!vt) vt = com_vtabla("IDirectSoundNotify", M_NOTIF, 4);
    if (!s->obj_aviso) s->obj_aviso = com_objeto(vt, s);
    s->refs++;
    M32(ARG(2)) = s->obj_aviso;
    VOLVER(3, 0);
  }
  s->refs++;
  M32(ARG(2)) = ARG(0);
  VOLVER(3, 0);
}
API(SB_AddRef) { VOLVER(1, (uint32_t)++SON->refs); }
static void liberar(Sonido *s) {
  if (--s->refs > 0) return;
  SDL_LockMutex(cerrojo);
  for (int i = 0; i < MAX_SONIDOS; i++) if (sonidos[i] == s) sonidos[i] = NULL;
  SDL_UnlockMutex(cerrojo);
  if (s->original) liberar(s->original);
  else if (s->va) heap_free(0, s->va);
  if (s->obj) com_borrar(s->obj);
  if (s->obj_aviso) com_borrar(s->obj_aviso);
  free(s);
}
API(SB_Release) { Sonido *s = SON; int r = s->refs - 1; liberar(s); VOLVER(1, (uint32_t)(r > 0 ? r : 0)); }
API(SB_GetCaps) {
  Sonido *s = SON;
  uint32_t p = ARG(1);
  M32(p + 4) = s->flags; M32(p + 8) = s->tam; M32(p + 12) = 0; M32(p + 16) = 0;
  VOLVER(2, 0);
}
API(SB_GetCurrentPosition) {
  Sonido *s = SON;
  SDL_LockMutex(cerrojo);
  uint32_t p = s->tam ? (uint32_t)s->pos / (s->alinear ? s->alinear : 1) * (s->alinear ? s->alinear : 1) : 0;
  SDL_UnlockMutex(cerrojo);
  if (ARG(1)) M32(ARG(1)) = p;
  // El cursor de escritura va un poco adelante (~15 ms), como en DirectSound.
  if (ARG(2)) M32(ARG(2)) = s->tam ? (p + (s->frec_orig * s->alinear / 64) / (s->alinear ? s->alinear : 1) * (s->alinear ? s->alinear : 1)) % s->tam : 0;
  VOLVER(3, 0);
}
API(SB_GetFormat) {
  Sonido *s = SON;
  uint32_t p = ARG(1);
  if (p && ARG(2) >= 16) {
    M16(p) = 1; M16(p + 2) = (uint16_t)s->canales; M32(p + 4) = s->frec_orig; M32(p + 8) = s->frec_orig * s->alinear;
    M16(p + 12) = (uint16_t)s->alinear; M16(p + 14) = (uint16_t)s->bits;
    if (ARG(2) >= 18) M16(p + 16) = 0;
  }
  if (ARG(3)) M32(ARG(3)) = 18;
  VOLVER(4, 0);
}
API(SB_GetVolume) { M32(ARG(1)) = (uint32_t)SON->vol; VOLVER(2, 0); }
API(SB_GetPan) { M32(ARG(1)) = (uint32_t)SON->pan; VOLVER(2, 0); }
API(SB_GetFrequency) { Sonido *s = SON; M32(ARG(1)) = s->frec ? s->frec : s->frec_orig; VOLVER(2, 0); }
API(SB_GetStatus) { Sonido *s = SON; M32(ARG(1)) = (s->sonando ? 1u : 0) | (s->sonando && s->bucle ? 4u : 0); VOLVER(2, 0); }
API(SB_Lock) {
  Sonido *s = SON;
  uint32_t ofs = ARG(1), n = ARG(2), fl = ARG(7);
  if (fl & 2) { ofs = 0; n = s->tam; }
  if (fl & 1) ofs = (uint32_t)s->pos;
  if (!s->tam || ofs >= s->tam || n > s->tam) VOLVER(8, D3DERR_INVALIDCALL);
  uint32_t n1 = ofs + n > s->tam ? s->tam - ofs : n;
  M32(ARG(3)) = s->va + ofs; M32(ARG(4)) = n1;
  if (ARG(5)) M32(ARG(5)) = n1 < n ? s->va : 0;
  if (ARG(6)) M32(ARG(6)) = n - n1;
  VOLVER(8, 0);
}
API(SB_Play) {
  Sonido *s = SON;
  SDL_LockMutex(cerrojo);
  s->sonando = 1; s->bucle = (ARG(3) & 1) != 0;
  SDL_UnlockMutex(cerrojo);
  VOLVER(4, 0);
}
API(SB_SetCurrentPosition) { Sonido *s = SON; SDL_LockMutex(cerrojo); s->pos = s->tam ? ARG(1) % s->tam : 0; SDL_UnlockMutex(cerrojo); VOLVER(2, 0); }
API(SB_SetFormat) {
  Sonido *s = SON;
  uint32_t w = ARG(1);
  s->canales = M16(w + 2); s->frec_orig = M32(w + 4); s->alinear = M16(w + 12); s->bits = M16(w + 14);
  VOLVER(2, 0);
}
API(SB_SetVolume) { int32_t v = (int32_t)ARG(1); SON->vol = v < -10000 ? -10000 : v > 0 ? 0 : v; VOLVER(2, 0); }
API(SB_SetPan) { int32_t v = (int32_t)ARG(1); SON->pan = v < -10000 ? -10000 : v > 10000 ? 10000 : v; VOLVER(2, 0); }
API(SB_SetFrequency) { SON->frec = ARG(1); VOLVER(2, 0); }
API(SB_Stop) { Sonido *s = SON; SDL_LockMutex(cerrojo); s->sonando = 0; SDL_UnlockMutex(cerrojo); VOLVER(1, 0); }
API(SB_Unlock) { VOLVER(5, 0); }
API(SB_Restore) { VOLVER(1, 0); }
static const Metodo M_SB[] = {
  {"QueryInterface", SB_QueryInterface}, {"AddRef", SB_AddRef}, {"Release", SB_Release}, {"GetCaps", SB_GetCaps},
  {"GetCurrentPosition", SB_GetCurrentPosition}, {"GetFormat", SB_GetFormat}, {"GetVolume", SB_GetVolume},
  {"GetPan", SB_GetPan}, {"GetFrequency", SB_GetFrequency}, {"GetStatus", SB_GetStatus}, {"Initialize", NULL},
  {"Lock", SB_Lock}, {"Play", SB_Play}, {"SetCurrentPosition", SB_SetCurrentPosition}, {"SetFormat", SB_SetFormat},
  {"SetVolume", SB_SetVolume}, {"SetPan", SB_SetPan}, {"SetFrequency", SB_SetFrequency}, {"Stop", SB_Stop},
  {"Unlock", SB_Unlock}, {"Restore", SB_Restore}, {"SetFX", NULL}, {"AcquireResources", NULL}, {"GetObjectInPath", NULL},
};
static uint32_t obj_sonido(Sonido *s) {
  static uint32_t vt;
  if (!vt) vt = com_vtabla("IDirectSoundBuffer8", M_SB, 24);
  return s->obj = com_objeto(vt, s);
}

// ---------------- IDirectSoundNotify ----------------
API(DN_QueryInterface) { M32(ARG(2)) = ARG(0); VOLVER(3, 0); }
API(DN_AddRef) { VOLVER(1, (uint32_t)++SON->refs); }
API(DN_Release) { Sonido *s = SON; int r = s->refs - 1; liberar(s); VOLVER(1, (uint32_t)(r > 0 ? r : 0)); }
API(DN_SetNotificationPositions) {
  Sonido *s = SON;
  uint32_t n = ARG(1), p = ARG(2);
  SDL_LockMutex(cerrojo);
  s->n_avisos = 0;
  for (uint32_t i = 0; i < n && i < 64; i++) { s->avisos[i].ofs = M32(p + 8 * i); s->avisos[i].evento = M32(p + 8 * i + 4); s->n_avisos++; }
  SDL_UnlockMutex(cerrojo);
  VOLVER(3, 0);
}
const Metodo M_NOTIF[] = {
  {"QueryInterface", DN_QueryInterface}, {"AddRef", DN_AddRef}, {"Release", DN_Release},
  {"SetNotificationPositions", DN_SetNotificationPositions},
};

const Exportada EXP_DSOUND[] = {{"#11", DirectSoundCreate8}, {"DirectSoundCreate8", DirectSoundCreate8}, {0, 0}};
