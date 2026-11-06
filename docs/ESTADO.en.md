<p align="right"><sub><b>English</b> · <a href="ESTADO.md">Español</a></sub></p>

# Status (log)

## 2026-10-23 — the original exe runs translated and the title screen comes out the same

**Done**
- x86 → C translator (`recomp/`): 9,306 functions, 424 thousand instructions, 0 unsupported ones executed.
  It discovers functions through calls, vtables and immediates (fixed point), detects functions that never return,
  propagates flags across functions (the CRT's assembly routines) and replaces VC6's `memcpy/memmove` with native
  ones (`analisis/reemplazos.txt`). It compiles with clang 19 in ~15 s (16 cores).
- Runtime (`runtime/`): 4 GB of guest memory, x87 on double with 24/53-bit precision according to the control word
  (Direct3D leaves it at 24), FNSAVE/FRSTOR, threads, TEB, indirect dispatch.
- Simulated Windows (`win32/`): kernel32 (memory, files with a read-only data folder + a write folder, threads and
  synchronization, TLS, resources, CP932 with Windows' real table), user32 (SDL windows, message queue → the game's
  WndProc), gdi32 (glyphs: real GDI on Windows), winmm, DirectInput 8, DirectSound 8 (its own mixer), Winsock (real
  UDP for netplay).
- Direct3D 9 on OpenGL ES 3 (`d3d/`): the fixed function generated as GLSL, vs_1_x translated, FBOs with row 0 at the
  top, D3D9's half pixel, textures from the in-memory BMP the game builds (D3DX).
- **Pixel-by-pixel verification**: `tools/c/d3d9_espia.c` is a d3d9.dll that goes into a copy of the original game
  and dumps the backbuffer of the requested frames. Title, frames 30-400: **479,892 of 480,000 pixels identical
  (99.978 %)**; the other 108 differ by 1 level, all inside the "ver. 1.01" text (the GPU's blending rounding over the
  text's A4R4G4B4 texture).

**How to run it**
- `tools/correr.sh` builds and runs (the original's data read-only, writes go to `run/escritura`).
  `HINO_OCULTA=1` no window, `HINO_VENTANA=1` not full screen, `HINO_CAPTURAS=30,120` saves frames.
- Debugging: `RASTRO=1 tools/correr.sh --rastro` (the guest's function stack and a check of every `ret`);
  `HINO_TRAZA=<va>` while translating traces that function instruction by instruction.
- The original with the spy: a copy in `Box/_scratch/hino/original` (the big .dat files are hardlinks, config in
  windowed mode), the spy `d3d9.dll` + `capturar_original.exe`.

**Bugs found and fixed along the way**
- Functions with shared blocks located before the entry point started at the wrong block.
- `call [IAT]` is an IndirectCall in iced (the no-return ones weren't detected).
- 32-bit stdcall exports come out decorated: the spy needs `/EXPORT:Name=_Name@n`.

## 2026-10-23 (2) — menus, character select and fights identical to the original

- A per-frame key script (`HINO_TECLAS="frame:dik:1|0,..."`) in the port and in the original
  (`tools/c/dinput8_espia.c`, which also asks for background DirectInput because the launched window doesn't have
  focus). With the same script, both go through title → menu → Arcade → select → fight.
- Backbuffers compared on the same frames: character select 95.4 % identical (max ±2, in the blended animated
  background), the VS transition 99 %, **the fight from frame 700 to 2500: 94-96.5 % identical**; the only big
  difference is the HUD's FPS counter (the original runs at 33 FPS on this notebook, the port at 60). The fight (AI,
  combos, damage) advances the same frame by frame: the recompiled logic is faithful.
- Fixes: `D3DSAMP_MIPMAPLODBIAS` (the game uses -0.5; it now goes in as a bias in `texture()`), `ExitThread` from
  threads whose handle was already closed ended the process, a corrupt `record.dat` from an old run.
- D3DX mipmaps checked against the real dll (`tools/c/prueba_d3dx.c`): a 2x2 box with `(sum+2)>>2`, 0 differences.
- New debugging: an invalid-access handler (guest address + RVA for llvm-symbolizer), a frame watchdog with every
  thread's stack, `HINO_LOG_ESTADOS` (render states the first time).

## 2026-10-23 (3) — Android: a playable APK, widescreen full screen and touch controls

**Done**
- **APK** (`apk/HinokakeraCE-android.apk`, release, arm64-v8a + x86_64, 750 MB with the game inside, signed with
  `android/hino.keystore`, the project's own key: updates install on top without losing saves).
  `node tools/apk.mjs [--abis x86_64] [--datos] [--release]` uses the shared environment in
  `Box/_scratch/pacmini-android` (JDK 21, the SDK with NDK 28.2 and CMake 3.31.6, Gradle 8.11.1 and its cache with
  AGP 8.9.2); everything generated goes to `Box/_scratch/hino-android`. The data goes in as uncompressed assets
  (`.dat`, `.ogg`), hardlinks put together by `tools/datos.mjs`; `glifos.bin` and `imagen.bin` also go in the assets.
- Android: the game runs on its own thread with a 64 MB stack (SDL's comes with ~1 MB), stderr goes to logcat (tag
  `hino`), writes go to internal storage, Back = Pause, always full screen. `-ffp-contract=off` (no FMA on ARM) and
  `-fsigned-char`, as on x86.
- **Verification on Android** against the original's backbuffer: title and menu 100.000 % identical (x86_64, and
  arm64 under the emulator's ARM translation); an arm64 fight from frame 700 to 2000: 98.4–99.7 % (what differs is
  the FPS counter); the fight goes frame by frame the same (same health and bars). The flames vary with the real clock
  (also between two runs of the original: the PC port gives 79 % at frame 2500 against that same original).
- **Widescreen mode** (`d3d/dibujo.c`, explicitly asked for: no black bars at all): the real backbuffer is as wide as
  the screen at 600 tall (1332×600 at 20:9) and the game, which still sees 800×600, is drawn centered by multiplying
  the clip x by 800/width: the 2D stays at 1:1 and the 3D shows more of the stage (Hor+). All the game's shaders do
  `m4x4 oPos, v0, c0` or `mov oPos, v0`, so the CPU knows where each draw lands: 2D that takes the whole width is
  extended (backgrounds: the edge mirrored and mirrored back; HUD/menu bands: the edge column stretched), except
  almost-transparent layers at the edges (the story mode text), according to a 64×64 alpha map per texture; the rest
  of the 2D is clipped to the game area. The central strip stays exactly as in 4:3. `HINO_ANCHO=0` turns it off,
  `HINO_ANCHO=1332` forces it in a window on the PC.
- **Touch controls** (`d3d/tactil.c`): an 8-way D-pad, A B C D and START (pause), drawn with the game's own MS Gothic
  font; they're laid out on every frame by physical size (cm according to the density), safe area (camera) and free
  space; with the wide image they go on top, semi-transparent; with a gamepad connected they aren't drawn. They write
  `teclas_virtuales[]` with player 1's keys from `config.dat`; a short tap lasts at least two frames. Tapping the image
  is the mouse (story mode reads clicks with DirectInput's `GetDeviceData`, which now has an event buffer) and the real
  mouse is also rescaled to game coordinates.
- Presentation: "sharp bilinear" scaling (integer and unfiltered, then linear) to the size of the screen.
- Icon: the exe's (`HIKAKE_ICON`, 32×32, found with REA's `inspect_pe_resources`), `tools/icono.mjs`.

**Tested on the emulator** (subibaja's `celu` AVD, 2400×1080): title, menu (the mode wheel), Arcade, select, fight,
pause with Back, story mode advancing with taps; ~50 fps (the emulator is limited by the translated GPU: the game's
thread uses ~19 % of a CPU).

**How to test on the emulator**: `node ../subibaja/tools/emu.mjs start`, `node tools/apk.mjs --abis x86_64 --datos`,
`node tools/android.mjs instalar`, `node tools/android.mjs entorno "HINO_ESCRITURA={escritura}" "HINO_CAPTURAS=30"`
(test variables in `entorno.txt` on external storage), `node tools/android.mjs correr 15`,
`node tools/android.mjs bajar <dir>`. Careful: whatever `adb push` uploads to `Android/data/<pkg>` ends up owned by
`shell` and the app can't get into its subfolders: test data always goes inside the APK.

**Debugging**: `HINO_LOG_DIBUJOS=cuadros` (every call: 2D/3D, on-screen box, texture, target), `HINO_LOG_VS=1` (the
vertex shaders' source), `HINO_LOG_ARCHIVOS=1` (file opens).

**Not tested** (at that point; the real phone got tested in entry 4): online play, real multitouch (the emulator only
sends one finger), every screen of story mode and the extras.

**Ideas**
- The fight HUD spread out to the edges (today it stays centered, with the frame extended).
- Story backgrounds: the mirroring shows in some images; blurring the sides would hide it.

## 2026-10-23 (4) — a real phone: the guest lock and the screen at its real size

**Done**
- Tested on a **Samsung Galaxy A52** (SM-A525M, Android 14, Adreno 618, 2400×1080) over wireless adb: 60 fps.
- **A hang only on real ARM**: a SIGSEGV on the music thread (the game's Vorbis decoder, `f_0040fc30` →
  `f_004c6350`) that didn't show up on the PC, nor on the emulator, nor when slowing down reads (`HINO_LENTO=ms` in
  `k32_archivos.c` delays `ReadFile` on secondary threads). The game counts on x86's memory ordering (TSO) and on ARM
  one thread was reading half-written data. The fix: a **guest lock** (`runtime/hilo.c`, turns with a mutex and a
  condition variable): a single game thread runs at a time; the lock is released around `Sleep`, waits, busy critical
  sections, `GetMessage`, `recvfrom` and the `SwapWindow`, and `PeekMessage` yields the turn. On the A52 it held up
  almost 8 minutes straight (27,900 frames) without a hitch; on the PC the fight frames come out the same as before.
- The window started smaller (2186×1017) before going to 2400×1080 and black bars were left: `ancho_ajustar` rebuilds
  the backbuffer after every `SwapWindow` if the size changed.
- Public repo `github.com/agustinyarrus/hinokakera-android`: code, documentation and screenshots; without the game,
  without its translated code (`gen/`) and without the APK, which carry it inside.

## 2026-10-23 (5) — Outskirt Hill at night (Moonlit Wilderness style)

**Done**
- `node tools/escenario/moonlit.mjs [--png folder]` builds `_scratch/hino-android/mods/vsobj2.dat`: the game's patch
  plus 20 new files. The game looks in `vsobj2.dat` first (a merged table, the first one wins), so the stage changes
  without touching the exe; `tools/datos.mjs` takes what's in `mods/` before the game's files.
- Formats (with REA): `dat/field.dat` (loader at 0x4984b0, `tools/escenario/campo.mjs`): per stage, the list of models
  with their vertex type (0 xyz+normal, 1 +uv, 2 xyz, 3 xyz+uv; 0/1 are lit), Z, Z write, blending (0x403ad0: 2 alpha,
  3 additive) and alpha test; a particle effect (1 desert dust, 3 white specks), diffuse/ambient light/direction and
  wind (it moves clothes and hair). Model 0 is the floor: its texture scrolls with the fight. Every model follows the
  center of the fight: the stage is a diorama around the fighters, and when the round starts the camera looks toward
  +Z, ~5.7 units back, with a ~30° vertical FOV. In `nmd_`, material floats 0–3 are the color (the shader's c4
  constant), 4 turns on a specular pass (0 = no), and `ini`/`n` count triangles. `BMA_` is ARGB with no palette.
- What it has: a sky dome (a 4096×1024 panorama in two halves, with non-linear elevation so the strip the camera sees
  has resolution), a moon with a halo (alpha blending), floor and hills turned into night from the originals, hills
  with a moonlit edge and distant ruins instead of the city, dead trees (billboards and 3D), pampas grass (the
  original's grass strips, 3 times taller, and 200 clumps between r 13.2 and 17.5 in three tones, drawn from the
  outside in because they don't write Z), Gothic ruins on a hill (pointed-arch arcades, a tower, a fallen wing, a
  loose arch and rubble; lit), a toppled monolith, bluish moonlight and effect 3 as floating fluff.
- The stage select thumbnail (the fourth strip of `tex_sys42`): rendered by `tools/escenario/vista.mjs`, a software
  rasterizer with the same flags as the game; `tools/escenario/ver.mjs <n> <png> [--sin m] [--sintex t]` shows any
  stage without running the game. `npm test` tests the tools.

**Traps**
- Ear clipping got stuck on repeated or collinear points (the steps of the broken edge) and left faces uncovered:
  `limpiar()` before triangulating.
- Two strips in the same texture contaminate each other through filtering: the bottom of one showed up as little
  lines above the other. They get a transparent margin.
- The almost-transparent pixels of the original hills were white and invisible; turned dark blue, they were little
  lines in the sky: low alpha is cut off.
- The noise periods have to be integers or the panorama doesn't wrap seamlessly.

## 2026-10-23 (6) — Sunlit Shore: a new beach stage

**Done**
- `node tools/escenario/playa.mjs [--png folder]` first builds Outskirt Hill at night (`moonlit.mjs`) and adds the
  beach to the same `mods/vsobj2.dat`: a `field.dat` with 10 stages (the beach is number 9), the wheel with new
  `tex_sys24` (names, 256x512) and `tex_sys42` (thumbnails, 512x1024), 7 models (`mod_f100`–`f106`) and 12 textures
  (`tex_f100`–`f111`). It comes out byte for byte the same on every run.
- The stage wheel (the select scene: 0x48f070 builds it, 0x48fb10 moves it, 0x490af0 draws it): `[esi+100h]` slot,
  `[esi+10Ch]` count (8), `[esi+108h]` animation. Slot = stage 0 to 6; slot 7 was Random, and choosing it turned into
  9, the "random" marker. The code that sets up the fight (0x49540a) accepted 0–8 and, with 9 or a negative, did
  `rand16 % 7` (also in arcade, 0x495f55, and in COM vs. COM, 0x495fb5). The choice is saved in the save game
  (`[+0F00h]`, which is validated from 0 to 9 on load at 0x49a4e7) and the initial slot comes from there (capped at 7).
- `analisis/parches.txt`, which `recomp/parches.mjs` applies while translating (it checks the original bytes): 34
  instructions and one constant, plus 5 C fragments. Nine slots (7 = beach, 8 = Random); Random chosen = -1 and the
  saved marker = 10; the fight accepts 0–9 and slot 7 is stage 9; the random pick is `% 8` with 7 → 9. The thumbnails
  divide v by 1024 and the names by 512 with constants that were already there (1/1024 at 54d6a0, 1/512 at 54c134),
  and Random stopped being a special case (`cmp` with 7Fh, never true): it uses its strip like the others. Strips 9
  and 15 of the thumbnail repeat 0 and Random because the previous and the next one are drawn without a modulo (-1
  and 9).
- Nothing else depends on the stage number: the effects only ask whether it's the snow (2), so the beach uses the
  sand-colored dust, and `field.dat` is loaded by index with the count from the file (0x4984b0 checks stage < count).
  Arcade and story mode keep their stages.
- The engine, for painting stages: the shader for lit models (VS 2) does
  `max(diffuse · (N·L + 1) / 2, ambient) × material color`, clamped to 1 (c8 = 1, 1, 0.5, ...); the alpha test compares
  against 64 (`GREATEREQUAL`); and back faces are culled (`CULL_CCW`): the visible one is that of (b − a) × (c − a).
  `vista.mjs` now does all three things like the game.
- Particle effects: 1 desert dust, 2 brown dust, 3 white specks, 4 a pink glow. The beach uses none: the specks
  looked like snow on the sand.
- The name on the wheel is put together from the letters of the other names (connected components of the white
  stroke; the shadow goes with the nearest stroke), so it has the same typeface.
- A 787.5 MB release APK, signed with `android/hino.keystore`: the same certificate as before, it installs on top. The
  Android environment on this machine was set up with `_scratch/pacmini-android/instalar.sh` (JDK 21, SDK, NDK, CMake
  and Gradle, with their checksums).

**Tested on the PC**: the wheel goes all the way around in both directions (with the animation between Random and
Lost Grave -Blue-), a fight on the beach at 60 fps with the camera turning about 90° to each side, Random and Arcade.

**Tested on the emulator** (a 1080x2400 AVD with Android 15 and WHPX, set up in `_scratch/pacmini-android/avd`): the
APK installs on top, and the wheel and the beach fight look the same as on the PC. With no captures in between, Tower
of Pray runs at 60 fps, Outskirt Hill at night at 19 (on the real A52, at 60) and the beach at 47: the emulator is no
good for measuring the phone, but the beach weighs on it less than the night. With captures it drops to ~17: on
Android every capture is expensive.

**Tested on the Galaxy A52** (wireless adb: pairing worked but the connection port didn't show up over mDNS; it was
the usual 40565): installed on top with the same signature, a fight on the beach at 59–60 fps from start to finish
(49.6 only during loading and the intro) with `_scratch/hino/playa/celu-playa.sh`, which waits for the phone to be
unlocked, measures with no captures in between and at the end deletes `entorno.txt` and the test folder.

**Not tested**: online play against an unpatched original (if the beach is picked, the other side doesn't have stage
9); whether the wheel remembers the beach between sessions (the save game is written when quitting the game, and the
test runs are cut off before that).

**Traps**
- Lowering the sand toward the water hides the shoreline behind the edge of the flat area (the camera sits at a height
  of ~1): the slope is only 0.15.
- A model can't go over 65535 vertices: the headlands go separately (`mod_f106`).
- Two instances of the port at once leave a half-written `record.dat` in their write folder and the next run crashes
  early (invalid access at 384f3cf8): one at a time, or a new folder.
- In each round's intro the game draws a horizontal line across the screen: it's the game's own, it also shows up on
  Outskirt Hill.
- `cmd.exe` cuts a `.bat`'s arguments at every `;`: `sdkmanager` gets the packages in a file (`--package_file`).

## 2026-10-23 (7) — Online play with rooms: server, lobby and IP-to-IP fights

**The original's network mode** (with REA/Ghidra and `tools/dis.mjs`). The network object is at `[[escena+8]+34h]`,
created at startup (WSAStartup at 0x49b350): name at +4, comment at +18h, port at +7Ch and up to 100 entries of 0x8B8
bytes from +8BCh (IP at +90h, port at +78h, state at +88h: 0 Unknown, 1 Standby, 2 Busy). `net.txt` is read on
entering the screen (0x49b650), and that's where the UDP socket is opened (0x49b490: non-blocking through
WSAEventSelect, bound to PORT) with one thread that receives (0x49bc30, polling every 1 ms) and another that asks for
the state of every entry every 4 s (0x49c2e0). Messages (first byte): 0x01 state? → 0x02 state (only in Server On; it
carries the name, comment, delay and the server's settings), 0x0B fight request → 0x0D accepted / 0x0E rejected / 0x0F
busy → 0x11 confirmation, and during the fight 0x15 with the frame number and 24 bytes of inputs. The 0x02 reply is
matched to the entry by source IP + the port that comes inside; everything else is answered to the source address.
The network screen (vtable 0x54dab8: init 0x486570, update 0x487030, draw 0x487a90): `[+C0h]` 0 list, 1 waiting for a
reply, 3 being asked for a fight (A accepts with 0x49c4d0, B rejects), 0xB–0xD messages; `[+B0h]` Server On (C);
`[+B4h]` delay (D, 1–5); on accepting it goes to scene 0x1C (character select) and `[red+370D8h]` stays at 1 while the
online game lasts.

**Two fixes so the original network mode works in the port** (tested with two copies on the PC, `net.txt` by hand):
- `W_recvfrom` read `WSAGetLastError` after taking back the guest lock's turn, which overwrote it: the game saw an
  unknown error and the receiving thread ended (nothing ever arrived). Now the error is saved first, and
  `WSAGetLastError` is per thread.
- The host waits up to 5 s for the 0x11 confirmation in a loop that only calls `timeGetTime` without releasing the
  turn: the receiving thread never ran and accepting always failed ("通信の不通によりキャンセル", "cancelled because
  the connection failed"). `timeGetTime` yields the turn (`invitado_ceder`, cheap if nobody is waiting).
With that, both copies get to character select and the fight stays in sync.

**Rooms** (the request: a server that only keeps rooms and introduces the players, fights IP to IP, with the game's
look):
- `servidor/salas.mjs`: Node with no dependencies, TCP and UDP on 10995. Lines with tab-separated fields (HOLA, CREAR,
  UNIRSE, SALIR, DELAY, EMPEZAR, VOLVER, PING / BIENVENIDO, LISTA, SALA, PAR, IR, AVISO). Over UDP it receives
  "HKLR"+token from the game's socket (that's how it sees the public address the NAT gave it) and answers "HKLB"
  (search on the local network). When a room is full it sends PAR to each player with the other's public and local
  addresses and a key. Limits: 512 bytes per line, 16 connections per IP, 45 s without news and you're out.
  `servidor/prueba.mjs` (in `npm test`) tests it with two fake clients.
- `win32/salas.c`: the client. It writes `net.txt` in the write folder (which wins over the data folder) with a single
  entry, `127.77.0.1:10996`; `ws2_32.c` translates that address into the opponent's when sending and back when
  receiving, filters out the lobby's "HKL…" packets, and if 10996 is taken (two copies on one machine) binds the next
  one. It punches the NAT from the game's socket (P/Q packets with the key, every 100 ms until the first round trip and
  then one per second, which gives the ping) and, when it starts, presses the original network mode's keys for the
  player with `teclas_sala[]` (C = Server On on the host while it has a room, A on the entry when it's in Standby on
  the guest, A to accept on the host if the request comes from the virtual address). The network screen is detected
  with a C fragment in its update (`analisis/parches.txt`, 487030). While the lobby is showing, DirectInput passes
  nothing to the game except those keys.
- `d3d/sala.c`: the lobby, drawn into the backbuffer before the captures (it shows up in `HINO_CAPTURAS`) with the
  network screen's textures read from the `.dat` files (the `tex_sys19` background mirrored at the sides, the title,
  Server On/Off, Port, Delay, Page, Unknown/Standby/Busy and the pulsing bar `(sin(t·0.1)+2)/8` from `tex_sys79`), the
  24-px MS Gothic with a 1-px shadow and the same positions (rows every 45 from y 172, text at x 143 and 354). It comes
  in and goes out with a fade to black, covering the original screen from its very first frame.
- `tools/sala/letras.mjs` (run by `playa.mjs`, it goes in `vsobj2.dat` as `obj/tex_sala.bmt` + `obj/sala.txt`): 51
  letters cut out of the stage names (`tex_sys24`), the character names (`tex_sys25`), the network screen
  (`tex_sys79`) and the configuration digits (`tex_sys69`, at natural size as the network screen uses them), with their
  baseline; and seven hints put together from pieces of the thin italic of the network screen's hints (the A and the P
  come from Replay, `tex_sys92`, shrunk to the same height). There's no b, E, F, I, J, N, V, Y, Z or lowercase p: the
  new texts were chosen with what there is ("Button A···Play", "Button D···Player / Server").

**Tested**: two copies on the PC against the local server (create, join, change the delay, which travels in the
game's 0x0D, start, select and fight in sync; if one drops, the other goes back to the lobby on its own); finding the
server on the local network; Android (x86_64 emulator, behind QEMU's NAT) against the PC: it joins, connects and fights
on the first try, at 48 fps on both (the online game goes at the pace of the slower one).

**Not tested**: the real phone (wireless debugging was off) and the Internet with real NAT on both sides. If both are
behind a symmetric NAT there's no direct path (there's no relay through the server, on purpose).

**Traps**
- QEMU's NAT is symmetric: the emulator goes out through different ports depending on the destination. At first the
  PC sent to one port and the fight request arrived from another, the lobby didn't recognize it and rejected it (0x0E).
  Now everything that arrives with the room's key is from the opponent and is answered wherever the last packet came
  from (like symmetric RTP).
- With `MSYS_NO_PATHCONV=1` (for adb), Git Bash no longer converts `/c/...`: the Windows exe and node need
  `cygpath -w`.
- Double-width MS Gothic doesn't fit in a single 1024 row: the atlas goes in rows.
- Sending the search broadcast needs `SO_BROADCAST`; the server's reply comes back directly (no multicast blocking on
  Android).

## 2026-10-24 (8) — Rooms with a name and a password, kicking, and the start that failed on the phone

**The request**: "being able to create rooms so each player plays with whoever they want, both on PC and on Android".
The rooms were already there, but the hint said "Server On/Off" (the game's text) and the list only showed the host:
it wasn't clear that C created a room. Now it's a room system in plain sight:
- **Protocol 2** (`servidor/salas.mjs`; version-1 clients get the "version" notice): `CREAR name password`,
  `UNIRSE room password`, `ECHAR`; `LISTA` and `SALA` carry the room's name and whether it's private (the password
  never leaves the server). Five wrong passwords in a row and a minute without being able to join private rooms (not
  even with the right one). The test (`servidor/prueba.mjs`) covers a wrong and a right password, kicking and joining
  again, the default name and the limit.
- **Lobby** (`d3d/sala.c`): C opens "Create Room" (a name, by default "<name>'s room", and an optional password); the
  list shows the name, the host, 1/2 and "Key"; A joins (with a password, it asks for it: "Enter Password"); in the
  room the host starts (A), kicks (C), changes the delay (D) or closes it (B). The forms use the keyboard (on Android,
  the on-screen one). The hints at the top are written on the fly with the game's bold at 0.8 ("Button C···Create
  Room", "Button A···Enter Room", "Button C···Kick"...), and the lettering adds E, F, N, V, p and x from the
  configuration menu (`tex_sys69`, shrunk to 0.85 with their baseline): 57 letters.
- `HINO_TEXTO="frame:text|..."` types into the forms from the test script (like `HINO_TECLAS`).

**The start that failed on the phone**: `salas_cuadro` takes the time when the frame starts, but the server's `IR` is
processed later in the same frame and records a later time; `ahora - t_estado > 25000` with unsigned integers came out
huge and the start's timeout fired on the spot. On the PC it almost always lands in the same millisecond; on the A52,
which is slower, it doesn't: the phone gave up right away, pressed B to clear the network screen and cancelled the
fight request (the phone's log had "Starting" and "did not start" in the same 300-frame block, and the PC waited its
25 s). The same thing made "Could not reach..." fire when receiving the opponent's address. Now all those
subtractions are signed (`pasaron`).

**Tested**: two copies side by side against the local server (`_scratch/hino/red/salas-demo.sh`): a room with a
password, a wrong password, joining, kicking, joining again, starting and fighting. The phone was left with the new
version installed.

## 2026-11-05 — Lost Grave -Undead-: a new stage with zombies

**Done**
- **Stage 10, the ninth option of the VS wheel** (it comes after ( Random ), right before Lost Grave -Blue-): Lost
  Grave at night with a red moon, smoke and fires on the horizon, a ring of barricades (striped concrete barriers,
  sandbags, barrels and burnt cars) and 46 zombies that move: the ones pushing against the barricades reach out with
  their arms and nod, and 20 circle in two lanes, each lane its own way and at an even speed (they don't cross each
  other). 5 barrels and a car are burning, with flickering flames and a glow on the ground. Random still picks among
  the 7 originals. `node tools/escenario/mods.mjs` builds the two new stages (the night one and this one) into
  `mods/vsobj2.dat`; what they share went into `tools/escenario/comun.mjs`.
- **Patches to the exe** (`analisis/parches.txt`; `recomp/traducir.mjs` applies them before translating and stops if
  the original bytes don't match): the wheel goes from 8 to 9 options; on confirming, option 7 (Random) gives 9 and
  8 gives 10; setting up the VS fight takes "x == 9" as random (before, "x >= 9"); going back to the wheel, the cap
  goes from 7 to 8. Stage 9 of field.dat stays as a copy of 0, because 9 is the "random" marker. The stage number is
  only used to load field.dat (the loader at 0x4984b0, which is already data-driven), the snow of Lost Grave -white-
  and the wheel cursor: there are no tables of 9 to overrun.
- **One more row without touching the code that draws the wheel** (0x490af0): names (`tex_sys24`, 8 rows of 32) and
  thumbnails (`tex_sys42`, bands of 64) are cut with fixed fractions and 7 is Random by hand; row 8 asks for v from 1
  to 2. Both textures go at double height with two transparent marker pixels at the end (CCFFEE and BBDD55; the high
  digit is checked so it survives the 16-bit formats); `textura_subir` recognizes them and the fragment shader
  multiplies v by `u_esc` (1 for all the others: times 1.0 doesn't change a bit). The name comes from
  `tools/escenario/texto.ps1` (GDI+, Times New Roman bold italic, widened, like the originals).
- **Animated models** (`d3d/animado.c`): if the first vertices of a VB carry the signature 7777 -3131 2468, they are
  data no triangle uses (bones with their pivot, figures, the bone of each vertex, in position and uv) and the rest
  are the figures in their rest pose, each one in its local space and with the same mesh. Every frame they are
  uploaded placed and animated from a copy; the game's VB isn't touched and time comes from the frame number (the
  captures repeat). Zombies with 7 bones (hunched, head drooping, arms reaching out, hanging or one torn off, a limp),
  1-bone shadows that follow their zombie and 2-bone fires (a still glow, a flame that flickers around its base).
  `npm test` checks the data format and the marker.

**Measurements that rule** (recorded in a fight): the camera moves 1.6–8.2 from the center of the stage (4 on
average, at ~0.8 high) and the fighters are ~1 tall; the floor of Lost Grave is flat up to ~11.6. That's why the
barricades go at 9.4, the zombies at 10 and the lanes at 10.7 and 11.3.

**Traps**
- BMT textures don't go through `D3DXCreateTextureFromFileInMemoryEx`: the game decodes them and fills them with
  `CreateTexture` + `LockRect`. Anything to look at in a texture goes in `textura_subir`.
- The stage is drawn with vertex shaders (vs 2 the lit models, vs 4 the others): the hook takes position, normal and
  uv from the attributes `dibujar()` sets up, not from the FVF.
- A macro with `p++` as an argument evaluated it three times: the figures came out shifted and giant zombies showed up
  next to the camera.
- Turning the car with (x, y, z) -> (z, y, x) is a mirror and flips the faces: rotations only.
- field.dat particles: 1 and 2 are dust bands (they hide the zombies), 3 snow, 4 a sunset glow, 5 the rocks floating
  in the final stage. Undead has none.

**Tested**: on the PC (60 fps) and on the emulator with the release APK (x86_64, 59.6–59.9 fps in the fight): wheel,
thumbnail, name, a fight with the 46 zombies, shadows and fires; ( Random ) still picks among the 7 originals. On the
emulator, delete the test `escritura` folder first: it saves the last stage, the wheel starts on another option and
the key script lands somewhere else.

**Not tested**: the phone (arm64; the A52 wasn't paired), going back to the wheel after a fight on Undead (the cap
from 7 to 8), online play with stage 10 (both sides need this version) and story mode (it doesn't use the wheel).

## 2026-11-06 — Eclipse Caldera: a stage with columns to jump on

**Done**
- **Stage 11, the tenth option of the wheel** (after Undead): a basalt island in a lava lake inside a caldera, under a
  total eclipse (the sunset ring all around the horizon and a distant volcano in the caldera's breach, below the
  eclipse). It has a fire crystal that spins and floats over the lava, flowing lava falls and lava lake, braziers,
  obelisks, rising embers and the eclipse seal on the floor. It's all in `tools/escenario/caldera.mjs`, and `mods.mjs`
  builds the three stages.
- **Height differences**: 17 basalt columns (`dat/suelo.dat`, inside `vsobj2.dat`). There's a staircase of three on
  each side on the x axis (6, 11, 16), another one to the north up to the summit (20; only reachable from the 10 one),
  a 3.5 dais you walk up onto and loose pillars. The arena has a radius of 150.
- **Hooks in the translator** (`analisis/ganchos.txt`, `recomp/emitir.mjs`): before the given instruction the
  registers are dumped into the context, the runtime function is called and they are reloaded. The flags stay as they
  were and the x87 stack already lives in the context. `traducir.mjs` stops if a hook doesn't land on a translated
  instruction. There are 53, all from `runtime/escenarios.c`:
  - the 41 comparisons of the height with 0 (or with 8) subtract the top underneath from ST0;
  - the physics' fall rests on the top that was underneath when the frame started (so you don't go through it when
    falling fast) and the "height = 0" ones leave it on that top;
  - a column taller than the feet + 4 is a wall: it pushes the fighter out to the radius + 3. This happens after the
    physics and after SetPosition (the push between fighters), and that's also where the arena's edge stops you;
  - the shadow (`D3DXMatrixShadow` on y = 0) is cast on the top;
  - the `mod_w*` models get the center (0, -0.5, 0) and stay fixed in the world;
  - the wheel cursor on the way back: the cap goes back to the original one (7) and the hook takes 10 and 11 to their
    options.
- The wheel goes to 10 options (one byte changes in `parches.txt`; the cap patch is gone).
- `d3d/animado.c`: figure types 3 (a current: the uv flows), 4 (spins and floats) and 5 (an ember that rises, sways
  and dies out).
- `npm test`: the format of `dat/suelo.dat` and the layout of the columns. All of them inside the arena and not
  overlapping, the start clear, each staircase one jump at a time and the ones on the x axis crossing it.

**How the physics was found** (per-frame memory dumps and a write watcher with read-only pages, both temporary and
already removed):
- **The fighter**: position at +A4 (x, y, z), velocity at +B0, acceleration at +C8 (gravity comes from +9C), dt 1/60
  (0x54D8C0) and a ceiling of 250. `f_0046b600` is the physics: it integrates, does "y <= 0 → y = 0, vy = 0" at
  0x46bafa, passes the position to the model (0x4059C0) and flags "in the air" (4C8 |= 4).
- **The comparisons with 0.0** (0x54AAC4) are in `f_0046c170` (states), `f_00465e10`, `f_00465730` (which moves are
  allowed on the ground or in the air, plus "height < 8") and `f_00461690` (dust when running).
- **The center of the stage**: `f_00480520` computes the fighters' midpoint with y = 0 and `f_00498b60` hands it to
  every stage model (with y - 0.5); it also scrolls the floor texture by field.dat's scale. The stage models go at
  scale 20 (0x4988b9).
- There's no double jump: the jump reaches 16.5 and moves ~14 forward.

**Traps**
- The fight starts on the x axis and at first moves along that line (down crouches, it doesn't sidestep). Later the
  line turns and a long fight covers the whole arena. That's why the starting staircases have to cross the axis:
  otherwise, jumping backwards you don't reach them.
- The game copies the position into several objects (the model and others): the one that rules is the one at +A4.
- The HUD covers whatever is more than ~8° above the horizon when the round starts: the eclipse and the crystal go
  low.
- The noise periods of repeating textures have to be integers (again): with 1/9 or 1/30 of 512 there were seams.

**Tested**
- **On the PC**:
  - climbing the west staircase: it stops at the wall at x = -42 and lands on the 6 one and the 11 one;
  - walking off the edge, and the shadow on the top;
  - a long fight with random keys: 60 fps and it reaches the arena's edge at r = 150;
  - Undead and an original stage are still as they were;
  - going back to the wheel after a fight (pause → Exit) on Eclipse Caldera and on Undead: the cursor lands on its
    option (what was left untested in the previous entry).
- **On the emulator**, with the release APK (x86_64): the wheel, the fight at 59–60 fps and the same climb.

**Not tested**: the phone (the A52 doesn't show up over wireless debugging), online play on stage 11 (both sides need
this version), reaching the summit in a fight (the line has to point north) and the AI using the columns (it jumps,
but doesn't go for them).

## 2026-11-06 (2) — The three new stages together: the 11-slot wheel

**What happened**: Sunlit Shore and the rooms (entries 6 to 8) were made in another copy of the repo and reached
GitHub; Undead and Eclipse Caldera, in this one, without those commits. The two branches added stages to the wheel in
different ways and used the same model and texture names (`mod_f100`…, `tex_f100`…).

**Done**
- **An 11-slot wheel** (`analisis/parches.txt`): Sunlit Shore's way stays (Random last, the "random" marker outside
  the range of stages and the wheel textures with v divided by 1024 and 512), extended: slots 0-6 = stages 0-6, 7-9 =
  stages 9-11 (beach, Undead, Caldera) and 10 = Random. The picked Random is -1, the marker saved in the game data
  becomes 12 and random picks among the 10 stages (VS, arcade and COM vs COM). Going back to the wheel, stages 9-11
  lead to their slot; 7, 8 and the marker, to Random.
- The double-height textures with a marker (`textura_subir` and `u_esc` in the fragment shader) and the
  `g_rueda_cursor` hook are gone: the patch covers them.
- `tools/escenario/mods.mjs` builds everything: night, beach, Undead, Caldera and the lobby's letters
  (`tools/sala/letras.mjs`). `rueda()` (`comun.mjs`) builds the 16 rows of thumbnails and names and each stage paints
  its own. The new thumbnails take the alpha of the game's ones (rounded corners; before, Undead's and Caldera's were
  opaque).
- Undead moves to `mod_f140`-`mod_f149` and `tex_f140`-`tex_f155` (they clashed with the beach); `escribirMods`
  stops if there are repeated names.
- The translator stops if a C fragment from `parches.txt` or a hook doesn't land on a translated instruction.

**Tested on the PC**: the whole wheel (the beach, Undead, Caldera and Random in order, with their names and
thumbnails); fighting on all three; Random (it picked Caldera, with its columns); Outskirt Hill at night; going back
to the wheel from each one (pause → Exit → character select → wheel: the cursor stays on the stage played); the rooms
lobby. `npm test` (stages, patches and the rooms server).

**Tested on the emulator**, with the release APK (x86_64): the wheel and fighting on all three; Undead and Caldera at
60 fps, the beach at 51–53 (on the A52 it ran at 60).

**Not tested**: the phone (the A52 wasn't connected) and online play on stages 9-11 (both sides need this version).

<!-- i18n source=ESTADO.md sha256=b9ce7b0197ce -->
