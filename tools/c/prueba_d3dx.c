// Prueba de referencia (32 bits): genera una textura con mipmaps usando la d3dx9_37.dll real y guarda cada
// nivel crudo (BGRA). Así se compara con la generación de mipmaps del port.
// Uso: prueba_d3dx.exe <imagen.bmp de 32 bits> <prefijo-salida>   -> <prefijo>_nN.raw
#define COBJMACROS
#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
#include <stdlib.h>

typedef HRESULT(WINAPI *CrearTex)(IDirect3DDevice9 *, LPCVOID, UINT, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, DWORD, DWORD,
                                  D3DCOLOR, void *, void *, IDirect3DTexture9 **);

int main(int argc, char **argv) {
  if (argc < 3) return 2;
  FILE *f = fopen(argv[1], "rb");
  if (!f) return 1;
  fseek(f, 0, SEEK_END);
  long tam = ftell(f);
  fseek(f, 0, SEEK_SET);
  void *datos = malloc(tam);
  fread(datos, 1, tam, f);
  fclose(f);
  HWND w = CreateWindowA("STATIC", "prueba", WS_POPUP, 0, 0, 64, 64, NULL, NULL, NULL, NULL);
  IDirect3D9 *d3d = Direct3DCreate9(D3D_SDK_VERSION);
  D3DPRESENT_PARAMETERS pp = {0};
  pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.BackBufferFormat = D3DFMT_UNKNOWN; pp.hDeviceWindow = w;
  IDirect3DDevice9 *dev;
  if (FAILED(IDirect3D9_CreateDevice(d3d, 0, D3DDEVTYPE_HAL, w, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev))) { puts("sin device"); return 1; }
  HMODULE x = LoadLibraryA("d3dx9_37.dll");
  CrearTex crear = x ? (CrearTex)GetProcAddress(x, "D3DXCreateTextureFromFileInMemoryEx") : NULL;
  if (!crear) { puts("sin d3dx9_37"); return 1; }
  IDirect3DTexture9 *t;
  HRESULT r = crear(dev, datos, (UINT)tam, 64, 64, 4, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, 3, 5, 0, NULL, NULL, &t);
  if (FAILED(r)) { printf("D3DX falló %08lx\n", r); return 1; }
  for (UINT n = 0; n < IDirect3DTexture9_GetLevelCount(t); n++) {
    D3DSURFACE_DESC d;
    D3DLOCKED_RECT lr;
    IDirect3DTexture9_GetLevelDesc(t, n, &d);
    IDirect3DTexture9_LockRect(t, n, &lr, NULL, D3DLOCK_READONLY);
    char ruta[MAX_PATH];
    snprintf(ruta, sizeof ruta, "%s_n%u.raw", argv[2], n);
    FILE *o = fopen(ruta, "wb");
    for (UINT y = 0; y < d.Height; y++) fwrite((char *)lr.pBits + y * lr.Pitch, 4, d.Width, o);
    fclose(o);
    IDirect3DTexture9_UnlockRect(t, n);
    printf("nivel %u: %ux%u\n", n, d.Width, d.Height);
  }
  return 0;
}
