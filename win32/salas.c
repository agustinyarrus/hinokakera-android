// Cliente de salas (servidor/salas.mjs) y pegamento con el juego en red original del exe.
//
// La pantalla "VS Human (Network)" (vtable en 0x54dab8: init 0x486570, update 0x487030) lee net.txt (nombre, puerto
// UDP y una lista de IP:puerto a las que les pregunta el estado cada 4 s). Protocolo del juego: 0x01 estado? ->
// 0x02 estado (solo en "Server On"), 0x0B pedido de pelea -> 0x0D aceptado (0x0E rechazado) -> 0x11 confirmación,
// y de ahí 0x15 con las entradas de cada cuadro. El que pide la pelea queda 2P. Acá:
//  - net.txt (en la carpeta de escritura, que gana a la de datos) tiene una sola entrada, IP_VIRTUAL:10996, que
//    ws2_32.c traduce a la dirección real del rival;
//  - por TCP se habla con el servidor de salas; desde el socket del juego se le manda un UDP para que vea la
//    dirección pública que le dio el NAT, y con la del rival (la pública y la local) se abre el camino directo
//    desde ese mismo socket: los dos se mandan paquetes hasta que llegan ("perforar" el NAT);
//  - al empezar, se aprietan por el jugador las teclas del modo red original: C (Server On) en el anfitrión, A sobre
//    la entrada (pedir pelea) en el invitado y A (aceptar) en el anfitrión. De ahí sigue el juego solo.
#include "red.h"
#include "win32.h"
#include "salas.h"
#include <SDL3/SDL.h>
#include <stdarg.h>
#include <stdlib.h>
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0 // Windows no tiene SIGPIPE
#endif

#define VERSION 1
#define PUERTO_SALAS 10995
static const char COMENTARIO[] = "Hinokakera CE";

// Campos de la pantalla de red (this de su update) y del objeto de red del juego ([[escena+8]+34h]).
#define E_FASE 0x10      // 2 andando, 8 saliendo
#define E_SIGUIENTE 0x18 // a qué escena sale: 0x1c selección de personaje (la pelea en red)
#define E_PAGINA 0xa8
#define E_FILA 0xac
#define E_SERVIDOR 0xb0  // Server On
#define E_DELAY 0xb4
#define E_SUB 0xc0       // 0 lista, 1 esperando la respuesta al pedido, 3 le pidieron pelea, 0xb-0xd fallos
#define R_DELAY 0x8a8
#define R_ESTADO0 (0x8bc + 0x88) // la primera entrada: 0 Unknown, 1 Standby, 2 Busy
#define R_PEDIDO_DIR 0x371c0     // sockaddr_in del que pidió la pelea
#define R_SESION 0x370d8         // el juego en red anda (selección de personaje y pelea)

Salas salas;
uint8_t teclas_sala[256];
static uint32_t escena, escena_cuadro, red_obj, cuadro;
static uint8_t tecla_a = 0x2c, tecla_b = 0x2d, tecla_c = 0x2e; // las del jugador 1 en config.dat
static struct { uint8_t dik; int resta; } pulso;            // tecla del lobby: 3 cuadros abajo y 3 arriba

static void aviso(uint32_t ms, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(salas.aviso, sizeof salas.aviso, fmt, ap);
  va_end(ap);
  salas.aviso_hasta = ms ? (uint32_t)SDL_GetTicks() + ms : 0;
  fprintf(stderr, "salas: %s\n", salas.aviso);
}

static void limpiar_nombre(char *d, const char *s, size_t tam) {
  size_t n = 0;
  for (; *s && n + 1 < tam; s++) if (*s >= 0x20 && *s < 0x7f && *s != '\t') d[n++] = *s;
  while (n && d[n - 1] == ' ') n--;
  d[n] = 0;
}

