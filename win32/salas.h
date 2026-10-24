// Salas del juego en red (salas.c): lo que muestra el lobby (d3d/sala.c) y lo que puede pedir.
#pragma once
#include <stdint.h>

typedef enum {
  SL_APAGADO,    // fuera de la pantalla de red
  SL_BUSCANDO,   // sin servidor configurado: lo busca en la red local
  SL_CONECTANDO, // conectando con el servidor de salas
  SL_LOBBY,      // en la lista de salas
  SL_SALA,       // en una sala (propia o de otro)
  SL_EMPEZANDO,  // apretó Start: el modo red original hace el pedido y la aceptación
  SL_JUGANDO,    // en la pelea (el lobby ya no se ve)
} EstadoSalas;

typedef enum { EN_NADA, EN_BUSCANDO, EN_LISTO, EN_FALLO } Enlace; // camino directo con el rival

typedef struct { int id, delay; char estado; char anfitrion[17], invitado[17]; } FilaSala; // estado: e, l, j

#define MAX_SALAS 64
typedef struct {
  EstadoSalas estado;
  int visible;              // el lobby tapa la pantalla de red (y el juego no recibe las teclas)
  float opacidad;           // baja a 0 cuando la pantalla de red se va (a la pelea o al título)
  int fallo_conexion;       // no se pudo llegar al servidor
  char nombre[17], servidor[64];
  int conectados, n_salas;
  FilaSala salas[MAX_SALAS];
  // La sala en la que está (sala_id 0: ninguna).
  int sala_id, soy_anfitrion, delay;
  char sala_estado, anfitrion[17], invitado[17];
  Enlace enlace;
  int ping;                 // ms, del camino directo
  int puerto;               // puerto UDP real del juego
  char aviso[100];
  uint32_t aviso_hasta;     // en ms de SDL_GetTicks (0: fijo)
} Salas;
extern Salas salas;

void salas_cuadro(void);                 // en cada cuadro (dibujo.c), con el turno del invitado
void sala_escena_red(uint32_t escena);   // la llama el update de la pantalla de red (analisis/parches.txt)
void salas_crear(void);
void salas_unirse(int id);
void salas_salir(void);
void salas_empezar(void);
void salas_cambiar_delay(void);          // 1..5, en la sala propia
void salas_configurar(const char *nombre, const char *servidor);
void salas_volver(void);                 // al título, como el botón B de la pantalla de red
extern uint8_t teclas_sala[256];         // las que aprieta por el jugador (dinput8.c las usa mientras el lobby se ve)
