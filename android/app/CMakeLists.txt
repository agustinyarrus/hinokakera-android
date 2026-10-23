cmake_minimum_required(VERSION 3.22)
project(hinokakera C)

find_package(SDL3 REQUIRED CONFIG)

set(RAIZ ${CMAKE_CURRENT_SOURCE_DIR}/../..)
file(GLOB FUENTES CONFIGURE_DEPENDS ${RAIZ}/gen/*.c ${RAIZ}/runtime/*.c ${RAIZ}/win32/*.c ${RAIZ}/d3d/*.c)
add_library(main SHARED ${FUENTES})
# Las mismas banderas que en Windows (tools/compilar.mjs), más dos por ARM: -ffp-contract=off porque con FMA
# a*b+c redondea distinto que el x87 emulado (y la pelea depende de eso), y -fsigned-char como en x86.
target_compile_options(main PRIVATE -O2 -w -fwrapv -fno-strict-aliasing -ffp-contract=off -fsigned-char)
target_link_libraries(main PRIVATE SDL3::SDL3 android log)