// ---------------- configuración (salas.txt) y net.txt, en la carpeta de escritura ----------------
static void guardar_config(void) {
  char ruta[700], t[160];
  int n = snprintf(t, sizeof t, "nombre=%s\nservidor=%s\n", salas.nombre, salas.servidor);
  const char *r = ruta_host("C:\\HINOKAKERA\\salas.txt", 1, ruta, sizeof ruta);
  if (r) SDL_SaveFile(r, t, (size_t)n);
}
static void escribir_net_txt(void) {
  char ruta[700], t[200];
  int n = snprintf(t, sizeof t, "NAME=%s\r\nPORT=%d\r\nCOMMENT=%s\r\n127.77.0.1:%d sala\r\n", salas.nombre, PUERTO_JUEGO, COMENTARIO, PUERTO_JUEGO);
  const char *r = ruta_host("C:\\HINOKAKERA\\net.txt", 1, ruta, sizeof ruta);
  if (r) SDL_SaveFile(r, t, (size_t)n);
}
static void cargar_config(void) {
  char ruta[700];
  size_t n = 0;
  const char *r = ruta_host("C:\\HINOKAKERA\\salas.txt", 0, ruta, sizeof ruta);
  char *t = r ? SDL_LoadFile(r, &n) : NULL;
  for (char *l = t, *sig; l && *l; l = sig) {
    sig = strchr(l, '\n');
    if (sig) *sig++ = 0;
    char *cr = strchr(l, '\r');
    if (cr) *cr = 0;
    if (!strncmp(l, "nombre=", 7)) limpiar_nombre(salas.nombre, l + 7, sizeof salas.nombre);
    if (!strncmp(l, "servidor=", 9)) limpiar_nombre(salas.servidor, l + 9, sizeof salas.servidor);
    if (!sig) break;
  }
  SDL_free(t);
  // Para las pruebas: HINO_SALAS=servidor:puerto y HINO_NOMBRE.
  if (getenv("HINO_SALAS")) limpiar_nombre(salas.servidor, getenv("HINO_SALAS"), sizeof salas.servidor);
  if (getenv("HINO_NOMBRE")) limpiar_nombre(salas.nombre, getenv("HINO_NOMBRE"), sizeof salas.nombre);
  if (!salas.nombre[0]) { snprintf(salas.nombre, sizeof salas.nombre, "Player%02d", (int)SDL_rand(90) + 10); guardar_config(); }
  // Las teclas A, B y C del jugador 1 (config.dat, desde 0x50: izquierda, derecha, arriba, abajo, A, B, C, D).
  uint8_t *cfg = (r = ruta_host("C:\\HINOKAKERA\\config.dat", 0, ruta, sizeof ruta)) ? SDL_LoadFile(r, &n) : NULL;
  if (cfg && n >= 0x70 && !memcmp(cfg, "cfg_", 4)) {
    uint8_t *k[3] = {&tecla_a, &tecla_b, &tecla_c};
    for (int i = 0; i < 3; i++) { uint32_t v; memcpy(&v, cfg + 0x60 + 4 * i, 4); if (v && v < 256) *k[i] = (uint8_t)v; }
  }
  SDL_free(cfg);
}

// ---------------- conexión con el servidor (TCP) ----------------
typedef struct { char servidor[64]; SDL_AtomicInt listo; sock_t s; int ok; struct sockaddr_in dir, local; } Conexion;
static Conexion *conexion;    // la que está en curso (en su hilo)
static sock_t tcp = SOCK_INVALIDO;
static struct sockaddr_in dir_servidor, dir_local;
static char rx[8192], tx[8192], token[17];
static int rx_n, tx_n, registrado;
static uint32_t t_ping, t_registro, t_reintento, t_estado;

static void iniciar_winsock(void) {
#ifdef _WIN32
  static int hecho;
  WSADATA w;
  if (!hecho++) WSAStartup(0x0202, &w);
#endif
}

