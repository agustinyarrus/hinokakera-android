#include "gl.h"
#include <SDL3/SDL.h>
#include <stdio.h>

#define GL_DEFINIR(ret, nombre, args) ret(*nombre) args;
GL_FUNCIONES(GL_DEFINIR)

int gl_cargar(void) {
  int ok = 1;
#define GL_CARGAR(ret, nombre, args)                                       \
  nombre = (ret(*) args)SDL_GL_GetProcAddress(#nombre);                    \
  if (!nombre) { fprintf(stderr, "falta la función GL %s\n", #nombre); ok = 0; }
  GL_FUNCIONES(GL_CARGAR)
  return ok;
}
