# Estado (bitácora)

## 2026-10-23 — el exe original corre traducido y la pantalla de título sale igual

**Hecho**
- Traductor x86 → C (`recomp/`): 9.306 funciones, 424 mil instrucciones, 0 sin soporte que se ejecuten.
  Descubre funciones por llamadas, vtables e inmediatos (punto fijo), detecta funciones sin retorno,
  propaga banderas entre funciones (rutinas en ensamblador del CRT) y reemplaza `memcpy/memmove` de VC6
  por nativos (`analisis/reemplazos.txt`). Compila con clang 19 en ~15 s (16 núcleos).
- Runtime (`runtime/`): 4 GB de memoria del invitado, x87 sobre double con precisión de 24/53 bits según la
  palabra de control (Direct3D la deja en 24), FNSAVE/FRSTOR, hilos, TEB, despacho indirecto.
- Windows simulado (`win32/`): kernel32 (memoria, archivos con carpeta de datos de solo lectura + carpeta de
  escritura, hilos y sincronización, TLS, recursos, CP932 con la tabla real de Windows), user32 (ventanas
  SDL, cola de mensajes → WndProc del juego), gdi32 (glifos: GDI real en Windows), winmm, DirectInput 8,
  DirectSound 8 (mezclador propio), Winsock (UDP real para el netplay).
- Direct3D 9 sobre OpenGL ES 3 (`d3d/`): función fija generada como GLSL, vs_1_x traducidos, FBO con la fila 0
  arriba, medio píxel de D3D9, texturas desde el BMP en memoria que arma el juego (D3DX).
- **Verificación píxel a píxel**: `tools/c/d3d9_espia.c` es un d3d9.dll que se pone en una copia del juego
  original y vuelca el backbuffer de los cuadros pedidos. Título, cuadros 30-400: **479.892 de 480.000 píxeles
  idénticos (99,978 %)**; los otros 108 difieren en 1 nivel, todos dentro del texto "ver. 1.01" (redondeo del
  blending de la GPU sobre la textura A4R4G4B4 del texto).

**Cómo correr**
- `tools/correr.sh` compila y corre (datos del original de solo lectura, escritura en `run/escritura`).
  `HINO_OCULTA=1` sin ventana, `HINO_VENTANA=1` sin pantalla completa, `HINO_CAPTURAS=30,120` guarda cuadros.
- Depuración: `RASTRO=1 tools/correr.sh --rastro` (pila de funciones del invitado y verificación de cada `ret`);
  `HINO_TRAZA=<va>` al traducir traza instrucción por instrucción esa función.
- Original con espía: copia en `Box/_scratch/hino/original` (los .dat grandes son hardlinks, config en ventana),
  `d3d9.dll` espía + `capturar_original.exe`.

**Errores encontrados y arreglados en el camino**
- Funciones con bloques compartidos ubicados antes de la entrada empezaban por el bloque equivocado.
- `call [IAT]` es IndirectCall en iced (las sin retorno no se detectaban).
- Exportaciones stdcall de 32 bits salen decoradas: el espía necesita `/EXPORT:Nombre=_Nombre@n`.

## 2026-10-23 (2) — menús, selección y pelea idénticos al original

- Guion de teclas por cuadro (`HINO_TECLAS="cuadro:dik:1|0,..."`) en el port y en el original
  (`tools/c/dinput8_espia.c`, que además pide DirectInput en segundo plano porque la ventana lanzada no tiene
  el foco). Con el mismo guion los dos recorren título → menú → Arcade → selección → pelea.
- Comparación de backbuffers en los mismos cuadros: selección de personaje 95,4 % idéntico (máx ±2, en el
  fondo animado mezclado), transición VS 99 %, **pelea del cuadro 700 al 2500: 94-96,5 % idéntico**; la única
  diferencia grande es el contador de FPS del HUD (el original corre a 33 FPS en esta notebook, el port a 60).
  La pelea (IA, combos, daño) avanza igual cuadro por cuadro: la lógica recompilada es fiel.