// En un hilo: el nombre del servidor se resuelve con getaddrinfo, que puede tardar.
static int conectar_hilo(void *p) {
  Conexion *c = p;
  char host[64];
  int puerto = PUERTO_SALAS;
  SDL_strlcpy(host, c->servidor, sizeof host);
  char *dp = strrchr(host, ':');
  if (dp) { *dp = 0; puerto = atoi(dp + 1); }
  struct addrinfo pista, *r = NULL;
  memset(&pista, 0, sizeof pista);
  pista.ai_family = AF_INET;
  pista.ai_socktype = SOCK_STREAM;
  if (host[0] && puerto > 0 && puerto < 65536 && !getaddrinfo(host, NULL, &pista, &r) && r) {
    memcpy(&c->dir, r->ai_addr, sizeof c->dir);
    c->dir.sin_port = htons((uint16_t)puerto);
    freeaddrinfo(r);
    sock_t s = socket(AF_INET, SOCK_STREAM, 0);
    if (s != SOCK_INVALIDO) {
      no_bloqueante(s);
      int e = connect(s, (struct sockaddr *)&c->dir, sizeof c->dir), err = e ? error_red() : 0;
      if (e == 0 || err == 10035 || err == 10036) {
        fd_set w, x;
        FD_ZERO(&w); FD_SET(s, &w); FD_ZERO(&x); FD_SET(s, &x);
        struct timeval t = {5, 0};
        int listo = e == 0 || (select((int)s + 1, NULL, &w, &x, &t) > 0 && FD_ISSET(s, &w) && !FD_ISSET(s, &x));
        int so = 0;
        socklen_t l = sizeof so;
        if (listo && !getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&so, &l) && !so) { c->s = s; c->ok = 1; }
      }
      if (!c->ok) cerrar_sock(s);
    }
    // La IP local con la que se sale hacia el servidor: es la que sirve a un rival de la misma red.
    sock_t u = c->ok ? socket(AF_INET, SOCK_DGRAM, 0) : SOCK_INVALIDO;
    if (u != SOCK_INVALIDO) {
      socklen_t l = sizeof c->local;
      if (connect(u, (struct sockaddr *)&c->dir, sizeof c->dir) || getsockname(u, (struct sockaddr *)&c->local, &l)) memset(&c->local, 0, sizeof c->local);
      cerrar_sock(u);
    }
  }
  SDL_SetAtomicInt(&c->listo, 1);
  return 0;
}

static void conectar(void) {
  if (conexion) return;
  iniciar_winsock();
  conexion = calloc(1, sizeof *conexion);
  SDL_strlcpy(conexion->servidor, salas.servidor, sizeof conexion->servidor);
  SDL_Thread *h = SDL_CreateThread(conectar_hilo, "salas", conexion);
  if (h) SDL_DetachThread(h);
  else { free(conexion); conexion = NULL; }
  salas.estado = SL_CONECTANDO;
  salas.fallo_conexion = 0;
  t_estado = (uint32_t)SDL_GetTicks();
}

static void mandar(const char *fmt, ...) {
  if (tcp == SOCK_INVALIDO) return;
  char l[600];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(l, sizeof l - 1, fmt, ap);
  va_end(ap);
  if (n < 0 || n >= (int)sizeof l - 1) return;
  l[n++] = '\n';
  if (tx_n + n > (int)sizeof tx) return; // el servidor no lee: se pierde (no debería pasar con líneas tan cortas)
  memcpy(tx + tx_n, l, (size_t)n);
  tx_n += n;
  while (tx_n > 0) {
    int k = (int)send(tcp, tx, tx_n, MSG_NOSIGNAL);
    if (k <= 0) break;
    memmove(tx, tx + k, (size_t)(tx_n - k));
    tx_n -= k;
  }
}

static void sin_rival(void) {
  salas.enlace = EN_NADA;
  salas.ping = 0;
  red_rival(NULL);
}
static void desconectar(void) {
  if (tcp != SOCK_INVALIDO) cerrar_sock(tcp);
  tcp = SOCK_INVALIDO;
  rx_n = tx_n = registrado = 0;
  salas.sala_id = 0;
  salas.n_salas = 0;
  sin_rival();
}

