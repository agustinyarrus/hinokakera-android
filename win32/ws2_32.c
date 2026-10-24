// WS2_32: lo que usa el netplay (UDP). Sockets reales del anfitrión; sockaddr_in de IPv4 tiene el mismo
// formato en Windows y en Android, así que se copia tal cual. WSAEventSelect: el evento se marca cuando
// alguien lo espera y el socket tiene datos (socket_listo).
// Para las salas (salas.c): el socket del juego también lleva los paquetes del lobby ("HKL..."), que no le llegan
// al juego, y la dirección virtual IP_VIRTUAL se traduce a la del rival en los dos sentidos.
#include <stdlib.h>
#include "red.h"
#include "win32.h" // después de los encabezados del sistema: define "mem"

#define MAX_S 32
static struct { sock_t s; int usado; uint32_t evento; } socks[MAX_S];
static _Thread_local uint32_t ultimo; // WSAGetLastError es por hilo
static int juego = -1, puerto_juego;  // el socket UDP que ató el juego y su puerto real
static struct sockaddr_in rival;      // adónde va IP_VIRTUAL (sin_family 0: todavía a ninguna parte)
// Desde dónde puede llegar el rival: detrás de un NAT simétrico sale por un puerto distinto según a qué dirección le
// manda, y todo lo que llega de cualquiera de ellas es suyo (el juego lo ve como IP_VIRTUAL).
static struct sockaddr_in visto[4];
static int n_visto;
#define H_SOCK(i) (0x200u + (uint32_t)(i))

static int indice(uint32_t h) { return h >= 0x200u && h < 0x200u + MAX_S && socks[h - 0x200u].usado ? (int)(h - 0x200u) : -1; }
static uint32_t falla(void) { ultimo = (uint32_t)error_red(); return 0xffffffffu; }

int red_socket_juego(sock_t *s, int *puerto) {
  if (juego < 0 || !socks[juego].usado) return 0;
  *s = socks[juego].s; *puerto = puerto_juego;
  return 1;
}
void red_rival(const struct sockaddr_in *real) {
  if (real) rival = *real;
  else { memset(&rival, 0, sizeof rival); n_visto = 0; }
}
static int es_rival(const struct sockaddr_in *a) {
  for (int i = 0; i < n_visto; i++) if (visto[i].sin_addr.s_addr == a->sin_addr.s_addr && visto[i].sin_port == a->sin_port) return 1;
  return 0;
}
void red_rival_visto(const struct sockaddr_in *de) {
  if (es_rival(de)) return;
  visto[n_visto < 4 ? n_visto++ : 3] = *de;
}

// HINO_LOG_RED=1: cada paquete que manda o recibe el juego (tipo, tamaño, dirección y los primeros bytes).
static void log_paquete(const char *que, const struct sockaddr_in *a, const uint8_t *d, int n) {
  static int log = -1;
  if (log < 0) log = getenv("HINO_LOG_RED") != NULL;
  if (!log || n <= 0) return;
  const uint8_t *ip = (const uint8_t *)&a->sin_addr;
  char hex[3 * 24 + 1] = {0};
  for (int i = 0; i < n && i < 24; i++) snprintf(hex + 3 * i, 4, "%02x ", d[i]);
  fprintf(stderr, "red: %s %u.%u.%u.%u:%u tipo %02x, %d bytes: %s\n", que, ip[0], ip[1], ip[2], ip[3], ntohs(a->sin_port), d[0], n, hex);
}

