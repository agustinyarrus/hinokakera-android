// d3d9.dll espía para el juego ORIGINAL (32 bits): se pone en la carpeta de una copia del juego junto a
// d3d9_real.dll (copia de la del sistema). Reenvía todo y, en cada Present, vuelca el backbuffer de los
// cuadros pedidos: así se compara contra el port lo que Direct3D dibujó, sin ventana, DPI ni DWM de por medio.
// Variables: HINO_CAPTURAS="30,120,600" y HINO_SALIDA=carpeta (por defecto la del juego).
#define COBJMACROS
#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static HRESULT(WINAPI *crear_device_real)(IDirect3D9 *, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS *, IDirect3DDevice9 **);
static HRESULT(WINAPI *present_real)(IDirect3DDevice9 *, const RECT *, const RECT *, HWND, const RGNDATA *);
static unsigned cuadro;

static int pedido(unsigned n) {
  const char *l = getenv("HINO_CAPTURAS");
  char b[16];
  if (!l) return 0;
  snprintf(b, sizeof b, "%u", n);
  for (const char *p = l; (p = strstr(p, b)); p++)
    if ((p == l || p[-1] == ',') && (p[strlen(b)] == ',' || !p[strlen(b)])) return 1;
  return 0;
}

static void volcar(IDirect3DDevice9 *d, unsigned n) {
  IDirect3DSurface9 *bb = NULL, *mem = NULL;
  D3DSURFACE_DESC desc;
  if (FAILED(IDirect3DDevice9_GetBackBuffer(d, 0, 0, D3DBACKBUFFER_TYPE_MONO, &bb))) return;
  IDirect3DSurface9_GetDesc(bb, &desc);
  if (SUCCEEDED(IDirect3DDevice9_CreateOffscreenPlainSurface(d, desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &mem, NULL)) &&
      SUCCEEDED(IDirect3DDevice9_GetRenderTargetData(d, bb, mem))) {
    D3DLOCKED_RECT lr;
    if (SUCCEEDED(IDirect3DSurface9_LockRect(mem, &lr, NULL, D3DLOCK_READONLY))) {
      char ruta[MAX_PATH];
      const char *dir = getenv("HINO_SALIDA");
      snprintf(ruta, sizeof ruta, "%s%scuadro_%05u.bmp", dir ? dir : "", dir ? "\\" : "", n);
      int w = (int)desc.Width, h = (int)desc.Height, pitch = (w * 3 + 3) & ~3;
      BITMAPINFOHEADER bi = {sizeof bi, w, h, 1, 24, BI_RGB};
      BITMAPFILEHEADER fh = {0x4d42, (DWORD)(sizeof fh + sizeof bi + pitch * h), 0, 0, sizeof fh + sizeof bi};
      FILE *f = fopen(ruta, "wb");
      if (f) {
        fwrite(&fh, sizeof fh, 1, f);
        fwrite(&bi, sizeof bi, 1, f);
        unsigned char *fila = calloc(1, (size_t)pitch);
        for (int y = h - 1; y >= 0; y--) {
          const unsigned char *s = (const unsigned char *)lr.pBits + (size_t)y * lr.Pitch;
          for (int x = 0; x < w; x++) { fila[3 * x] = s[4 * x]; fila[3 * x + 1] = s[4 * x + 1]; fila[3 * x + 2] = s[4 * x + 2]; }
          fwrite(fila, 1, (size_t)pitch, f);
        }
        free(fila);
        fclose(f);
      }
      IDirect3DSurface9_UnlockRect(mem);
    }
  }
  if (mem) IDirect3DSurface9_Release(mem);
  IDirect3DSurface9_Release(bb);
}

// Para el espía de dinput8: cuántos Present van (exportada como HinoCuadro).
unsigned __cdecl HinoCuadro(void) { return cuadro; }

static HRESULT WINAPI mi_present(IDirect3DDevice9 *d, const RECT *a, const RECT *b, HWND h, const RGNDATA *r) {
  cuadro++;
  if (pedido(cuadro)) volcar(d, cuadro);
  return present_real(d, a, b, h, r);
}

static void parchear(void **vtabla, int i, void *nuevo, void **viejo) {
  DWORD p;
  VirtualProtect(&vtabla[i], sizeof(void *), PAGE_EXECUTE_READWRITE, &p);
  if (!*viejo) *viejo = vtabla[i];
  vtabla[i] = nuevo;
  VirtualProtect(&vtabla[i], sizeof(void *), p, &p);
}

static HRESULT WINAPI mi_crear_device(IDirect3D9 *d3d, UINT a, D3DDEVTYPE t, HWND h, DWORD f, D3DPRESENT_PARAMETERS *pp, IDirect3DDevice9 **out) {
  HRESULT r = crear_device_real(d3d, a, t, h, f, pp, out);
  if (SUCCEEDED(r)) parchear(*(void ***)*out, 17, (void *)mi_present, (void **)&present_real);
  return r;
}

// El resto de las exportaciones (las importa d3dx9_37) son de perfilado: se contestan acá para no cargar la
// d3d9 real dentro del cargador al arrancar (con reenvíos el juego se quedaba trabado antes de WinMain).
int WINAPI D3DPERF_BeginEvent(DWORD c, LPCWSTR n) { (void)c; (void)n; return 0; }
int WINAPI D3DPERF_EndEvent(void) { return 0; }
DWORD WINAPI D3DPERF_GetStatus(void) { return 0; }
BOOL WINAPI D3DPERF_QueryRepeatFrame(void) { return FALSE; }
void WINAPI D3DPERF_SetMarker(DWORD c, LPCWSTR n) { (void)c; (void)n; }
void WINAPI D3DPERF_SetOptions(DWORD o) { (void)o; }
void WINAPI D3DPERF_SetRegion(DWORD c, LPCWSTR n) { (void)c; (void)n; }

static void bitacora(const char *s) {
  FILE *f = fopen("d3d9_espia.log", "a");
  if (f) { fprintf(f, "%s\n", s); fclose(f); }
}

IDirect3D9 *WINAPI Direct3DCreate9(UINT v) {
  bitacora("Direct3DCreate9");
  HMODULE real = LoadLibraryA("d3d9_real.dll");
  bitacora(real ? "d3d9_real cargada" : "no cargó d3d9_real");
  IDirect3D9 *(WINAPI *f)(UINT) = real ? (void *)GetProcAddress(real, "Direct3DCreate9") : NULL;
  IDirect3D9 *d = f ? f(v) : NULL;
  bitacora(d ? "IDirect3D9 creado" : "Direct3DCreate9 real falló");
  if (d) parchear(*(void ***)d, 16, (void *)mi_crear_device, (void **)&crear_device_real);
  return d;
}
