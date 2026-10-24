#!/bin/sh
# Compila y corre el port en Windows con los datos del juego y los escenarios de tools/escenario (mods/), de solo
# lectura, y una carpeta de escritura propia. Sin mods/vsobj2.dat (node tools/escenario/playa.mjs) la rueda de
# escenarios parcheada no tiene la textura ni el escenario de la playa.
cd "$(dirname "$0")/.." || exit 1
salida=$(node tools/compilar.mjs "$@" 2>&1) || { echo "$salida" | grep -v "^\s*$" | tail -40; exit 1; }
echo "$salida" | tail -1
datos=$(node -e "import('./tools/datos.mjs').then(m => console.log(m.prepararDatos()))" | tail -1)
timeout ${TIEMPO:-60} build/win${RASTRO:+-rastro}/hinokakera.exe \
  --datos "$datos" \
  --escritura run/escritura 2>&1 | head -${LINEAS:-60}
