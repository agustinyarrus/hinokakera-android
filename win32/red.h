// Sockets del anfitrión (Winsock en Windows, BSD en Android) para ws2_32.c y el cliente de salas (salas.c).
// Va antes que win32.h, que define "mem". Los errores salen con los números de Winsock, que son los que mira el
// juego (10035 = WSAEWOULDBLOCK, 10054 = WSAECONNRESET, ...).
#pragma once
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define SOCK_INVALIDO INVALID_SOCKET
static inline int error_red(void) { return WSAGetLastError(); }
static inline void no_bloqueante(sock_t s) { u_long v = 1; ioctlsocket(s, FIONBIO, &v); }
#define cerrar_sock closesocket
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int sock_t;
#define SOCK_INVALIDO (-1)
static inline int error_red(void) {
  switch (errno) {
    case EWOULDBLOCK: return 10035;
    case EINPROGRESS: return 10036;
    case EALREADY: return 10037;
    case EMSGSIZE: return 10040;
    case EADDRINUSE: return 10048;
    case ENETUNREACH: return 10051;
    case ECONNRESET: case ECONNREFUSED: return 10054;
    case EISCONN: return 10056;
    case ETIMEDOUT: return 10060;
    case EHOSTUNREACH: return 10065;
    default: return 10050; // WSAENETDOWN
  }
}
static inline void no_bloqueante(sock_t s) { fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK); }
#define cerrar_sock close
#endif
#include <stdint.h>

// El rival del juego en red se ve siempre en esta dirección (la única entrada de net.txt, ver salas.c):
// ws2_32.c la cambia por la dirección real cuando la sala la consigue.
#define IP_VIRTUAL 0x7f4d0001u // 127.77.0.1
#define PUERTO_JUEGO 10996     // el PORT de net.txt; si está ocupado, ws2_32.c prueba los siguientes

int red_socket_juego(sock_t *s, int *puerto);   // ws2_32.c: el socket UDP del juego, si ya lo abrió
void red_rival(const struct sockaddr_in *real); // ws2_32.c: adónde va la dirección virtual (NULL: a ninguna parte)
void red_rival_visto(const struct sockaddr_in *de); // ws2_32.c: otra dirección desde la que llega el rival
int salas_paquete(const uint8_t *d, int n, const struct sockaddr_in *de); // salas.c: 1 si era del lobby