// ---------------- camino directo con el rival (UDP por el socket del juego) ----------------
static struct { char clave[17]; struct sockaddr_in cand[2], real; int n_cand, hay_real; uint32_t desde, ultimo_envio, seq; } par;

static int udp(const void *d, int n, const struct sockaddr_in *a) {
  sock_t s;
  int p;
  return red_socket_juego(&s, &p) && sendto(s, (const char *)d, n, 0, (const struct sockaddr *)a, sizeof *a) == n;
}
static void perforar(uint32_t ahora) {
  uint8_t m[28];
  memcpy(m, "HKLP", 4);
  memcpy(m + 4, par.clave, 16);
  uint32_t seq = ++par.seq;
  memcpy(m + 20, &seq, 4);
  memcpy(m + 24, &ahora, 4);
  if (par.hay_real) udp(m, 28, &par.real);
  if (salas.enlace == EN_LISTO) { par.ultimo_envio = ahora; return; } // ya hay camino: solo por ahí
  for (int i = 0; i < par.n_cand; i++)
    if (!par.hay_real || par.cand[i].sin_addr.s_addr != par.real.sin_addr.s_addr || par.cand[i].sin_port != par.real.sin_port) udp(m, 28, &par.cand[i]);
  par.ultimo_envio = ahora;
}
static int direccion(struct sockaddr_in *a, const char *ip, const char *puerto) {
  memset(a, 0, sizeof *a);
  a->sin_family = AF_INET;
  a->sin_port = htons((uint16_t)atoi(puerto));
  return ip[0] && atoi(puerto) > 0 && inet_pton(AF_INET, ip, &a->sin_addr) == 1;
}

int salas_paquete(const uint8_t *d, int n, const struct sockaddr_in *de) {
  if (n >= 4 && !memcmp(d, "HKLV", 4)) { registrado = 1; return 1; }
  if (n < 28 || (memcmp(d, "HKLP", 4) && memcmp(d, "HKLQ", 4))) return 1;
  if (!salas.sala_id || memcmp(d + 4, par.clave, 16)) return 1; // de otra sala o viejo
  // Todo lo que llega con la clave es del rival, venga de donde venga (con NAT simétrico, de varios puertos), y se le
  // manda a donde salió lo último que llegó suyo: es el puerto que su NAT tiene abierto para nosotros (ws2_32.c hace
  // lo mismo con los paquetes del juego).
  red_rival_visto(de);
  par.real = *de;
  par.hay_real = 1;
  if (salas.enlace == EN_LISTO) red_rival(&par.real);
  if (d[3] == 'P') { // el otro llega hasta acá: se le contesta por donde vino
    uint8_t q[28];
    memcpy(q, d, 28);
    q[3] = 'Q';
    udp(q, 28, de);
    return 1;
  }
  uint32_t t;
  memcpy(&t, d + 24, 4);
  salas.ping = (int)((uint32_t)SDL_GetTicks() - t);
  if (salas.enlace != EN_LISTO) {
    salas.enlace = EN_LISTO;
    red_rival(&par.real);
    char ip[INET_ADDRSTRLEN] = "";
    inet_ntop(AF_INET, &de->sin_addr, ip, sizeof ip);
    fprintf(stderr, "salas: camino directo con el rival por %s:%d (%d ms)\n", ip, ntohs(de->sin_port), salas.ping);
  }
  return 1;
}

// ---------------- mensajes del servidor ----------------
static void copiar(char *d, const char *s, size_t tam) { limpiar_nombre(d, s ? s : "", tam); }