API(W_WSAStartup) {
#ifdef _WIN32
  WSADATA w;
  WSAStartup(0x0202, &w);
#endif
  uint32_t p = ARG(1);
  memset(MEM + p, 0, 400);
  M16(p) = (uint16_t)ARG(0); M16(p + 2) = 0x0202; // wVersion = la pedida (el juego pide 2.0 y lo verifica)
  strcpy(gstr(p + 4), "WinSock 2.0");
  strcpy(gstr(p + 261), "Running");
  M16(p + 390) = 0; M16(p + 392) = 65467;
  VOLVER(2, 0);
}
API(W_WSACleanup) { VOLVER(0, 0); }
API(W_WSAGetLastError) { VOLVER(0, ultimo); }
API(W_htons) { uint32_t v = ARG(0) & 0xffff; VOLVER(1, ((v & 0xff) << 8) | (v >> 8)); }
API(W_socket) {
  sock_t s = socket((int)ARG(0), (int)ARG(1), (int)ARG(2));
  if (s == SOCK_INVALIDO) VOLVER(3, falla());
  for (int i = 0; i < MAX_S; i++) if (!socks[i].usado) { socks[i].s = s; socks[i].usado = 1; socks[i].evento = 0; VOLVER(3, H_SOCK(i)); }
  cerrar_sock(s);
  ultimo = 10024; // WSAEMFILE
  VOLVER(3, 0xffffffffu);
}
API(W_closesocket) {
  int i = indice(ARG(0));
  if (i < 0) { ultimo = 10038; VOLVER(1, 0xffffffffu); }
  cerrar_sock(socks[i].s);
  socks[i].usado = 0;
  if (i == juego) juego = -1;
  VOLVER(1, 0);
}
API(W_bind) {
  int i = indice(ARG(0));
  if (i < 0) { ultimo = 10038; VOLVER(3, 0xffffffffu); }
  struct sockaddr_in a;
  memcpy(&a, MEM + ARG(1), sizeof a);
  int r = bind(socks[i].s, (struct sockaddr *)&a, sizeof a);
  // Ocupado (otra copia del juego en la misma máquina): los siguientes. El juego sigue creyendo que es el suyo.
  for (int k = 1; r != 0 && error_red() == 10048 && k <= 20; k++) {
    a.sin_port = htons((uint16_t)(ntohs(((struct sockaddr_in *)(MEM + ARG(1)))->sin_port) + k));
    r = bind(socks[i].s, (struct sockaddr *)&a, sizeof a);
  }
  if (r != 0) VOLVER(3, falla());
  juego = i; puerto_juego = ntohs(a.sin_port);
  VOLVER(3, 0);
}
API(W_sendto) {
  int i = indice(ARG(0));
  if (i < 0) { ultimo = 10038; VOLVER(6, 0xffffffffu); }
  struct sockaddr_in a;
  memcpy(&a, MEM + ARG(4), sizeof a);
  if (ntohl(a.sin_addr.s_addr) == IP_VIRTUAL) {
    if (!rival.sin_family) VOLVER(6, ARG(2)); // todavía no hay rival: como si hubiera salido
    a = rival;
  }
  int n = (int)sendto(socks[i].s, (const char *)MEM + ARG(1), (int)ARG(2), (int)ARG(3), (struct sockaddr *)&a, sizeof a);
  log_paquete("->", &a, MEM + ARG(1), n);
  VOLVER(6, n < 0 ? falla() : (uint32_t)n);
}
API(W_recvfrom) {
  int i = indice(ARG(0));
  if (i < 0) { ultimo = 10038; VOLVER(6, 0xffffffffu); }
  struct sockaddr_in a;
  socklen_t l = sizeof a;
  uint32_t buf = ARG(1), tam = ARG(2), flags = ARG(3);
  invitado_soltar(); // puede bloquear (socket bloqueante): los otros hilos del juego siguen
  int n = (int)recvfrom(socks[i].s, (char *)MEM + buf, (int)tam, (int)flags, (struct sockaddr *)&a, &l);
  uint32_t err = n < 0 ? (uint32_t)error_red() : 0; // antes de tomar el turno: el candado puede pisar el error
  invitado_tomar();
  if (n < 0) { ultimo = err; VOLVER(6, 0xffffffffu); }
  // Del lobby: no es para el juego (el socket es no bloqueante, así que vuelve a preguntar enseguida).
  if (n >= 4 && !memcmp(MEM + buf, "HKL", 3) && salas_paquete(MEM + buf, n, &a)) { ultimo = 10035; VOLVER(6, 0xffffffffu); }
  log_paquete("<-", &a, MEM + buf, n);
  if (rival.sin_family && es_rival(&a)) {
    rival = a; // se le contesta por donde llegó lo último (detrás de un NAT, es el puerto que está abierto)
    a.sin_addr.s_addr = htonl(IP_VIRTUAL); a.sin_port = htons(PUERTO_JUEGO);
  }
  if (ARG(4)) memcpy(MEM + ARG(4), &a, sizeof a);
  if (ARG(5)) M32(ARG(5)) = sizeof a;
  VOLVER(6, (uint32_t)n);
}

uint32_t evento_nuevo(int manual); // k32_hilos.c
API(W_WSACreateEvent) { VOLVER(0, evento_nuevo(1)); }
API(W_WSACloseEvent) { objeto_cerrar(ARG(0)); VOLVER(1, 1); }
API(W_WSAEventSelect) {
  int i = indice(ARG(0));
  if (i < 0) { ultimo = 10038; VOLVER(3, 0xffffffffu); }
  socks[i].evento = ARG(1);
  no_bloqueante(socks[i].s);
  VOLVER(3, 0);
}

// Llamado al esperar un evento: si es de un socket y llegó algo dentro del plazo, devuelve 1.
int socket_listo(uint32_t evento, uint32_t ms) {
  for (int i = 0; i < MAX_S; i++) {
    if (!socks[i].usado || socks[i].evento != evento) continue;
    fd_set f;
    FD_ZERO(&f);
    FD_SET(socks[i].s, &f);
    struct timeval t = {(long)(ms == 0xffffffffu ? 3600 : ms / 1000), (long)(ms == 0xffffffffu ? 0 : (ms % 1000) * 1000)};
    return select((int)socks[i].s + 1, &f, NULL, NULL, &t) > 0;
  }
  return -1; // no es de un socket
}

const Exportada EXP_WS2_32[] = {
  {"#115", W_WSAStartup}, {"#116", W_WSACleanup}, {"#111", W_WSAGetLastError}, {"#9", W_htons}, {"#23", W_socket},
  {"#3", W_closesocket}, {"#2", W_bind}, {"#20", W_sendto}, {"#17", W_recvfrom}, {"WSACreateEvent", W_WSACreateEvent},
  {"WSACloseEvent", W_WSACloseEvent}, {"WSAEventSelect", W_WSAEventSelect},
  {0, 0},
};