- Arreglos: `D3DSAMP_MIPMAPLODBIAS` (el juego usa -0,5; ahora va como sesgo en `texture()`), `ExitThread` de
  hilos cuyo handle ya se cerró terminaba el proceso, un `record.dat` corrupto de una corrida vieja.
- Mipmaps de D3DX verificados contra la dll real (`tools/c/prueba_d3dx.c`): caja 2x2 con `(suma+2)>>2`, 0 diferencias.
- Depuración nueva: manejador de accesos inválidos (dirección del invitado + RVA para llvm-symbolizer),
  vigilante de cuadros con la pila de cada hilo, `HINO_LOG_ESTADOS` (estados de render la primera vez).

## 2026-10-23 (3) — Android: APK jugable, pantalla completa en ancho y controles táctiles

**Hecho**
- **APK** (`apk/HinokakeraCE-android.apk`, release, arm64-v8a + x86_64, 750 MB con el juego adentro, firmado con
  `android/hino.keystore`, clave propia del proyecto: las actualizaciones se instalan encima sin perder partidas).
  `node tools/apk.mjs [--abis x86_64] [--datos] [--release]` usa el entorno compartido de
  `Box/_scratch/pacmini-android` (JDK 21, SDK con NDK 28.2 y CMake 3.31.6, Gradle 8.11.1 y su caché con AGP 8.9.2);
  todo lo generado va a `Box/_scratch/hino-android`. Los datos van como assets sin comprimir (`.dat`, `.ogg`),
  hardlinks armados por `tools/datos.mjs`; `glifos.bin` e `imagen.bin` también van en assets.
- Android: el juego corre en un hilo propio con 64 MB de pila (el de SDL trae ~1 MB), stderr va a logcat
  (etiqueta `hino`), escritura en el almacenamiento interno, Atrás = Pausa, pantalla completa siempre.
  `-ffp-contract=off` (sin FMA en ARM) y `-fsigned-char`, como en x86.
- **Verificación en Android** contra el backbuffer del original: título y menú 100,000 % idénticos (x86_64 y
  arm64 bajo traducción ARM del emulador); pelea arm64 del cuadro 700 al 2000: 98,4–99,7 % (lo distinto es el
  contador de FPS); la pelea va cuadro a cuadro igual (mismas vidas y barras). Las llamas varían con el reloj real
  (también entre dos corridas del original: el port de PC da 79 % en el cuadro 2500 contra el mismo original).
- **Modo ancho** (`d3d/dibujo.c`, pedido explícito: nada de bordes negros): el backbuffer real mide la pantalla a
  600 de alto (1332×600 en 20:9) y el juego, que sigue viendo 800×600, se dibuja centrado multiplicando la x del clip
  por 800/ancho: lo 2D queda a 1:1 y lo 3D muestra más escenario (Hor+). Todos los shaders del juego hacen
  `m4x4 oPos, v0, c0` o `mov oPos, v0`, así que la CPU sabe dónde cae cada dibujo: lo 2D que ocupa todo el ancho se
  extiende (fondos: borde reflejado y vuelto a reflejar; bandas del HUD/menús: columna del borde estirada), salvo
  capas casi transparentes en los bordes (el texto del modo historia), según un mapa de alfa 64×64 por textura; el
  resto de lo 2D se recorta a la zona del juego. La franja central queda igual que en 4:3. `HINO_ANCHO=0` lo apaga,
  `HINO_ANCHO=1332` lo fuerza en una ventana de la PC.
- **Controles táctiles** (`d3d/tactil.c`): cruz de 8 direcciones, A B C D y START (pausa), dibujados con la misma
  fuente MS Gothic del juego; se acomodan en cada cuadro por tamaño físico (cm según la densidad), zona segura
  (cámara) y lugar libre; con la imagen ancha van encima, semitransparentes; con mando conectado no se dibujan.
  Escriben `teclas_virtuales[]` con las teclas del jugador 1 de `config.dat`; un toque corto dura al menos dos
  cuadros. Tocar la imagen es el mouse (el modo historia lee los clics con `GetDeviceData` de DirectInput, que ahora
  tiene buffer de eventos) y el mouse de verdad también se reescala a coordenadas del juego.
