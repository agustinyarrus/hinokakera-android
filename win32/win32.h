// Base de las DLL de Windows reimplementadas. Cada función recibe el contexto del hilo que la llamó:
// los argumentos están en la pila del invitado (después de la dirección de retorno).
#pragma once
#include "../runtime/cpu.h"
#include "../runtime/despacho.h"
#include "../runtime/hilo.h"
#include "../runtime/memoria.h"
#include <stdio.h>
#include <string.h>

#define mem MEM
#define ARG(n) M32(c->esp + 4 + 4 * (n))
// stdcall: la función saca sus argumentos; cdecl: los saca el que llama.
#define VOLVER(nargs, valor) do { c->eax = (uint32_t)(valor); c->esp += 4 + 4 * (nargs); return; } while (0)
#define VOLVER_CDECL(valor) do { c->eax = (uint32_t)(valor); c->esp += 4; return; } while (0)
#define API(nombre) static void nombre(Ctx *c)

typedef struct { const char *nombre; FuncionGen fn; } Exportada;

static inline char *gstr(uint32_t va) { return va ? (char *)(MEM + va) : NULL; }
static inline uint16_t *gwstr(uint32_t va) { return va ? (uint16_t *)(MEM + va) : NULL; }

void poner_error(uint32_t err); // SetLastError del hilo actual

// Heap del proceso (k32_memoria.c), también para el runtime.
uint32_t heap_alloc(uint32_t heap, uint32_t tam, int cero);
void heap_free(uint32_t heap, uint32_t va);
uint32_t heap_tam(uint32_t va);
uint32_t heap_proceso(void);
uint32_t gstrdup(const char *s);

// Objetos del núcleo (k32_hilos.c).
typedef enum { OBJ_LIBRE, OBJ_ARCHIVO, OBJ_EVENTO, OBJ_MUTEX, OBJ_HILO, OBJ_BUSQUEDA, OBJ_SEMAFORO, OBJ_OTRO } TipoObjeto;
uint32_t objeto_nuevo(TipoObjeto tipo, void *dato);
void *objeto_dato(uint32_t h, TipoObjeto tipo);
int objeto_cerrar(uint32_t h);

// Rutas del invitado -> anfitrión (k32_archivos.c).
const char *ruta_host(const char *ruta_invitado, int escribir, char *buf, size_t tam);
void rutas_iniciar(const char *datos, const char *escritura);

// Códigos de error de Windows que se usan.
#define ERROR_FILE_NOT_FOUND 2
#define ERROR_PATH_NOT_FOUND 3
#define ERROR_ACCESS_DENIED 5
#define ERROR_INVALID_HANDLE 6
#define ERROR_NOT_ENOUGH_MEMORY 8
#define ERROR_NO_MORE_FILES 18
#define ERROR_INVALID_PARAMETER 87
#define ERROR_INSUFFICIENT_BUFFER 122
#define ERROR_ALREADY_EXISTS 183
#define ERROR_CALL_NOT_IMPLEMENTED 120
