// Probar en el emulador (el AVD "celu" del arnés de subibaja: node ../subibaja/tools/emu.mjs start).
// Uso: node tools/android.mjs <comando>
//   instalar [apk]          instala el APK de debug recién compilado (con el juego: node tools/apk.mjs --datos)
//   entorno [K=V ...]       variables de prueba (entorno.txt); sin argumentos lo borra
//   correr [segundos]       lanza la app, espera y muestra lo que escribió en logcat
//   bajar <dir>             trae las capturas (cuadro_*.bmp) de la carpeta de escritura de prueba
//   pantalla <png>          captura de la pantalla del emulador
import * as emu from '../../subibaja/tools/emu.mjs';
import { writeFileSync, mkdirSync } from 'node:fs';
import { join } from 'node:path';
import { SCRATCH } from './datos.mjs';

const PKG = 'ar.box.hinokakera';
const EXT = `/sdcard/Android/data/${PKG}/files`;
// Escritura de prueba en el almacenamiento externo, así adb la lee sin run-as (los .bmp de HINO_CAPTURAS van ahí).
export const ESCRITURA = `/storage/emulated/0/Android/data/${PKG}/files/escritura`;
const [cmd, ...a] = process.argv.slice(2);
const ok = (r) => { if (r.code) throw new Error((r.text + r.errText).trim()); return r.text.trim(); };

switch (cmd) {
  case 'instalar':
    console.log(await emu.install(a[0] || join(SCRATCH, 'build/app/outputs/apk/debug/app-debug.apk')));
    break;
  case 'entorno': {
    if (!a.length) { await emu.sh(`rm -f ${EXT}/entorno.txt`); console.log('entorno borrado'); break; }
    const f = join(SCRATCH, 'tmp/entorno.txt');
    mkdirSync(join(SCRATCH, 'tmp'), { recursive: true });
    writeFileSync(f, a.map((x) => x.replace('{escritura}', ESCRITURA)).join('\n') + '\n');
    console.log(ok(await emu.adb(['push', f, `${EXT}/entorno.txt`])));
    break;
  }
  case 'correr': {
    await emu.stopApp(PKG);
    await emu.logcat({ clear: true });
    console.log(JSON.stringify(await emu.launch(PKG)));
    await new Promise((r) => setTimeout(r, (+a[0] || 10) * 1000));
    const pid = await emu.appPid(PKG);
    const log = await emu.logcat({ all: true, lines: 4000 });
    const mio = log.split('\n').filter((l) => /\shino\s*:|SDL|DEBUG\s*:|libc\s*:|AndroidRuntime|F\s+DEBUG|crash|tombstone/i.test(l));
    console.log(mio.slice(-(+a[1] || 120)).join('\n'));
    console.log(pid ? `sigue vivo (pid ${pid})` : 'el proceso terminó');
    break;
  }
  case 'bajar': {
    mkdirSync(a[0], { recursive: true });
    console.log(ok(await emu.adb(['pull', `${EXT}/escritura/.`, a[0]], { timeout: 120000 })).split('\n').pop());
    break;
  }
  case 'pantalla':
    await emu.screenshot(a[0]);
    console.log(a[0]);
    break;
  default:
    console.log('comandos: instalar | entorno [K=V ...] | correr [s] | bajar <dir> | pantalla <png>');
}