- Presentación: escala "sharp bilinear" (entero sin filtrar y después lineal) al tamaño de la pantalla.
- Ícono: el del exe (`HIKAKE_ICON`, 32×32, ubicado con REA `inspect_pe_resources`), `tools/icono.mjs`.

**Probado en el emulador** (AVD `celu` de subibaja, 2400×1080): título, menú (rueda de modos), Arcade, selección,
pelea, pausa con Atrás, modo historia avanzando con toques; ~50 fps (el emulador limita por la GPU traducida: el hilo
del juego usa ~19 % de CPU).

**Cómo probar en el emulador**: `node ../subibaja/tools/emu.mjs start`, `node tools/apk.mjs --abis x86_64 --datos`,
`node tools/android.mjs instalar`, `node tools/android.mjs entorno "HINO_ESCRITURA={escritura}" "HINO_CAPTURAS=30"`
(variables de prueba en `entorno.txt` del almacenamiento externo), `node tools/android.mjs correr 15`,
`node tools/android.mjs bajar <dir>`. Ojo: lo que sube `adb push` a `Android/data/<pkg>` queda con dueño `shell` y
la app no puede entrar a sus subcarpetas: los datos de prueba van siempre dentro del APK.

**Depuración**: `HINO_LOG_DIBUJOS=cuadros` (cada llamada: 2D/3D, caja en pantalla, textura, destino),
`HINO_LOG_VS=1` (fuente de los vertex shaders), `HINO_LOG_ARCHIVOS=1` (aperturas de archivos).

**Sin probar** (en ese momento; el teléfono real quedó probado en la entrada 4): juego en red, multitáctil real (el emulador solo
manda un dedo), todas las pantallas del modo historia y extras.

**Ideas**
- HUD de la pelea repartido a los bordes (hoy queda centrado con el marco extendido).
- Fondos de la historia: el reflejo se nota en algunas imágenes; un desenfoque de los costados lo disimularía.

## 2026-10-23 (4) — teléfono real: candado del invitado y pantalla al tamaño real

**Hecho**
- Probado en un **Samsung Galaxy A52** (SM-A525M, Android 14, Adreno 618, 2400×1080) por adb inalámbrico: 60 fps.
- **Cuelgue solo en ARM real**: SIGSEGV en el hilo de la música (decodificador Vorbis del juego, `f_0040fc30` →
  `f_004c6350`), que no salía en la PC ni en el emulador ni frenando lecturas (`HINO_LENTO=ms` en
  `k32_archivos.c` demora `ReadFile` en los hilos secundarios). El juego cuenta con el orden de memoria de x86 (TSO)
  y en ARM un hilo leía datos a medio escribir. Arreglo: **candado del invitado** (`runtime/hilo.c`, turnos con
  mutex y condición): corre un solo hilo del juego a la vez; se suelta alrededor de `Sleep`, esperas, secciones
  críticas ocupadas, `GetMessage`, `recvfrom` y el `SwapWindow`, y `PeekMessage` cede el turno. En el A52 aguantó
  casi 8 minutos seguidos (27.900 cuadros) sin cortes; en la PC los cuadros de la pelea salen iguales que antes.
- La ventana arrancaba más chica (2186×1017) antes de pasar a 2400×1080 y quedaban bordes negros: `ancho_ajustar`
  rearma el backbuffer después de cada `SwapWindow` si cambió el tamaño.
- Repo público `github.com/agustinyarrus/hinokakera-android`: código, documentación y capturas; sin el juego, sin
  su código traducido (`gen/`) y sin el APK, que lo llevan adentro.

