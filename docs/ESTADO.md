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
