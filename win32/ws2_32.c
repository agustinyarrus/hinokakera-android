// WS2_32: lo que usa el netplay (UDP). Sockets reales del anfitrión; sockaddr_in de IPv4 tiene el mismo
// formato en Windows y en Android, así que se copia tal cual. WSAEventSelect: el evento se marca cuando
// alguien lo espera y el socket tiene datos (socket_listo).
#include <stdlib.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define SOCK_INVALIDO INVALID_SOCKET
static int error_red(void) { return WSAGetLastError(); }
static void no_bloqueante(sock_t s) { u_long v = 1; ioctlsocket(s, FIONBIO, &v); }
#define cerrar_sock closesocket
#else
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int sock_t;
#define SOCK_INVALIDO (-1)
static int error_red(void) {
  switch (errno) {
    case EWOULDBLOCK: return 10035;
    case ECONNRESET: case ECONNREFUSED: return 10054;
    case EADDRINUSE: return 10048;
    case EMSGSIZE: return 10040;
    default: return 10050; // WSAENETDOWN
  }
}
static void no_bloqueante(sock_t s) { fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK); }
#define cerrar_sock close
#endif
#include "win32.h" // después de los encabezados del sistema: define "mem"

#define MAX_S 32
static struct { sock_t s; int usado; uint32_t evento; } socks[MAX_S];
static uint32_t ultimo;
#define H_SOCK(i) (0x200u + (uint32_t)(i))

static int indice(uint32_t h) { return h >= 0x200u && h < 0x200u + MAX_S && socks[h - 0x200u].usado ? (int)(h - 0x200u) : -1; }
static uint32_t falla(void) { ultimo = (uint32_t)error_red(); return 0xffffffffu; }

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
  VOLVER(1, 0);
}
API(W_bind) {
  int i = indice(ARG(0));
  if (i < 0) { ultimo = 10038; VOLVER(3, 0xffffffffu); }
  struct sockaddr_in a;
  memcpy(&a, MEM + ARG(1), sizeof a);
  if (bind(socks[i].s, (struct sockaddr *)&a, sizeof a) != 0) VOLVER(3, falla());
  VOLVER(3, 0);
}
API(W_sendto) {
  int i = indice(ARG(0));
  if (i < 0) { ultimo = 10038; VOLVER(6, 0xffffffffu); }
  struct sockaddr_in a;
  memcpy(&a, MEM + ARG(4), sizeof a);
  int n = (int)sendto(socks[i].s, (const char *)MEM + ARG(1), (int)ARG(2), (int)ARG(3), (struct sockaddr *)&a, sizeof a);
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
  invitado_tomar();
  if (n < 0) VOLVER(6, falla());
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
