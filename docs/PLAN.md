# Hinokakera Chaotic Eclipse → Android

Objetivo: el juego original (v1.01, `hinokakera.exe` sha256 `d9aea156…c8ab`) en Android, idéntico al original.

## Decisión: recompilación estática, no reescritura

El juego es un 3D de lucha con 13 personajes, modo historia, IA, menús y red: 7.934 funciones y 362 mil
instrucciones x86 de C++ optimizado. Reescribirlo a mano obliga a entender y copiar cada mecánica, y
cualquier detalle mal copiado lo hace distinto. En cambio, si se traduce el código máquina original a C y se
compila para ARM, corre la lógica original: cada daño, combo, IA y cuadro sale igual por construcción.
Lo único que se escribe a mano es lo que estaba fuera del exe: las DLL de Windows (Direct3D 9, DirectInput,
DirectSound, kernel32, etc.), reimplementadas sobre OpenGL ES, SDL y Android.

Es la técnica de los recomps de N64/Xbox 360 (N64Recomp, XenonRecomp), aplicada a un exe de Windows.

## Lo que se sabe del exe (REA + desensamblado propio, 2026-10-22)

- PE32 x86, base fija 0x400000, sin relocaciones. `.text` 1,35 MB, 7.934 funciones (Ghidra 12.1.4 vía REA).
- Compilado con **Visual C++ 6.0**: código del juego optimizado (/O2), enlazado con MFC 4.2 y CRT **de debug**
  estáticos (por eso `config.dat` termina en `CD CD CD CD`). MFC se usa como biblioteca (CString, CFile,
  arrays); el WinMain (`0x49e4e0`) es Win32 puro, no `AfxWinMain`.
- FPU **x87** en todo (sin SSE: la única `movups` es basura después de un `exit`). 130 mnemónicos distintos.
- 117 tablas de `switch`, 5.290 llamadas indirectas (vtables C++ y COM), SEH por `fs:[0]` en pocas funciones.
- Importa: D3D9 (`Direct3DCreate9`), D3DX9_37 (matemática, `D3DXAssembleShader`,
  `D3DXCreateTextureFromFileInMemoryEx`), DINPUT8, DSOUND8, WINMM (`timeGetTime`, mmio), WS2_32 (UDP, netplay),
  GDI32 (`GetGlyphOutlineA`: el texto se dibuja con fuentes de Windows), más lo que arrastra MFC.
- Shaders: 10 `vs_1_0` en texto, ensamblados en tiempo de ejecución (luz con rampa toon). Píxel: etapas fijas.
- BGM en `bgm/*.ogg`, decodificado por libvorbis enlazado dentro del exe.

## Datos (`tools/dat1.mjs`)

Contenedor `dat1` sin cifrar: `"dat1"`, u32 cantidad y por entrada u32 largo, nombre, u32 offset, u32 tamaño.
`vsobj` (texturas `BMT_`/`BMA_`, mallas `nmd_`), `vsdat` (personajes `smd_` + `skl_`, mensajes), `vswav`
(733 WAV), `nv*` (novela: scripts `ndat` con texto en NOT bit a bit). Con la recompilación no hace falta
entender estos formatos: el código original los lee igual que en Windows.

## Etapas

1. **Traductor** (`recomp/`, Node + iced-x86): exe → C, una función C por función x86. Prueba diferencial de
   cada forma de instrucción contra la CPU real (arnés x86 de 32 bits con MSVC).
2. **Runtime** (`runtime/`, C): memoria de 4 GB del invitado, contexto de CPU, x87, despacho de llamadas
   indirectas, hilos, heap. Arranca el CRT hasta WinMain.
3. **Windows de prueba**: el recompilado corriendo en esta PC con SDL + OpenGL, para comparar cuadro a cuadro
   contra el exe original (capturas en las mismas escenas, repeticiones grabadas).
4. **Direct3D 9 → OpenGL ES 3**, DirectSound → mezclador SDL, DirectInput → teclado/mando/toques,
   GDI → glifos pre-renderizados con la GDI real (texto idéntico).
5. **Android**: NDK + SDL, datos en el APK, controles táctiles, pantalla completa. Emulador y celu real.

## Verificación "píxel a píxel"

- Semántica de instrucciones: diferencial contra la CPU real.
- Lógica: las repeticiones (`replay/`) del original tienen que reproducirse igual en el port.
- Imagen: capturas del original y del port en la misma escena, diferencia por píxel.