static void linea(char *l) {
  char *f[8];
  int n = 0;
  for (char *p = l; n < 8;) { // campos separados por tabulador (pueden quedar vacíos)
    f[n++] = p;
    char *t = strchr(p, '\t');
    if (!t) break;
    *t = 0;
    p = t + 1;
  }
  for (int i = n; i < 8; i++) f[i] = "";
  if (!strcmp(f[0], "BIENVENIDO")) {
    SDL_strlcpy(token, f[2], sizeof token);
    salas.estado = SL_LOBBY;
    registrado = 0;
    t_registro = 0;
    salas.aviso[0] = 0;
  } else if (!strcmp(f[0], "LISTA")) {
    salas.conectados = atoi(f[1]);
    salas.n_salas = 0;
  } else if (!strcmp(f[0], "S") && salas.n_salas < MAX_SALAS) {
    FilaSala *s = &salas.salas[salas.n_salas++];
    s->id = atoi(f[1]);
    s->estado = f[2][0];
    s->delay = atoi(f[3]);
    copiar(s->anfitrion, f[4], sizeof s->anfitrion);
    copiar(s->invitado, f[5], sizeof s->invitado);
  } else if (!strcmp(f[0], "SALA")) {
    if (f[1][0] == '-') {
      salas.sala_id = 0;
      sin_rival();
      if (salas.estado == SL_SALA || salas.estado == SL_EMPEZANDO) salas.estado = SL_LOBBY;
      return;
    }
    int antes = salas.invitado[0] != 0;
    salas.sala_id = atoi(f[1]);
    salas.soy_anfitrion = !strcmp(f[2], "anfitrion");
    salas.sala_estado = f[3][0];
    salas.delay = SDL_clamp(atoi(f[4]), 1, 5);
    copiar(salas.anfitrion, f[5], sizeof salas.anfitrion);
    copiar(salas.invitado, f[6], sizeof salas.invitado);
    if (antes && !salas.invitado[0]) sin_rival();
    if (salas.estado == SL_LOBBY) salas.estado = SL_SALA;
    if (salas.soy_anfitrion && !antes && salas.invitado[0]) aviso(4000, "%s entered the room.", salas.invitado);
  } else if (!strcmp(f[0], "PAR")) {
    SDL_strlcpy(par.clave, f[1], sizeof par.clave);
    par.n_cand = 0;
    if (direccion(&par.cand[par.n_cand], f[2], f[3])) par.n_cand++;
    if (direccion(&par.cand[par.n_cand], f[4], f[5])) par.n_cand++;
    par.desde = (uint32_t)SDL_GetTicks();
    if (salas.enlace != EN_LISTO) { par.hay_real = 0; salas.enlace = EN_BUSCANDO; } // con camino, se sigue por el mismo
    perforar(par.desde);
  } else if (!strcmp(f[0], "IR")) {
    if (salas.estado != SL_SALA) return;
    salas.delay = SDL_clamp(atoi(f[1]), 1, 5);
    salas.estado = SL_EMPEZANDO;
    t_estado = (uint32_t)SDL_GetTicks();
    aviso(0, "Starting the match...");
  } else if (!strcmp(f[0], "AVISO")) {
    static const char *const COD[][2] = {
      {"cerrada", "The host closed the room."}, {"se_fue", "Your challenger left the room."},
      {"no_existe", "That room is gone."}, {"llena", "That room is already full."},
      {"no_se_puede", "The match cannot start yet."}, {"version", "The lobby server runs another version of the game."},
    };
    for (size_t i = 0; i < SDL_arraysize(COD); i++) if (!strcmp(f[1], COD[i][0])) aviso(5000, "%s", COD[i][1]);
  }
}

static void recibir(void) {
  for (;;) {
    int n = (int)recv(tcp, rx + rx_n, (int)sizeof rx - rx_n - 1, 0);
    if (n <= 0) {
      if (n == 0 || error_red() != 10035) {
        desconectar();
        salas.estado = SL_CONECTANDO;
        salas.fallo_conexion = 1;
        t_reintento = (uint32_t)SDL_GetTicks() + 3000;
        aviso(0, "Lost the connection to the lobby server.");
      }
      return;
    }
    rx_n += n;
    rx[rx_n] = 0;
    char *p = rx, *fin;
    while ((fin = strchr(p, '\n'))) {
      *fin = 0;
      if (fin > p && fin[-1] == '\r') fin[-1] = 0;
      linea(p);
      if (tcp == SOCK_INVALIDO) return;
      p = fin + 1;
    }
    rx_n -= (int)(p - rx);
    memmove(rx, p, (size_t)rx_n);
    if (rx_n >= (int)sizeof rx - 1) rx_n = 0; // una línea larguísima: se descarta
  }
}

