<p align="right"><sub><b>English</b> · <a href="PLAN.md">Español</a></sub></p>

# Hinokakera Chaotic Eclipse → Android

Goal: the original game (v1.01, `hinokakera.exe` sha256 `d9aea156…c8ab`) on Android, identical to the original.

## Decision: static recompilation, not a rewrite

The game is a 3D fighter with 13 characters, story mode, AI, menus and networking: 7,934 functions and 362 thousand
x86 instructions of optimized C++. Rewriting it by hand means understanding and copying every mechanic, and any detail
copied wrong makes it different. If, instead, the original machine code is translated to C and compiled for ARM, the
original logic runs: every hit, combo, AI decision and frame comes out the same by construction. The only thing
written by hand is what lived outside the exe: the Windows DLLs (Direct3D 9, DirectInput, DirectSound, kernel32,
etc.), reimplemented on top of OpenGL ES, SDL and Android.

It's the technique of the N64/Xbox 360 recomps (N64Recomp, XenonRecomp), applied to a Windows exe.

## What's known about the exe (REA + my own disassembly, 2026-10-22)

- PE32 x86, fixed base 0x400000, no relocations. `.text` 1.35 MB, 7,934 functions (Ghidra 12.1.4 through REA).
- Built with **Visual C++ 6.0**: optimized game code (/O2), statically linked with MFC 4.2 and the **debug** CRT
  (that's why `config.dat` ends in `CD CD CD CD`). MFC is used as a library (CString, CFile, arrays); the WinMain
  (`0x49e4e0`) is pure Win32, not `AfxWinMain`.
- **x87** FPU throughout (no SSE: the only `movups` is garbage after an `exit`). 130 distinct mnemonics.
- 117 `switch` tables, 5,290 indirect calls (C++ vtables and COM), SEH through `fs:[0]` in a few functions.
- Imports: D3D9 (`Direct3DCreate9`), D3DX9_37 (math, `D3DXAssembleShader`, `D3DXCreateTextureFromFileInMemoryEx`),
  DINPUT8, DSOUND8, WINMM (`timeGetTime`, mmio), WS2_32 (UDP, netplay), GDI32 (`GetGlyphOutlineA`: text is drawn
  with Windows fonts), plus whatever MFC drags in.
- Shaders: 10 `vs_1_0` as text, assembled at run time (lighting with a toon ramp). Pixel: fixed stages.
- BGM in `bgm/*.ogg`, decoded by a libvorbis linked inside the exe.

## Data (`tools/dat1.mjs`)

An unencrypted `dat1` container: `"dat1"`, a u32 count and, per entry, a u32 length, the name, a u32 offset and a u32
size. `vsobj` (`BMT_`/`BMA_` textures, `nmd_` meshes), `vsdat` (`smd_` + `skl_` characters, messages), `vswav` (733
WAVs), `nv*` (visual novel: `ndat` scripts with the text bitwise NOT-ed). With recompilation there's no need to
understand these formats: the original code reads them just like on Windows.

## Stages

1. **Translator** (`recomp/`, Node + iced-x86): exe → C, one C function per x86 function. A differential test of every
   instruction form against the real CPU (a 32-bit x86 harness with MSVC).
2. **Runtime** (`runtime/`, C): the guest's 4 GB of memory, CPU context, x87, dispatch of indirect calls, threads,
   heap. It boots the CRT up to WinMain.
3. **Test Windows build**: the recompiled game running on this PC with SDL + OpenGL, to compare frame by frame against
   the original exe (captures in the same scenes, recorded replays).
4. **Direct3D 9 → OpenGL ES 3**, DirectSound → SDL mixer, DirectInput → keyboard/gamepad/touch, GDI → glyphs
   pre-rendered with the real GDI (identical text).
5. **Android**: NDK + SDL, data in the APK, touch controls, full screen. Emulator and a real phone.

## "Pixel by pixel" verification

- Instruction semantics: differential against the real CPU.
- Logic: the original's replays (`replay/`) have to play back the same in the port.
- Image: captures of the original and of the port in the same scene, per-pixel difference.

<!-- i18n source=PLAN.md sha256=b5c0e05201ba -->
