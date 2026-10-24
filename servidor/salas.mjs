// Servidor de salas para el juego en red de Hinokakera CE (el port de este repo, en Android o en la PC).
// Solo arma las salas y presenta a los jugadores: a cada uno le pasa la dirección del otro (la pública, como la ve
// este servidor, y la de su red local) y ellos se conectan directo, IP a IP, con el protocolo UDP del propio juego.
// Ninguna pelea pasa por acá.
//
// Uso: node servidor/salas.mjs [puerto]        (por defecto 10995, TCP y UDP; sin dependencias)
//
// Protocolo: TCP, un mensaje por línea, campos separados por tabulador.
//   cliente -> servidor: HOLA versión nombre | NOMBRE nombre | LOCAL ip puerto | CREAR | UNIRSE sala | SALIR |
//                        DELAY n | EMPEZAR | VOLVER (no arrancó: la sala vuelve a estar lista) | PING
//   servidor -> cliente: BIENVENIDO id token | LISTA conectados n, y n líneas S id estado delay anfitrión invitado |
//                        SALA id rol estado delay anfitrión invitado (o "SALA -") |
//                        PAR clave ip puerto ip_local puerto_local (la otra punta) | IR delay | AVISO código | PONG
//   UDP: "HKLR" + token, mandado desde el socket del juego -> "HKLV ip:puerto" (así el servidor ve la dirección
//        pública que el NAT le dio a ese socket); "HKLB" (difusión en la red local) -> "HKLS puerto".
import net from 'node:net';
import dgram from 'node:dgram';
import { randomBytes } from 'node:crypto';

export const VERSION = 1;
const MAX_LINEA = 512, MAX_CLIENTES = 1000, MAX_POR_IP = 16, OCIOSO_MS = 45000;

const limpiarNombre = (s) => (s ?? '').replace(/[^\x20-\x7e]/g, '').replace(/\t/g, ' ').trim().slice(0, 16) || 'Player';
const ipv4 = (s) => /^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})$/.test(s) && s.split('.').every((n) => +n <= 255);
const sinPrefijo = (ip) => (ip ?? '').replace(/^::ffff:/, '');