// ---------------- búsqueda del servidor en la red local (sin servidor configurado) ----------------
static sock_t busca = SOCK_INVALIDO;
static uint32_t t_busca;
static void buscar(uint32_t ahora) {
  if (busca == SOCK_INVALIDO) {
    iniciar_winsock();
    busca = socket(AF_INET, SOCK_DGRAM, 0);
    if (busca == SOCK_INVALIDO) return;
    int si = 1;
    setsockopt(busca, SOL_SOCKET, SO_BROADCAST, (const char *)&si, sizeof si);
    no_bloqueante(busca);
  }
  if (ahora - t_busca > 1500) {
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(PUERTO_SALAS);
    a.sin_addr.s_addr = htonl(0xffffffffu);
    sendto(busca, "HKLB", 4, 0, (struct sockaddr *)&a, sizeof a);
    t_busca = ahora;
  }
  char m[32];
  struct sockaddr_in de;
  socklen_t l = sizeof de;
  int n = (int)recvfrom(busca, m, sizeof m - 1, 0, (struct sockaddr *)&de, &l);
  if (n < 5 || memcmp(m, "HKLS", 4)) return;
  m[n] = 0;
  char ip[INET_ADDRSTRLEN] = "";
  inet_ntop(AF_INET, &de.sin_addr, ip, sizeof ip);
  snprintf(salas.servidor, sizeof salas.servidor, "%s:%d", ip, atoi(m + 4));
  fprintf(stderr, "salas: servidor encontrado en %s\n", salas.servidor);
  guardar_config();
  cerrar_sock(busca);
  busca = SOCK_INVALIDO;
  conectar();
}

// ---------------- acciones del lobby ----------------
void salas_crear(void) { if (salas.estado == SL_LOBBY) mandar("CREAR"); }
void salas_unirse(int id) { if (salas.estado == SL_LOBBY) mandar("UNIRSE\t%d", id); }
void salas_salir(void) { if (salas.estado == SL_SALA) mandar("SALIR"); }
void salas_empezar(void) {
  if (salas.estado == SL_SALA && salas.soy_anfitrion && salas.invitado[0] && salas.enlace == EN_LISTO) mandar("EMPEZAR");
}
void salas_cambiar_delay(void) {
  if (salas.estado == SL_SALA && salas.soy_anfitrion) mandar("DELAY\t%d", salas.delay % 5 + 1);
}
void salas_configurar(const char *nombre, const char *servidor) {
  char n[17], s[64];
  limpiar_nombre(n, nombre, sizeof n);
  limpiar_nombre(s, servidor, sizeof s);
  int otro_servidor = strcmp(s, salas.servidor) != 0;
  if (n[0]) SDL_strlcpy(salas.nombre, n, sizeof salas.nombre);
  SDL_strlcpy(salas.servidor, s, sizeof salas.servidor);
  guardar_config();
  escribir_net_txt();
  if (red_obj) SDL_strlcpy(gstr(red_obj + 4), salas.nombre, 20); // el nombre que el juego le manda al rival
  if (otro_servidor || salas.estado == SL_BUSCANDO) {
    desconectar();
    if (busca != SOCK_INVALIDO) { cerrar_sock(busca); busca = SOCK_INVALIDO; }
    if (salas.servidor[0]) conectar();
    else salas.estado = SL_BUSCANDO;
  } else mandar("NOMBRE\t%s", salas.nombre);
}
void salas_volver(void) {
  if (pulso.resta || !escena || M32(escena + E_SUB) != 0) return;
  pulso.dik = tecla_b; pulso.resta = 6; // el B de la pantalla de red: vuelve al título
}