## 2026-10-23 (5) — Outskirt Hill de noche (estilo Moonlit Wilderness)

**Hecho**
- `node tools/escenario/moonlit.mjs [--png carpeta]` arma `_scratch/hino-android/mods/vsobj2.dat`: el parche del
  juego más 20 archivos nuevos. El juego busca primero en `vsobj2.dat` (tabla unida, gana el primero), así que el
  escenario cambia sin tocar el exe; `tools/datos.mjs` toma lo de `mods/` antes que los archivos del juego.
- Formatos (con REA): `dat/field.dat` (cargador en 0x4984b0, `tools/escenario/campo.mjs`): por escenario, la lista
  de modelos con tipo de vértice (0 xyz+normal, 1 +uv, 2 xyz, 3 xyz+uv; los 0/1 se iluminan), Z, escribe Z, mezcla
  (0x403ad0: 2 alfa, 3 aditiva) y prueba alfa; efecto de partículas (1 polvo del desierto, 3 motas blancas), luz
  difusa/ambiente/dirección y viento (mueve ropa y pelo). El modelo 0 es el piso: su textura se corre con la pelea.
  Todos los modelos siguen el centro de la pelea: el escenario es un diorama alrededor de los peleadores y al
  arrancar la ronda la cámara mira a +Z, ~5,7 unidades atrás, con ~30° de FOV vertical. En `nmd_`, los floats
  0–3 del material son el color (constante c4 del shader), el 4 activa una pasada especular (0 = no), y `ini`/`n`
  cuentan triángulos. `BMA_` es ARGB sin paleta.
- Lo que tiene: domo de cielo (panorama de 4096×1024 en dos mitades, la elevación no lineal para que la franja que
  ve la cámara tenga resolución), luna con halo (mezcla alfa), piso y lomas pasados a noche desde los originales,
  montes con filo de luna y ruinas lejanas en vez de la ciudad, árboles secos (carteles y 3D), cortaderas (las
  tiras de pasto del original, 3 veces más altas, y 200 matas entre r 13,2 y 17,5 en tres tonos, dibujadas de
  afuera hacia adentro porque no escriben Z), ruinas góticas en una loma (arcadas de arco apuntado, torre, ala
  caída, arco suelto y escombros; iluminadas), monolito vencido, luz de luna azulada y el efecto 3 como pelusa.
- Miniatura de la selección de escenario (cuarta franja de `tex_sys42`): la renderiza `tools/escenario/vista.mjs`,
  un rasterizador por software con las mismas banderas del juego; `tools/escenario/ver.mjs <n> <png> [--sin m]
  [--sintex t]` muestra cualquier escenario sin correr el juego. `npm test` prueba las herramientas.

**Trampas**
- El corte de orejas se trababa con puntos repetidos o alineados (los escalones del borde roto) y dejaba caras sin
  tapar: `limpiar()` antes de triangular.
- Dos tiras en la misma textura se contaminan por el filtrado: el pie de una aparecía como rayitas arriba de la
  otra. Van con margen transparente.
- Los píxeles casi transparentes de los montes originales eran blancos y no se veían; pasados a azul oscuro eran
  rayitas en el cielo: se corta el alfa bajo.
- Los períodos del ruido tienen que ser enteros o el panorama no empalma.

## 2026-10-23 (6) — Sunlit Shore: un escenario nuevo de playa

**Hecho**
- `node tools/escenario/playa.mjs [--png carpeta]` arma primero Outskirt Hill de noche (`moonlit.mjs`) y le suma la
  playa al mismo `mods/vsobj2.dat`: `field.dat` con 10 escenarios (la playa es el 9), la rueda con `tex_sys24`
  (nombres, 256x512) y `tex_sys42` (miniaturas, 512x1024) nuevas, 7 modelos (`mod_f100`–`f106`) y 12 texturas
  (`tex_f100`–`f111`). Sale igual byte a byte en cada corrida.
