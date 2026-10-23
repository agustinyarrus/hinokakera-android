// Corre el exe original, espera su ventana y guarda el área cliente como BMP (para comparar con el port).
// Uso: capturar_original.exe <exe> <carpeta> <ms> <salida.bmp> [<ms> <salida.bmp> ...]
// Los ms se cuentan desde que aparece la ventana. Al final cierra el proceso.
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static DWORD pid;
static HWND encontrada;
static BOOL CALLBACK buscar(HWND h, LPARAM l) {
  DWORD p;
  GetWindowThreadProcessId(h, &p);
  if (p == pid && IsWindowVisible(h)) { encontrada = h; return FALSE; }
  return TRUE;
}

static int guardar(HWND h, const char *ruta) {
  RECT r;
  GetClientRect(h, &r);
  int w = r.right, alto = r.bottom;
  HDC dc = GetDC(h), m = CreateCompatibleDC(dc);
  HBITMAP b = CreateCompatibleBitmap(dc, w, alto);
  SelectObject(m, b);
  if (!PrintWindow(h, m, 3)) BitBlt(m, 0, 0, w, alto, dc, 0, 0, SRCCOPY); // PW_CLIENTONLY | PW_RENDERFULLCONTENT
  BITMAPINFOHEADER bi = {sizeof bi, w, alto, 1, 24, BI_RGB};
  int pitch = (w * 3 + 3) & ~3;
  unsigned char *px = malloc((size_t)pitch * alto);
  GetDIBits(m, b, 0, alto, px, (BITMAPINFO *)&bi, DIB_RGB_COLORS);
  BITMAPFILEHEADER fh = {0x4d42, (DWORD)(sizeof fh + sizeof bi + pitch * alto), 0, 0, sizeof fh + sizeof bi};
  FILE *f = fopen(ruta, "wb");
  if (f) { fwrite(&fh, sizeof fh, 1, f); fwrite(&bi, sizeof bi, 1, f); fwrite(px, 1, (size_t)pitch * alto, f); fclose(f); }
  free(px);
  DeleteObject(b); DeleteDC(m); ReleaseDC(h, dc);
  printf("%s: %dx%d\n", ruta, w, alto);
  return f != NULL;
}

int main(int argc, char **argv) {
  if (argc < 5 || (argc - 3) % 2) { fprintf(stderr, "uso: %s <exe> <carpeta> <ms> <salida.bmp> ...\n", argv[0]); return 2; }
  STARTUPINFOA si = {sizeof si};
  PROCESS_INFORMATION pi;
  if (!CreateProcessA(argv[1], NULL, NULL, NULL, FALSE, 0, NULL, argv[2], &si, &pi)) { fprintf(stderr, "no arrancó (%lu)\n", GetLastError()); return 1; }
  pid = pi.dwProcessId;
  for (int i = 0; i < 300 && !encontrada; i++) { Sleep(100); EnumWindows(buscar, 0); }
  if (!encontrada) { fprintf(stderr, "no apareció la ventana\n"); TerminateProcess(pi.hProcess, 1); return 1; }
  DWORD t0 = GetTickCount();
  for (int a = 3; a + 1 < argc; a += 2) {
    DWORD ms = (DWORD)atoi(argv[a]);
    while (GetTickCount() - t0 < ms) Sleep(5);
    guardar(encontrada, argv[a + 1]);
  }
  TerminateProcess(pi.hProcess, 0);
  return 0;
}