export function iniciar(puerto = 10995, { log = console.log } = {}) {
  const clientes = new Map(), salas = new Map(), porToken = new Map(), porIp = new Map();
  let ultimoCliente = 0, ultimaSala = 0;

  const enviar = (c, ...campos) => { if (!c.sock.destroyed) c.sock.write(campos.join('\t') + '\n'); };
  const lista = () => {
    const filas = [...salas.values()].map((s) => ['S', s.id, s.estado, s.delay, s.anfitrion.nombre, s.invitado?.nombre ?? '']);
    const dentro = [...clientes.values()].filter((x) => x.nombre);
    for (const c of dentro) {
      enviar(c, 'LISTA', dentro.length, filas.length);
      for (const f of filas) enviar(c, ...f);
    }
  };
  const avisarSala = (s) => {
    for (const [c, rol] of [[s.anfitrion, 'anfitrion'], [s.invitado, 'invitado']])
      if (c) enviar(c, 'SALA', s.id, rol, s.estado, s.delay, s.anfitrion.nombre, s.invitado?.nombre ?? '');
  };
  // Cuando la sala está completa y los dos ya mandaron su UDP, cada uno recibe cómo llegar al otro.
  const par = (s) => {
    const a = s.anfitrion, b = s.invitado;
    if (!b || !a.publico || !b.publico) return;
    for (const [yo, otro] of [[a, b], [b, a]])
      enviar(yo, 'PAR', s.clave, otro.publico.ip, otro.publico.puerto, otro.local?.ip ?? '', otro.local?.puerto ?? 0);
  };
  const salir = (c, motivo) => {
    const s = c.sala;
    if (!s) return;
    c.sala = null;
    if (s.anfitrion === c) { // el anfitrión se lleva la sala
      salas.delete(s.id);
      if (s.invitado) { s.invitado.sala = null; enviar(s.invitado, 'SALA', '-'); enviar(s.invitado, 'AVISO', motivo ?? 'cerrada'); }
      log(`sala ${s.id} cerrada`);
    } else {
      s.invitado = null; s.estado = 'espera';
      enviar(c, 'SALA', '-');
      avisarSala(s); enviar(s.anfitrion, 'AVISO', 'se_fue');
    }
    lista();
  };

  const ordenes = {
    NOMBRE(c, [nombre]) { c.nombre = limpiarNombre(nombre); if (c.sala) avisarSala(c.sala); lista(); },
    LOCAL(c, [ip, p]) { if (ipv4(ip) && +p > 0 && +p < 65536) { c.local = { ip, puerto: +p }; if (c.sala) par(c.sala); } },
    CREAR(c) {
      if (c.sala) return enviar(c, 'AVISO', 'ya_en_sala');
      const s = { id: ++ultimaSala, anfitrion: c, invitado: null, estado: 'espera', delay: 1, clave: '' };
      salas.set(s.id, s); c.sala = s;
      log(`sala ${s.id} creada por ${c.nombre}`);
      avisarSala(s); lista();
    },
    UNIRSE(c, [id]) {
      const s = salas.get(+id);
      if (c.sala) return enviar(c, 'AVISO', 'ya_en_sala');
      if (!s) return enviar(c, 'AVISO', 'no_existe');
      if (s.estado !== 'espera') return enviar(c, 'AVISO', 'llena');
      s.invitado = c; s.estado = 'llena'; s.clave = randomBytes(8).toString('hex'); c.sala = s;
      log(`${c.nombre} entró a la sala ${s.id} de ${s.anfitrion.nombre}`);
      avisarSala(s); par(s); lista();
    },
    SALIR(c) { salir(c); },
    DELAY(c, [n]) {
      const s = c.sala;
      if (!s || s.anfitrion !== c || !(+n >= 1 && +n <= 5)) return;
      s.delay = +n; avisarSala(s); lista();
    },
    EMPEZAR(c) {
      const s = c.sala;
      if (!s || s.anfitrion !== c || s.estado !== 'llena') return enviar(c, 'AVISO', 'no_se_puede');
      s.estado = 'jugando';
      log(`sala ${s.id}: ${s.anfitrion.nombre} contra ${s.invitado.nombre}`);
      enviar(s.anfitrion, 'IR', s.delay); enviar(s.invitado, 'IR', s.delay);
      avisarSala(s); lista();
    },
    VOLVER(c) {
      const s = c.sala;
      if (!s || s.estado !== 'jugando') return;
      s.estado = s.invitado ? 'llena' : 'espera'; avisarSala(s); lista();
    },
    PING(c) { enviar(c, 'PONG'); },
  };

  const tcp = net.createServer((sock) => {
    const ip = sinPrefijo(sock.remoteAddress);
    if (clientes.size >= MAX_CLIENTES || (porIp.get(ip) ?? 0) >= MAX_POR_IP) return sock.destroy();
    porIp.set(ip, (porIp.get(ip) ?? 0) + 1);
    const c = { id: ++ultimoCliente, sock, ip, buf: '', nombre: '', token: randomBytes(8).toString('hex'), local: null, publico: null, sala: null, visto: Date.now() };
    clientes.set(c.id, c); porToken.set(c.token, c);
    sock.setEncoding('latin1');
    sock.setNoDelay(true);
    sock.setKeepAlive(true, 20000);
    sock.on('data', (d) => {
      c.visto = Date.now();
      c.buf += d;
      if (c.buf.length > MAX_LINEA * 8) return sock.destroy();
      let i;
      while ((i = c.buf.indexOf('\n')) >= 0) {
        const linea = c.buf.slice(0, i).replace(/\r$/, ''); c.buf = c.buf.slice(i + 1);
        if (linea.length > MAX_LINEA) return sock.destroy();
        const [orden, ...campos] = linea.split('\t');
        if (!c.nombre) { // lo primero tiene que ser HOLA
          if (orden !== 'HOLA') return sock.destroy();
          if (+campos[0] !== VERSION) { enviar(c, 'AVISO', 'version'); return sock.end(); }
          c.nombre = limpiarNombre(campos[1]);
          log(`entró ${c.nombre} (${ip})`);
          enviar(c, 'BIENVENIDO', c.id, c.token);
          lista();
          continue;
        }
        ordenes[orden]?.(c, campos);
      }
    });
    const fin = () => {
      if (!clientes.has(c.id)) return;
      clientes.delete(c.id); porToken.delete(c.token);
      porIp.set(ip, porIp.get(ip) - 1); if (!porIp.get(ip)) porIp.delete(ip);
      salir(c, 'cerrada');
      if (c.nombre) { log(`salió ${c.nombre}`); lista(); }
    };
    sock.on('close', fin);
    sock.on('error', () => sock.destroy());
  });

  const udp = dgram.createSocket('udp4');
  udp.on('message', (m, r) => {
    const t = m.toString('latin1', 0, Math.min(m.length, 64));
    if (t.startsWith('HKLB')) return udp.send(`HKLS${tcp.address().port}`, r.port, r.address);
    if (!t.startsWith('HKLR')) return;
    const c = porToken.get(t.slice(4, 20));
    if (!c) return;
    c.visto = Date.now();
    const nuevo = !c.publico || c.publico.ip !== r.address || c.publico.puerto !== r.port;
    c.publico = { ip: r.address, puerto: r.port };
    udp.send(`HKLV${r.address}:${r.port}`, r.port, r.address);
    if (nuevo && c.sala) par(c.sala);
  });
  udp.on('error', (e) => log(`udp: ${e.message}`));

  const ocioso = setInterval(() => {
    const ahora = Date.now();
    for (const c of clientes.values()) if (ahora - c.visto > OCIOSO_MS) c.sock.destroy();
  }, 5000);

  return new Promise((ok, mal) => {
    tcp.once('error', mal);
    tcp.listen(puerto, () => {
      udp.bind(tcp.address().port, () => {
        log(`salas: escuchando en el puerto ${tcp.address().port} (TCP y UDP)`);
        ok({
          puerto: tcp.address().port,
          cerrar: () => new Promise((fin) => { clearInterval(ocioso); for (const c of clientes.values()) c.sock.destroy(); udp.close(); tcp.close(fin); }),
        });
      });
    });
  });
}

if (import.meta.url === `file:///${process.argv[1]?.replaceAll('\\', '/')}` || import.meta.url === `file://${process.argv[1]}`) {
  const p = +(process.argv[2] || process.env.PUERTO || 10995);
  const hora = () => new Date().toTimeString().slice(0, 8);
  iniciar(p, { log: (s) => console.log(`${hora()} ${s}`) }).catch((e) => { console.error(`no pude abrir el puerto ${p}: ${e.message}`); process.exit(1); });
}
