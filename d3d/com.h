// Objetos COM en la memoria del invitado: [vtable][id del objeto del anfitrión]. Cada entrada de la vtable
// es una dirección del anfitrión (registrar_host); las que no están implementadas fallan con su nombre.
#pragma once
#include "../win32/win32.h"

typedef struct { const char *nombre; FuncionGen fn; } Metodo;

uint32_t com_vtabla(const char *interfaz, const Metodo *metodos, int n);
uint32_t com_objeto(uint32_t vtabla, void *host);
void *com_host(uint32_t obj);
void com_borrar(uint32_t obj);

#define E_NOINTERFACE 0x80004002u
#define E_FAIL 0x80004005u
#define E_OUTOFMEMORY 0x8007000eu
#define D3DERR_INVALIDCALL 0x8876086cu
#define D3DERR_NOTAVAILABLE 0x8876086au
#define D3D_OK 0u
