// Prueba del servidor de salas con clientes de mentira (TCP + UDP), como los del juego: node servidor/prueba.mjs
import net from 'node:net';
import dgram from 'node:dgram';
import assert from 'node:assert/strict';
import { iniciar, VERSION } from './salas.mjs';

const srv = await iniciar(0, { log: () => {} });

function cliente(nombre) {
  const sock = net.connect(srv.puerto, '127.0.0.1'), udp = dgram.createSocket('udp4'), lineas = [], esperas = [];
  let buf = '';
  sock.setEncoding('latin1');
  sock.on('data', (d) => {
    buf += d;
    let i;
    while ((i = buf.indexOf('\n')) >= 0) { lineas.push(buf.slice(0, i).split('\t')); buf = buf.slice(i + 1); }
    for (const e of [...esperas]) e();
  });
  const c = {
    sock, udp, nombre,
    mandar: (...campos) => sock.write(campos.join('\t') + '\n'),
    // Espera la próxima línea que empiece con esa orden (y que cumpla la condición) y la saca de la cola.
    esperar: (orden, cond = () => true) => new Promise((ok, mal) => {
      const t = setTimeout(() => mal(new Error(`${nombre}: no llegó ${orden}`)), 2000);
      const mirar = () => {
        const i = lineas.findIndex((l) => l[0] === orden && cond(l));
        if (i < 0) return;
        clearTimeout(t); esperas.splice(esperas.indexOf(mirar), 1);
        ok(lineas.splice(i, 1)[0]);
      };
      esperas.push(mirar); mirar();
    }),
    udpPedir: (texto) => new Promise((ok) => { udp.once('message', (m) => ok(m.toString('latin1'))); udp.send(texto, srv.puerto, '127.0.0.1'); }),
  };
  return new Promise((ok) => udp.bind(0, '127.0.0.1', () => ok(c)));
}

const a = await cliente('Agus'), b = await cliente('Juan');
a.mandar('HOLA', VERSION, 'Agus');
const [, idA, tokenA] = await a.esperar('BIENVENIDO');
assert.match(tokenA, /^[0-9a-f]{16}$/);
b.mandar('HOLA', VERSION, 'Ju\x01an\twith tab and a very long name');
const [, , tokenB] = await b.esperar('BIENVENIDO');
assert.equal((await b.esperar('LISTA', (l) => l[1] === '2'))[2], '0');

// Una sala con nombre y clave: en la lista sale el nombre y que es privada, nunca la clave.
a.mandar('CREAR', 'Sala de Agus', 'secreto');
const salaA = await a.esperar('SALA');
assert.deepEqual(salaA.slice(2), ['anfitrion', 'espera', '1', 'Agus', '', 'Sala de Agus', '1']);
const fila = await b.esperar('S');
assert.deepEqual(fila.slice(2), ['espera', '1', 'Agus', '', 'Sala de Agus', '1']);

// El UDP desde el "socket del juego": el servidor contesta con la dirección que ve.
const vA = await a.udpPedir(`HKLR${tokenA}`), vB = await b.udpPedir(`HKLR${tokenB}`);
assert.equal(vA, `HKLV127.0.0.1:${a.udp.address().port}`);
assert.equal(vB, `HKLV127.0.0.1:${b.udp.address().port}`);
a.mandar('LOCAL', '192.168.1.40', '10996');
b.mandar('LOCAL', '192.168.1.59', '10996');
assert.equal(await b.udpPedir('HKLB'), `HKLS${srv.puerto}`);

// Clave equivocada: afuera. Bien: adentro, con la dirección del otro.
b.mandar('UNIRSE', salaA[1], 'otra');
assert.equal((await b.esperar('AVISO'))[1], 'clave');
b.mandar('UNIRSE', salaA[1], 'secreto');
const enB = await b.esperar('SALA');
assert.deepEqual(enB.slice(2), ['invitado', 'llena', '1', 'Agus', 'Juan', 'Sala de Agus', '1']); // sin el control y cortado en el tabulador
const parA = await a.esperar('PAR'), parB = await b.esperar('PAR');
assert.equal(parA[1], parB[1]);
assert.deepEqual(parA.slice(2), ['127.0.0.1', String(b.udp.address().port), '192.168.1.59', '10996']);
assert.deepEqual(parB.slice(2), ['127.0.0.1', String(a.udp.address().port), '192.168.1.40', '10996']);

// El anfitrión lo echa; vuelve a entrar.
a.mandar('ECHAR');
assert.deepEqual(await b.esperar('SALA', (l) => l[1] === '-'), ['SALA', '-']);
assert.equal((await b.esperar('AVISO'))[1], 'echado');
assert.equal((await a.esperar('AVISO'))[1], 'lo_echaste');
assert.equal((await a.esperar('SALA', (l) => l[3] === 'espera'))[6], '');
b.mandar('UNIRSE', salaA[1], 'secreto');
await b.esperar('SALA', (l) => l[3] === 'llena');

b.mandar('DELAY', '4'); // solo el anfitrión cambia el delay
a.mandar('DELAY', '3');
assert.equal((await b.esperar('SALA', (l) => l[4] === '3'))[4], '3');
b.mandar('EMPEZAR');
assert.equal((await b.esperar('AVISO'))[1], 'no_se_puede');
a.mandar('EMPEZAR');
assert.deepEqual(await a.esperar('IR'), ['IR', '3']);
assert.deepEqual(await b.esperar('IR'), ['IR', '3']);
assert.equal((await b.esperar('S', (l) => l[2] === 'jugando'))[1], salaA[1]);

// Si el anfitrión se va, la sala se cierra y el otro queda afuera.
a.sock.destroy();
assert.deepEqual(await b.esperar('SALA', (l) => l[1] === '-'), ['SALA', '-']);
assert.equal((await b.esperar('AVISO'))[1], 'cerrada');
assert.deepEqual(await b.esperar('LISTA', (l) => l[1] === '1'), ['LISTA', '1', '0']);

// Sin clave, con el nombre por defecto; cinco claves mal seguidas y se espera un minuto.
b.mandar('CREAR', '', '');
assert.equal((await b.esperar('SALA', (l) => l[7] === "Juan's room"))[8], '0');
const c = await cliente('x');
c.mandar('HOLA', VERSION, 'x');
await c.esperar('BIENVENIDO');
b.mandar('SALIR');
b.mandar('CREAR', 'Cerrada', 'clave');
const cerrada = await b.esperar('SALA', (l) => l[7] === 'Cerrada');
for (let i = 0; i < 5; i++) { c.mandar('UNIRSE', cerrada[1], `mal${i}`); assert.equal((await c.esperar('AVISO'))[1], 'clave'); }
c.mandar('UNIRSE', cerrada[1], 'clave');
assert.equal((await c.esperar('AVISO'))[1], 'espera');

// Basura: una línea enorme corta la conexión, sin tirar el servidor.
await new Promise((ok) => { c.sock.on('close', ok); c.sock.write('A'.repeat(9000)); });
b.mandar('PING');
await b.esperar('PONG');

for (const x of [b, c]) { x.sock.destroy(); x.udp.close(); }
a.udp.close();
await srv.cerrar();
console.log(`servidor de salas: todo bien (sala ${salaA[1]}, cliente ${idA})`);