- La rueda de escenarios (la escena de selección: 0x48f070 la arma, 0x48fb10 la mueve, 0x490af0 la dibuja):
  `[esi+100h]` lugar, `[esi+10Ch]` cantidad (8), `[esi+108h]` animación. Lugar = escenario de 0 a 6; el lugar 7 era
  Random y al elegirlo pasaba a 9, la marca de "al azar". El que arma la pelea (0x49540a) aceptaba 0–8 y con 9 o
  negativo hacía `rand16 % 7` (también en arcade, 0x495f55, y en COM contra COM, 0x495fb5). La elección se guarda en
  la partida (`[+0F00h]`, que al cargar se valida de 0 a 9 en 0x49a4e7) y de ahí sale el lugar inicial (tope 7).
- `analisis/parches.txt`, que `recomp/parches.mjs` aplica al traducir (verifica los bytes originales): 34
  instrucciones y una constante, más 5 fragmentos de C. Nueve lugares (7 = playa, 8 = Random); Random elegido = -1
  y la marca guardada = 10; la pelea acepta 0–9 y el lugar 7 es el escenario 9; el azar es `% 8` con 7 → 9. Las
  miniaturas dividen la v por 1024 y los nombres por 512 con constantes que ya estaban (1/1024 en 54d6a0, 1/512 en
  54c134) y Random dejó de ser un caso aparte (`cmp` con 7Fh, nunca): usa su franja como los demás. Las franjas 9 y
  15 de la miniatura repiten la 0 y Random porque el anterior y el siguiente se dibujan sin módulo (-1 y 9).
- Nada más depende del número de escenario: los efectos solo preguntan si es la nieve (2), así que la playa usa el
  polvo color arena, y `field.dat` se carga por índice con la cantidad del archivo (0x4984b0 valida escenario <
  cantidad). Arcade y la historia siguen con sus escenarios.
- El motor, para pintar escenarios: el shader de los modelos iluminados (VS 2) hace
  `max(difusa · (N·L + 1) / 2, ambiente) × color del material`, recortado a 1 (c8 = 1, 1, 0.5, ...); la prueba de
  alfa compara con 64 (`GREATEREQUAL`); y se descartan las caras de espaldas (`CULL_CCW`): se ve la de
  (b − a) × (c − a). `vista.mjs` ahora hace las tres cosas como el juego.
- Efectos de partículas: 1 polvo de desierto, 2 polvo marrón, 3 motas blancas, 4 un resplandor rosado. La playa no
  usa ninguno: las motas parecían nieve sobre la arena.
- El nombre de la rueda se arma con las letras de los otros nombres (componentes conexas del trazo blanco; la sombra
  va con el trazo más cercano), así tiene la misma tipografía.
- APK release de 787,5 MB, firmado con `android/hino.keystore`: mismo certificado que el anterior, se instala encima.
  El entorno Android de esta máquina se armó con `_scratch/pacmini-android/instalar.sh` (JDK 21, SDK, NDK, CMake y
  Gradle, con sus sumas).

**Probado en la PC**: la rueda da la vuelta completa en los dos sentidos (con la animación entre Random y Lost
Grave -Blue-), pelea en la playa a 60 fps con la cámara girando unos 90° para cada lado, Random y Arcade.

**Probado en el emulador** (AVD de 1080x2400 con Android 15 y WHPX, armado en `_scratch/pacmini-android/avd`): el
APK se instala encima, la rueda y la pelea en la playa se ven igual que en la PC. Sin capturas de por medio, Tower of
Pray va a 60 fps, Outskirt Hill de noche a 19 (en el A52 real, a 60) y la playa a 47: el emulador no sirve para medir
el celu, pero la playa le pesa menos que la noche. Con capturas baja a ~17: en Android cada captura es cara.

