// Compila el APK con el entorno Android compartido de Box (_scratch/pacmini-android: JDK 21, SDK con NDK y
// CMake, Gradle 8.11.1 y su caché). Todo lo temporal va a Box/_scratch/hino-android.
// Uso: node tools/apk.mjs [--abis x86_64] [--datos] [--release]
//   --datos   mete el juego en el APK (sin eso sale chico, sin el juego)
import { spawn, spawnSync } from 'node:child_process';
import { readFileSync, mkdirSync, existsSync, statSync, createWriteStream } from 'node:fs';
import { join } from 'node:path';
import { BOX, SCRATCH, prepararDatos } from './datos.mjs';

const RAIZ = new URL('..', import.meta.url).pathname.replace(/^\/(\w:)/, '$1');
const tc = JSON.parse(readFileSync(join(BOX, '_scratch/pacmini-android/toolchain.json'), 'utf8'));
const arg = (n) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : null; };
const abis = arg('--abis') || 'arm64-v8a,x86_64';
const release = process.argv.includes('--release');
const conDatos = process.argv.includes('--datos');

const dirs = { tmp: join(SCRATCH, 'tmp'), logs: join(SCRATCH, 'logs'), user: join(SCRATCH, 'android-user'), java: join(SCRATCH, 'java-user') };
for (const d of Object.values(dirs)) mkdirSync(d, { recursive: true });
const env = {
  ...process.env, TEMP: dirs.tmp, TMP: dirs.tmp, JAVA_HOME: tc.javaRoot, ANDROID_HOME: tc.sdkRoot, ANDROID_SDK_ROOT: tc.sdkRoot,
  ANDROID_USER_HOME: dirs.user, GRADLE_USER_HOME: join(BOX, '_scratch/pacmini-android/gradle'),
  JAVA_TOOL_OPTIONS: `-Djava.io.tmpdir=${dirs.tmp} -Duser.home=${dirs.java}`,
};

const clave = join(RAIZ, 'android/hino.keystore');
if (!existsSync(clave)) {
  const r = spawnSync(join(tc.javaRoot, 'bin/keytool.exe'), ['-genkeypair', '-keystore', clave, '-storepass', 'android', '-keypass', 'android',
    '-alias', 'hino', '-keyalg', 'RSA', '-keysize', '2048', '-validity', '20000', '-dname', 'CN=Hinokakera CE port, C=AR', '-noprompt'],
    { env, windowsHide: true, encoding: 'utf8' });
  if (r.status) throw new Error(`keytool: ${r.stderr}`);
}

const tarea = release ? 'assembleRelease' : 'assembleDebug';
const args = ['-cp', tc.gradleMain, 'org.gradle.launcher.GradleMain', '-p', join(RAIZ, 'android'),
  '--project-cache-dir', join(SCRATCH, 'project-cache'), '--no-daemon', '--console=plain', tarea, `-Pabis=${abis}`,
  ...(conDatos ? [`-Pdatos=${prepararDatos()}`] : [])];
const log = join(dirs.logs, 'gradle.log');
const t0 = Date.now();
const salida = createWriteStream(log);
const p = spawn(tc.javaExe, args, { env, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
let cola = '';
for (const s of [p.stdout, p.stderr]) s.on('data', (d) => { salida.write(d); cola = (cola + d).slice(-6000); });
const code = await new Promise((ok) => p.on('exit', ok));
salida.end();
if (code) { console.error(cola); console.error(`gradle salió con ${code} (log completo: ${log})`); process.exit(1); }
const tipo = release ? 'release' : 'debug';
const apk = join(SCRATCH, `build/app/outputs/apk/${tipo}/app-${tipo}.apk`);
console.log(`listo en ${((Date.now() - t0) / 1000).toFixed(0)} s: ${apk} (${(statSync(apk).size / 1048576).toFixed(1)} MB)`);
