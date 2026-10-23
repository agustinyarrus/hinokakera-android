// Hilos del invitado: contexto de CPU, TEB en memoria del invitado y su pila.
#pragma once
#include "cpu.h"

typedef struct Hilo {
  Ctx ctx;
  uint32_t teb, pila_base, pila_tam;
  uint32_t id, manejador;
  uint32_t rastro[1024];
  int n_rastro;
  struct SDL_Thread *sdl;
} Hilo;

extern _Thread_local Hilo *hilo_actual;
extern uint32_t PEB;

Hilo *hilo_nuevo(uint32_t tam_pila); // TEB, pila y FPU como Windows deja un hilo nuevo
void hilo_rastro(Hilo *h);           // imprime la pila de funciones del invitado (con -DRASTRO)

// Candado del invitado: un solo hilo del juego corre a la vez y se pasan el turno al bloquear (Sleep, esperas,
// el vsync). El juego cuenta con el orden de memoria de x86, donde las escrituras de un hilo se ven en orden; en
// ARM no, y el hilo de la música leía el decodificador a medio armar por el hilo principal (crash en el celu, nunca
// en x86). Cada pase de turno es una barrera completa. Las APIs que bloquean lo sueltan alrededor de la espera.
void invitado_tomar(void);
void invitado_soltar(void);
void invitado_ceder(void); // si otro hilo espera turno, se lo da

// Offsets del TEB que usan el CRT y MFC.
#define TEB_EXCEPCIONES 0x00
#define TEB_PILA_TOPE 0x04
#define TEB_PILA_LIMITE 0x08
#define TEB_SELF 0x18
#define TEB_PROCESO 0x20
#define TEB_HILO 0x24
#define TEB_PEB 0x30
#define TEB_ULTIMO_ERROR 0x34
#define TEB_TLS 0xe10
#define N_TLS 64
