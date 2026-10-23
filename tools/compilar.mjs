// Compila el port para Windows (prueba en la PC) con el clang de Visual Studio y SDL3.
// Uso: node tools/compilar.mjs [--rastro] [--O1]   -> build/win[-rastro]/hinokakera.exe
import { readdirSync, statSync, mkdirSync, copyFileSync, existsSync } from 'node:fs';
import { join, basename } from 'node:path';
import { spawn } from 'node:child_process';
import { availableParallelism } from 'node:os';

const raiz = new URL('..', import.meta.url).pathname.replace(/^\/(\w:)/, '$1');
const CLANG = 'C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/Llvm/x64/bin/clang.exe';
const SDL = join(raiz, 'terceros/sdl3-vc/SDL3-3.4.18');
const rastro = process.argv.includes('--rastro');
const cacheGlifos = process.argv.includes('--cache-glifos'); // texto desde glifos.bin como en Android
const opt = process.argv.includes('--O1') ? '-O1' : '-O2';
const sufijo = (rastro ? 'win-rastro' : 'win') + (cacheGlifos ? '-cache' : '');
const objs = join(raiz, 'obj', sufijo), salida = join(raiz, 'build', sufijo);
mkdirSync(objs, { recursive: true });
mkdirSync(salida, { recursive: true });

const fuentes = [
  ...readdirSync(join(raiz, 'gen')).filter(a => a.endsWith('.c')).map(a => join(raiz, 'gen', a)),
  ...['runtime', 'win32', 'd3d'].flatMap(d => readdirSync(join(raiz, d)).filter(a => a.endsWith('.c')).map(a => join(raiz, d, a))),
];
const cabeceras = ['runtime', 'win32', 'd3d'].flatMap(d => readdirSync(join(raiz, d)).filter(a => a.endsWith('.h')).map(a => join(raiz, d, a)));
const masNueva = Math.max(...cabeceras.map(h => statSync(h).mtimeMs), statSync(join(raiz, 'gen', 'funciones.h')).mtimeMs);
const flags = [opt, '-g', '-w', '-fwrapv', '-fno-strict-aliasing', '-D_CRT_SECURE_NO_WARNINGS', `-I${join(SDL, 'include')}`, ...(rastro ? ['-DRASTRO'] : []), ...(cacheGlifos ? ['-DHINO_CACHE_GLIFOS'] : [])];

const correr = args => new Promise((ok, mal) => {
  const p = spawn(CLANG, args, { stdio: ['ignore', 'inherit', 'inherit'] });
  p.on('exit', code => code === 0 ? ok() : mal(new Error(`clang ${args.at(-1)} salió con ${code}`)));
});

const pendientes = fuentes.filter(f => {
  const o = join(objs, basename(f).replace(/\.c$/, '.o'));
  return !existsSync(o) || statSync(o).mtimeMs < Math.max(statSync(f).mtimeMs, masNueva);
});
const t0 = Date.now();
let hechos = 0;
const cola = [...pendientes];
await Promise.all(Array.from({ length: availableParallelism() }, async () => {
  while (cola.length) {
    const f = cola.shift();
    await correr([...flags, '-c', f, '-o', join(objs, basename(f).replace(/\.c$/, '.o'))]);
    hechos++;
  }
}));
console.log(`compilados ${hechos}/${fuentes.length} en ${((Date.now() - t0) / 1000).toFixed(1)} s`);

const exe = join(salida, 'hinokakera.exe');
await correr(['-fuse-ld=lld', '-g', ...fuentes.map(f => join(objs, basename(f).replace(/\.c$/, '.o'))),
  `-L${join(SDL, 'lib/x64')}`, '-lSDL3', '-lws2_32', '-lgdi32', '-Wl,/STACK:67108864', '-Wl,/SUBSYSTEM:CONSOLE', '-o', exe]);
copyFileSync(join(SDL, 'lib/x64/SDL3.dll'), join(salida, 'SDL3.dll'));
copyFileSync(join(raiz, 'gen', 'imagen.bin'), join(salida, 'imagen.bin'));
if (existsSync(join(raiz, 'gen', 'glifos.bin'))) copyFileSync(join(raiz, 'gen', 'glifos.bin'), join(salida, 'glifos.bin'));
console.log(`listo: ${exe} (${((Date.now() - t0) / 1000).toFixed(1)} s)`);