**Probado en el Galaxy A52** (adb inalámbrico: el vínculo anduvo pero el puerto de conexión no apareció por mDNS; era
el 40565 de siempre): instalado encima con la misma firma, pelea en la playa a 59–60 fps de punta a punta (49,6 solo
en la carga y la presentación) con `_scratch/hino/playa/celu-playa.sh`, que espera a que desbloqueen el celu, mide sin
capturas en el medio y al final borra `entorno.txt` y la carpeta de prueba.

**Sin probar**: juego en red contra un original sin parche (si se elige la
playa, el otro no tiene el escenario 9); que la rueda recuerde la playa entre sesiones (la partida se guarda al salir
del juego y las corridas de prueba se cortan antes).

**Trampas**
- Bajar la arena hacia el agua esconde la orilla detrás del borde de la zona plana (la cámara va a ~1 de altura): la
  bajada es de 0.15 nomás.
- Un modelo no puede pasar de 65535 vértices: los promontorios van aparte (`mod_f106`).
- Dos instancias del port a la vez dejan un `record.dat` a medio escribir en su carpeta de escritura y la siguiente
  corrida se cae temprano (acceso inválido en 384f3cf8): una por vez, o carpeta nueva.
- En la presentación de cada ronda el juego dibuja una línea horizontal que cruza la pantalla: es suya, también sale
  en Outskirt Hill.
- `cmd.exe` corta los argumentos de un `.bat` en cada `;`: `sdkmanager` recibe los paquetes en un archivo
  (`--package_file`).

## 2026-10-23 (7) — Juego en red con salas: servidor, lobby y pelea IP a IP

**El modo red del original** (con REA/Ghidra y `tools/dis.mjs`). Objeto de red en `[[escena+8]+34h]`, creado al
arrancar (WSAStartup en 0x49b350): nombre en +4, comentario en +18h, puerto en +7Ch y hasta 100 entradas de 0x8B8
bytes desde +8BCh (IP en +90h, puerto en +78h, estado en +88h: 0 Unknown, 1 Standby, 2 Busy). `net.txt` se lee al
entrar a la pantalla (0x49b650) y ahí se abre el socket UDP (0x49b490: no bloqueante por WSAEventSelect, se ata al
PORT) con un hilo que recibe (0x49bc30, sondea cada 1 ms) y otro que pregunta el estado de todas las entradas cada
4 s (0x49c2e0). Mensajes (primer byte): 0x01 estado? → 0x02 estado (solo en Server On; trae nombre, comentario,
delay y la configuración del servidor), 0x0B pedido de pelea → 0x0D aceptado / 0x0E rechazado / 0x0F ocupado →
0x11 confirmación, y en la pelea 0x15 con el número de cuadro y 24 bytes de entradas. La respuesta 0x02 se asocia a
la entrada por IP de origen + el puerto que viene adentro; todo lo demás se contesta a la dirección de origen.
Pantalla de red (vtable 0x54dab8: init 0x486570, update 0x487030, dibujo 0x487a90): `[+C0h]` 0 lista, 1 esperando
respuesta, 3 le piden pelea (A acepta con 0x49c4d0, B rechaza), 0xB–0xD mensajes; `[+B0h]` Server On (C);
`[+B4h]` delay (D, 1–5); al aceptar sale a la escena 0x1C (selección de personaje) y `[red+370D8h]` queda en 1
mientras dura el juego en red.

**Dos arreglos para que el modo red original ande en el port** (probado con dos copias en la PC, `net.txt` a mano):
- `W_recvfrom` leía `WSAGetLastError` después de retomar el turno del candado del invitado, que lo pisaba: el juego
  veía un error desconocido y el hilo que recibe terminaba (nunca llegaba nada). Ahora el error se guarda antes, y
  `WSAGetLastError` es por hilo.
- El anfitrión espera la confirmación 0x11 hasta 5 s en un bucle que solo llama a `timeGetTime` sin soltar el turno:
  el hilo que recibe no corría y la aceptación siempre fallaba ("通信の不通によりキャンセル"). `timeGetTime` cede el
  turno (`invitado_ceder`, barato si nadie espera).