// ---------------- la pantalla de red original ----------------
void sala_escena_red(uint32_t e) {
  escena = e;
  escena_cuadro = cuadro;
  red_obj = M32(M32(e + 8) + 0x34);
}
static void pulsar(uint8_t dik) {
  if (pulso.resta) return;
  pulso.dik = dik;
  pulso.resta = 6;
}

static void apagar(void) {
  if (salas.sala_id && salas.estado != SL_JUGANDO) mandar("SALIR");
  desconectar();
  if (busca != SOCK_INVALIDO) { cerrar_sock(busca); busca = SOCK_INVALIDO; }
  salas.estado = SL_APAGADO;
  salas.visible = 0;
  salas.aviso[0] = 0;
  memset(teclas_sala, 0, sizeof teclas_sala);
  pulso.resta = 0;
}

// Lo que hace el modo red original por el jugador (solo con la pantalla quieta en su lista).
static void manejar_juego(uint32_t ahora) {
  uint32_t sub = M32(escena + E_SUB);
  int sala = salas.estado == SL_SALA || salas.estado == SL_EMPEZANDO;
  if (sala && salas.soy_anfitrion) { M32(escena + E_DELAY) = (uint32_t)salas.delay; M32(red_obj + R_DELAY) = (uint32_t)salas.delay; }
  if (sub == 0 && M32(escena + E_SERVIDOR) != (uint32_t)(sala && salas.soy_anfitrion)) { pulsar(tecla_c); return; }
  if (salas.estado != SL_EMPEZANDO) {
    // Un pedido que no es de la sala, o lo que quedó de un arranque que no salió: de vuelta a la lista.
    if (sub == 1 || sub == 3 || (sub >= 0xb && sub <= 0xd)) pulsar(tecla_b);
    return;
  }
  if (salas.soy_anfitrion) {
    if (sub == 3) pulsar(M32(red_obj + R_PEDIDO_DIR + 4) == htonl(IP_VIRTUAL) ? tecla_a : tecla_b);
  } else if (sub == 0 && M32(red_obj + R_ESTADO0) == 1) {
    M32(escena + E_PAGINA) = 0; M32(escena + E_FILA) = 0; // la única entrada
    pulsar(tecla_a);
  } else if (sub >= 0xb && sub <= 0xd) {
    pulsar(tecla_b); // rechazado u ocupado: se limpia el mensaje y se vuelve a pedir
  }
  if (ahora - t_estado > 25000) {
    aviso(6000, "The match did not start. Try again.");
    salas.estado = SL_SALA;
    mandar("VOLVER");
  }
}

