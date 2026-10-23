#pragma once
#include "cpu.h"

uint32_t registrar_host(FuncionGen fn, const char *nombre); // dirección del invitado que llama a fn
const char *nombre_host(uint32_t va);
FuncionGen buscar_funcion(uint32_t va);
// Llama a una función del invitado con n argumentos (stdcall, o cdecl si cdecl != 0) y devuelve eax.
uint32_t llamar_invitado(Ctx *c, uint32_t fn, int n, const uint32_t *args, int cdecl);
