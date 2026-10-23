#!/bin/sh
# Compila y corre el port en Windows con los datos originales (solo lectura) y una carpeta de escritura propia.
cd "$(dirname "$0")/.." || exit 1
salida=$(node tools/compilar.mjs "$@" 2>&1) || { echo "$salida" | grep -v "^\s*$" | tail -40; exit 1; }
echo "$salida" | tail -1
timeout ${TIEMPO:-60} build/win${RASTRO:+-rastro}/hinokakera.exe \
  --datos "${HINO_JUEGO:-../hinokakera/04_Hinokakera_Chaotic_Eclipse_2010/Game_Chaotic_Eclipse_v101_Unpacked}" \
  --escritura run/escritura 2>&1 | head -${LINEAS:-60}