void salas_cuadro(void) {
  uint32_t ahora = (uint32_t)SDL_GetTicks();
  if (++cuadro == 1) { cargar_config(); escribir_net_txt(); }
  if (pulso.resta) { teclas_sala[pulso.dik] = pulso.resta > 3; pulso.resta--; }
  if (salas.aviso_hasta && (int32_t)(ahora - salas.aviso_hasta) > 0) salas.aviso[0] = salas.aviso_hasta = 0;

  // Resultado de la conexión que hizo el hilo (aunque ya no se quiera, para liberarla).
  if (conexion && SDL_GetAtomicInt(&conexion->listo)) {
    Conexion *c = conexion;
    conexion = NULL;
    if (c->ok && salas.estado == SL_CONECTANDO && !strcmp(c->servidor, salas.servidor)) {
      tcp = c->s;
      dir_servidor = c->dir;
      dir_local = c->local;
      int si = 1;
      setsockopt(tcp, IPPROTO_TCP, TCP_NODELAY, (const char *)&si, sizeof si);
      mandar("HOLA\t%d\t%s", VERSION, salas.nombre);
      t_ping = ahora;
    } else {
      if (c->ok) cerrar_sock(c->s);
      if (salas.estado == SL_CONECTANDO) {
        salas.fallo_conexion = 1;
        t_reintento = ahora + 4000;
        aviso(0, "Could not reach the lobby server at %s.", salas.servidor);
      }
    }
    free(c);
  }

  int activa = escena && escena_cuadro + 1 >= cuadro;
  int quieta = activa && M32(escena + E_FASE) == 2;
  switch (salas.estado) {
    case SL_APAGADO: // tapa la pantalla desde que empieza a aparecer, y entra con su propio fundido desde negro
      if (!activa || M32(escena + E_FASE) == 8) return;
      salas.visible = 1;
      salas.opacidad = 0;
      sin_rival();
      if (salas.servidor[0]) conectar();
      else { salas.estado = SL_BUSCANDO; aviso(0, "Searching for a lobby server on this network..."); }
      break;
    case SL_JUGANDO:
      // De vuelta en la pantalla de red sin juego en red (el otro se fue, o se cortó): otra vez el lobby.
      if (activa && M32(escena + E_FASE) != 8 && !(red_obj && M32(red_obj + R_SESION))) { apagar(); return; }
      if (activa) { salas.opacidad = SDL_max(0, salas.opacidad - 1.0f / 30); return; } // sale hacia la selección
      salas.visible = 0;
      if (red_obj && M32(red_obj + R_SESION)) { t_estado = ahora; if (ahora - t_ping > 15000) { mandar("PING"); t_ping = ahora; } }
      if (tcp != SOCK_INVALIDO) recibir();
      if (ahora - t_estado > 2000) apagar(); // terminó el juego en red (volvió al título)
      return;
    default:
      if (!activa) { apagar(); return; } // salió de la pantalla de red
      if (M32(escena + E_FASE) == 8) { // se va: al título (B) o a la pelea
        if (salas.estado == SL_EMPEZANDO && M32(escena + E_SIGUIENTE) == 0x1c) {
          salas.estado = SL_JUGANDO;
          salas.aviso[0] = 0;
          t_estado = ahora;
          fprintf(stderr, "salas: empieza la pelea en red (%s contra %s)\n", salas.anfitrion, salas.invitado);
        }
        salas.opacidad = SDL_max(0, salas.opacidad - 1.0f / 30);
        return;
      }
      salas.opacidad = SDL_min(1, salas.opacidad + 1.0f / 15);
      break;
  }

  if (salas.estado == SL_BUSCANDO) buscar(ahora);
  if (salas.estado == SL_CONECTANDO && !conexion && tcp == SOCK_INVALIDO && salas.fallo_conexion && (int32_t)(ahora - t_reintento) > 0) conectar();
  if (tcp != SOCK_INVALIDO) {
    recibir();
    if (tcp != SOCK_INVALIDO && ahora - t_ping > 15000) { mandar("PING"); t_ping = ahora; }
  }
  // El servidor tiene que ver al socket del juego desde afuera (para el rival) y saber su IP local.
  sock_t s;
  int puerto;
  if (tcp != SOCK_INVALIDO && token[0] && red_socket_juego(&s, &puerto) && ahora - t_registro > (registrado ? 15000u : 1000u)) {
    char m[24];
    memcpy(m, "HKLR", 4);
    memcpy(m + 4, token, 16);
    struct sockaddr_in a = dir_servidor;
    udp(m, 20, &a);
    if (!t_registro || puerto != salas.puerto) {
      char ip[INET_ADDRSTRLEN] = "";
      if (dir_local.sin_family) inet_ntop(AF_INET, &dir_local.sin_addr, ip, sizeof ip);
      if (ip[0]) mandar("LOCAL\t%s\t%d", ip, puerto);
    }
    salas.puerto = puerto;
    t_registro = ahora;
  }
  // Con rival: paquetes al otro hasta que llegue la respuesta (cada 100 ms) y después uno por segundo.
  if (salas.sala_id && salas.enlace != EN_NADA && par.n_cand && ahora - par.ultimo_envio > (salas.enlace == EN_BUSCANDO ? 100u : 1000u)) {
    if (salas.enlace == EN_BUSCANDO && ahora - par.desde > 10000) salas.enlace = EN_FALLO; // el lobby lo dice; se sigue probando
    perforar(ahora);
  }
  if (quieta) manejar_juego(ahora);
}
