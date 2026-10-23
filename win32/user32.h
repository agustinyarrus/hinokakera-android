#pragma once
#include "../runtime/cpu.h"
#include <SDL3/SDL.h>

typedef struct Ventana {
  uint32_t hwnd, wndproc, estilo, estilo_ex, padre, menu, instancia, usuario;
  int x, y, w, h;   // rectángulo exterior
  int cw, ch;       // área cliente
  int visible, destruida, conserva_sdl;
  char titulo[256];
  SDL_Window *sdl;
  uint32_t extra[16];
} Ventana;

Ventana *ventana(uint32_t hwnd);
Ventana *ventana_principal(void);
void encolar(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp);
uint32_t enviar(Ctx *c, uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp);
void sjis_a_utf8(const char *s, char *out, size_t tam);
extern void (*oyente_eventos)(const SDL_Event *e);
// HINO_OCULTA: pruebas automáticas sin ventana visible. HINO_VENTANA: nunca pantalla completa.
void mostrar_ventana(SDL_Window *w);
int forzar_ventana(void);