Con eso las dos copias llegan a la selección y la pelea va sincronizada.

**Salas** (pedido: un servidor que solo mantenga salas y presente a los jugadores, pelea IP a IP, con el diseño del
juego):
- `servidor/salas.mjs`: Node sin dependencias, TCP y UDP en el 10995. Líneas con campos separados por tabulador
  (HOLA, CREAR, UNIRSE, SALIR, DELAY, EMPEZAR, VOLVER, PING / BIENVENIDO, LISTA, SALA, PAR, IR, AVISO). Por UDP
  recibe "HKLR"+token desde el socket del juego (así ve la dirección pública que le dio el NAT) y contesta "HKLB"
  (búsqueda en la red local). Con la sala llena manda PAR a cada uno con la dirección pública y la local del otro y
  una clave. Límites: 512 bytes por línea, 16 conexiones por IP, 45 s sin noticias y afuera. `servidor/prueba.mjs`
  (en `npm test`) lo prueba con dos clientes de mentira.
- `win32/salas.c`: el cliente. Escribe `net.txt` en la carpeta de escritura (gana a la de datos) con una sola
  entrada, `127.77.0.1:10996`; `ws2_32.c` traduce esa dirección a la del rival al mandar y al revés al recibir, filtra
  los paquetes "HKL…" del lobby, y si el 10996 está ocupado (dos copias en una máquina) ata el siguiente. Perfora el
  NAT desde el socket del juego (paquetes P/Q con la clave, cada 100 ms hasta la primera ida y vuelta y después uno
  por segundo, que da el ping) y, al empezar, aprieta por el jugador las teclas del modo red original con
  `teclas_sala[]` (C = Server On en el anfitrión mientras tiene sala, A sobre la entrada cuando está en Standby en el
  invitado, A para aceptar en el anfitrión si el pedido viene de la dirección virtual). La pantalla de red se
  detecta con un fragmento de C en su update (`analisis/parches.txt`, 487030). Mientras el lobby se ve, DirectInput
  no le pasa al juego nada más que esas teclas.
- `d3d/sala.c`: el lobby, dibujado dentro del backbuffer antes de las capturas (sale en `HINO_CAPTURAS`) con las
  texturas de la pantalla de red leídas de los `.dat` (fondo `tex_sys19` reflejado a los costados, título,
  Server On/Off, Port, Delay, Page, Unknown/Standby/Busy y la barra que late `(sin(t·0,1)+2)/8` de `tex_sys79`), la
  MS Gothic de 24 con sombra de 1 px y las mismas posiciones (filas cada 45 desde y 172, texto en x 143 y 354).
  Entra y sale con fundido a negro, tapando la pantalla original desde su primer cuadro.
- `tools/sala/letras.mjs` (lo corre `playa.mjs`, va en `vsobj2.dat` como `obj/tex_sala.bmt` + `obj/sala.txt`): 51
  letras recortadas de los nombres de escenarios (`tex_sys24`), de personajes (`tex_sys25`), de la red (`tex_sys79`)
  y los dígitos de la configuración (`tex_sys69`, a tamaño natural como los usa la red), con su línea base; y siete
  ayudas armadas con pedazos de la cursiva fina de las ayudas de la red (la A y la P salen de Replay, `tex_sys92`,
  achicadas a la misma altura). No hay b, E, F, I, J, N, V, Y, Z ni p minúscula: los textos nuevos se eligieron con
  lo que hay ("Button A···Play", "Button D···Player / Server").

**Probado**: dos copias en la PC contra el servidor local (crear, entrar, cambiar el delay, que viaja en el 0x0D del
juego, empezar, selección y pelea sincronizadas; si uno se cae, el otro vuelve solo al lobby); búsqueda del servidor
en la red local; Android (emulador x86_64, detrás del NAT de QEMU) contra la PC: entra, se conecta y pelea al primer
intento, a 48 fps los dos (el juego en red va al paso del más lento).

