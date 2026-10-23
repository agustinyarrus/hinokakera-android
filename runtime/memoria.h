// Espacio de direcciones del invitado (4 GB reservados en el anfitrión), con reservas y compromisos
// por página al estilo VirtualAlloc.
#pragma once
#include <stdint.h>

#define PAGINA 0x1000u
#define GRANO 0x10000u

void mem_iniciar(void);
uint32_t mem_reservar(uint32_t va, uint32_t tam, int comprometer); // va = 0: donde haya lugar. 0 si falla
int mem_comprometer(uint32_t va, uint32_t tam);
void mem_descomprometer(uint32_t va, uint32_t tam);
int mem_liberar(uint32_t va);                       // libera la reserva que empieza en va
int mem_valida(uint32_t va, uint32_t tam);          // todas sus páginas comprometidas
uint32_t mem_reserva_de(uint32_t va, uint32_t *tam); // inicio y tamaño de la reserva que contiene va (0 si libre)
uint32_t mem_bloque(uint32_t tam);                  // memoria para el runtime, comprometida y en cero
