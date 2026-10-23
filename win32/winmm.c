// WINMM: reloj en milisegundos. (Las funciones mmio se agregan si el juego llega a usarlas.)
#include "win32.h"
#include <SDL3/SDL.h>

API(timeBeginPeriod) { VOLVER(1, 0); }
API(timeEndPeriod) { VOLVER(1, 0); }
API(timeGetTime) { VOLVER(0, (uint32_t)SDL_GetTicks() + 1000000u); } // como si Windows llevara un rato prendido

const Exportada EXP_WINMM[] = {
  {"timeBeginPeriod", timeBeginPeriod}, {"timeEndPeriod", timeEndPeriod}, {"timeGetTime", timeGetTime},
  {0, 0},
};