**Sin probar**: el celu real (la depuración inalámbrica estaba apagada) e Internet con NAT de verdad en los dos
lados. Si los dos están detrás de NAT simétrico no hay camino directo (no hay relevo por el servidor, a propósito).

**Trampas**
- El NAT de QEMU es simétrico: el emulador sale por puertos distintos según el destino. Primero la PC le mandaba a
  un puerto y el pedido de pelea llegaba de otro, el lobby no lo reconocía y lo rechazaba (0x0E). Ahora todo lo que
  llega con la clave de la sala es del rival y se le contesta por donde llegó lo último (como RTP simétrico).
- Con `MSYS_NO_PATHCONV=1` (para adb) Git Bash ya no convierte `/c/...`: el exe de Windows y node necesitan
  `cygpath -w`.
- La MS Gothic al doble de ancho no entra en una sola fila de 1024: el atlas va en filas.
- Mandar la difusión de búsqueda necesita `SO_BROADCAST`; la respuesta del servidor vuelve directa (sin bloqueo de
  multicast en Android).

## 2026-10-24 (8) — Salas con nombre y clave, echar, y el arranque que fallaba en el celu

**Pedido**: "posibilidad de crear salas para que cada jugador juegue con quien quiera, tanto en PC como en Android".
Las salas ya estaban, pero la ayuda decía "Server On/Off" (el texto del juego) y la lista solo mostraba al anfitrión:
no se entendía que C creaba una sala. Ahora es un sistema de salas a la vista:
- **Protocolo 2** (`servidor/salas.mjs`, los clientes de la versión 1 reciben el aviso "version"): `CREAR nombre
  clave`, `UNIRSE sala clave`, `ECHAR`; `LISTA` y `SALA` traen el nombre de la sala y si es privada (la clave nunca
  sale del servidor). Cinco claves mal seguidas y un minuto sin poder entrar a salas privadas (ni con la buena). La
  prueba (`servidor/prueba.mjs`) cubre clave mal y bien, echar y volver a entrar, el nombre por defecto y el límite.
- **Lobby** (`d3d/sala.c`): C abre "Create Room" (nombre, por defecto "<nombre>'s room", y clave opcional); la lista
  muestra nombre, anfitrión, 1/2 y "Key"; A entra (con clave, la pide: "Enter Password"); en la sala el anfitrión
  empieza (A), echa (C), cambia el delay (D) o la cierra (B). Los formularios usan el teclado (en Android, el de
  pantalla). Las ayudas de arriba se escriben en el momento con la negrita del juego a 0,8 ("Button C···Create Room",
  "Button A···Enter Room", "Button C···Kick"...), y la letra suma E, F, N, V, p y x del menú de configuración
  (`tex_sys69`, achicadas a 0,85 con su línea base): 57 letras.
- `HINO_TEXTO="cuadro:texto|..."` escribe en los formularios desde el guion de pruebas (como `HINO_TECLAS`).

**El arranque que fallaba en el celu**: `salas_cuadro` toma la hora al empezar el cuadro, pero el `IR` del servidor
se procesa después en el mismo cuadro y anota una hora posterior; `ahora - t_estado > 25000` con enteros sin signo
daba enorme y el tiempo límite del arranque saltaba en el acto. En la PC casi siempre cae en el mismo milisegundo; en
el A52, más lento, no: el celu abandonaba enseguida, apretaba B para limpiar la pantalla de red y cancelaba el pedido
de pelea (el registro del celu tenía "Starting" y "did not start" en el mismo bloque de 300 cuadros, y la PC esperó
sus 25 s). Lo mismo hacía saltar "Could not reach..." al recibir la dirección del rival. Ahora todas esas restas son
con signo (`pasaron`).

**Probado**: dos copias a la vista contra el servidor local (`_scratch/hino/red/salas-demo.sh`): sala con clave, clave
mal, entrar, echar, volver a entrar, empezar y pelear. El celu con la versión nueva quedó instalado.
