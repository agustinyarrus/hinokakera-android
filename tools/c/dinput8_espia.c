// dinput8.dll espía para el juego ORIGINAL (32 bits), junto al d3d9.dll espía y a dinput8_real.dll (copia de
// la del sistema). Aplica al teclado el mismo guion que el port (HINO_TECLAS="cuadro:dik:1|0,..."), contando
// cuadros con el Present del espía de d3d9: así los dos recorren las mismas pantallas.
#define DIRECTINPUT_VERSION 0x0800
#define COBJMACROS
#include <windows.h>
#include <dinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static HRESULT(WINAPI *crear_device_real)(IDirectInput8A *, REFGUID, LPDIRECTINPUTDEVICE8A *, LPUNKNOWN);
static HRESULT(WINAPI *estado_real)(IDirectInputDevice8A *, DWORD, LPVOID);
static unsigned (*cuadro_d3d9)(void);

// Estado de cada tecla según el guion hasta el cuadro dado (la última entrada que ya pasó manda).
static void aplicar(unsigned cuadro, BYTE *teclas) {
  const char *g = getenv("HINO_TECLAS");
  for (const char *p = g; p && *p;) {
    unsigned n, dik, v;
    if (sscanf(p, "%u:%x:%u", &n, &dik, &v) == 3 && n <= cuadro && dik < 256) teclas[dik] = v ? 0x80 : 0;
    p = strchr(p, ',');
    if (p) p++;
  }
}

static HRESULT WINAPI mi_estado(IDirectInputDevice8A *d, DWORD n, LPVOID datos) {
  HRESULT r = estado_real(d, n, datos);
  if (SUCCEEDED(r) && n == 256) {
    if (!cuadro_d3d9) cuadro_d3d9 = (unsigned (*)(void))GetProcAddress(GetModuleHandleA("d3d9.dll"), "HinoCuadro");
    BYTE guion[256] = {0};
    aplicar(cuadro_d3d9 ? cuadro_d3d9() : 0, guion);
    for (int i = 0; i < 256; i++) ((BYTE *)datos)[i] |= guion[i];
  }
  return r;
}

static void parchear(void **vtabla, int i, void *nuevo, void **viejo) {
  DWORD p;
  if (vtabla[i] == nuevo) return;
  VirtualProtect(&vtabla[i], sizeof(void *), PAGE_EXECUTE_READWRITE, &p);
  if (!*viejo) *viejo = vtabla[i];
  vtabla[i] = nuevo;
  VirtualProtect(&vtabla[i], sizeof(void *), p, &p);
}

// La ventana que lanza la herramienta de captura no siempre tiene el foco: con DISCL_FOREGROUND DirectInput
// no entrega nada. Se pide siempre en segundo plano y no exclusivo.
static HRESULT(WINAPI *cooperar_real)(IDirectInputDevice8A *, HWND, DWORD);
static HRESULT WINAPI mi_cooperar(IDirectInputDevice8A *d, HWND h, DWORD f) {
  (void)f;
  return cooperar_real(d, h, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE);
}

static HRESULT WINAPI mi_crear_device(IDirectInput8A *di, REFGUID g, LPDIRECTINPUTDEVICE8A *out, LPUNKNOWN u) {
  HRESULT r = crear_device_real(di, g, out, u);
  if (SUCCEEDED(r)) {
    parchear(*(void ***)*out, 9, (void *)mi_estado, (void **)&estado_real);
    parchear(*(void ***)*out, 13, (void *)mi_cooperar, (void **)&cooperar_real);
  }
  return r;
}

HRESULT WINAPI DirectInput8Create(HINSTANCE h, DWORD v, REFIID iid, LPVOID *out, LPUNKNOWN u) {
  HMODULE real = LoadLibraryA("dinput8_real.dll");
  HRESULT(WINAPI * f)(HINSTANCE, DWORD, REFIID, LPVOID *, LPUNKNOWN) = real ? (void *)GetProcAddress(real, "DirectInput8Create") : NULL;
  if (!f) return E_FAIL;
  HRESULT r = f(h, v, iid, out, u);
  if (SUCCEEDED(r)) parchear(*(void ***)*out, 3, (void *)mi_crear_device, (void **)&crear_device_real);
  return r;
}
