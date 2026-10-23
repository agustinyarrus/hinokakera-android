// Estado de la CPU x86 del invitado y acceso a su memoria. Lo incluye todo el código generado.
#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>

typedef struct Ctx {
  uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
  uint32_t cf, zf, sf, of, pf; // solo cuando cruzan llamadas (rutinas en ensamblador del CRT)
  uint32_t df;
  uint32_t fs;                 // base del TEB del hilo (fs:[x])
  double st[8];                // pila x87 (registros en double: alcanza con precisión de 24 y 53 bits)
  uint32_t top;
  uint16_t fcw, fsw;           // palabra de control; de la de estado solo C0-C3 y excepciones
  uint32_t pc24;               // precisión de 24 bits activa (Direct3D la pone así al crear el device)
  struct Hilo *hilo;
  uint32_t eip_dbg;             // última instrucción marcada (depuración)
} Ctx;

extern uint8_t *MEM; // base de los 4 GB del invitado

typedef uint16_t __attribute__((aligned(1), may_alias)) u16u;
typedef uint32_t __attribute__((aligned(1), may_alias)) u32u;
typedef uint64_t __attribute__((aligned(1), may_alias)) u64u;
typedef float __attribute__((aligned(1), may_alias)) f32u;
typedef double __attribute__((aligned(1), may_alias)) f64u;

#define M8(a) (*(uint8_t *)(mem + (uint32_t)(a)))
#define M16(a) (*(u16u *)(mem + (uint32_t)(a)))
#define M32(a) (*(u32u *)(mem + (uint32_t)(a)))
#define M64(a) (*(u64u *)(mem + (uint32_t)(a)))
#define MF32(a) (*(f32u *)(mem + (uint32_t)(a)))
#define MF64(a) (*(f64u *)(mem + (uint32_t)(a)))
#define PARIDAD(x) (!__builtin_parity((x) & 0xffu))

// Con -DRASTRO cada hilo lleva la pila de funciones del invitado, para los informes de fallas.
#ifdef RASTRO
typedef struct { Ctx *c; } Rastro;
Rastro rastro_entrar(Ctx *c, uint32_t va);
void rastro_salir(Rastro *r);
#define RASTREAR(va) __attribute__((cleanup(rastro_salir))) Rastro rastro_ = rastro_entrar(c, va)
#else
#define RASTREAR(va) (void)0
#endif
#ifdef RASTRO
#define EIP_DBG(x) (c->eip_dbg = (x), c->esp = esp, c->ebp = ebp)
// Cada función recuerda la dirección de retorno que dejó el que llamó; un ret que saca otra cosa delata un
// desajuste de pila (se informa una vez por lugar).
void aviso_ret(Ctx *c, uint32_t va, uint32_t esperado, uint32_t visto);
#define RET_ESPERADO uint32_t ret_esperado_ = *(u32u *)(MEM + c->esp)
#define VERIFICAR_RET(va) do { if (M32(esp) != ret_esperado_) aviso_ret(c, va, ret_esperado_, M32(esp)); } while (0)
#else
#define EIP_DBG(x) (void)0
#define RET_ESPERADO (void)0
#define VERIFICAR_RET(va) (void)0
#endif

// Registros en variables locales: el compilador los mantiene en registros de la máquina anfitriona.
#define ENTRADA_FUNCION(va)                                                                               \
  RASTREAR(va);                                                                                           \
  RET_ESPERADO;                                                                                           \
  uint8_t *const mem = MEM;                                                                               \
  uint32_t eax = c->eax, ecx = c->ecx, edx = c->edx, ebx = c->ebx, esp = c->esp, ebp = c->ebp,            \
           esi = c->esi, edi = c->edi;                                                                    \
  uint32_t cf = 0, zf = 0, sf = 0, of = 0, pf = 0;                                                        \
  (void)mem; (void)cf; (void)zf; (void)sf; (void)of; (void)pf
#define SALIDA (c->eax = eax, c->ecx = ecx, c->edx = edx, c->ebx = ebx, c->esp = esp, c->ebp = ebp, c->esi = esi, c->edi = edi)
#define ENTRADA (eax = c->eax, ecx = c->ecx, edx = c->edx, ebx = c->ebx, esp = c->esp, ebp = c->ebp, esi = c->esi, edi = c->edi)

typedef void (*FuncionGen)(Ctx *);
typedef struct { uint32_t va; FuncionGen fn; } EntradaTabla;
typedef struct { uint32_t iat; const char *dll, *nombre; } Importacion;
extern const EntradaTabla TABLA_FUNCIONES[];
extern const uint32_t N_FUNCIONES;
extern const Importacion IMPORTACIONES[];
extern const uint32_t N_IMPORTACIONES;
extern const uint32_t ENTRADA_PE, TAM_IMAGEN;

void despachar(Ctx *c, uint32_t destino);
void traza(Ctx *c, uint32_t va, uint32_t esp, uint32_t eax, uint32_t ecx, uint32_t edx);
_Noreturn void fallar(Ctx *c, uint32_t va, const char *motivo);

// ---- x87 ----
#define ST(i) (c->st[(c->top + (i)) & 7])
static inline void fpush(Ctx *c, double v) { c->top = (c->top - 1) & 7; c->st[c->top] = v; }
static inline void fpop(Ctx *c) { c->top = (c->top + 1) & 7; }
// Con precisión de 24 bits el x87 redondea cada operación a la mantisa de un float.
static inline double fr(Ctx *c, double v) { return c->pc24 ? (double)(float)v : v; }
static inline uint16_t fsw(Ctx *c) { return (uint16_t)((c->fsw & ~0x3800u) | ((c->top & 7) << 11)); }
void fcomparar(Ctx *c, double a, double b);
void fxam(Ctx *c);
void fldcw(Ctx *c, uint16_t cw);
void fninit(Ctx *c);
int16_t fist16(Ctx *c, double v);
int32_t fist32(Ctx *c, double v);
int64_t fist64(Ctx *c, double v);
double frndint(Ctx *c, double v);
void fprem(Ctx *c, int ieee);
double leer_f80(uint32_t ea);
void escribir_f80(uint32_t ea, double v);
void fnsave(Ctx *c, uint32_t ea);
void frstor(Ctx *c, uint32_t ea);
void fnstenv(Ctx *c, uint32_t ea);
void fldenv(Ctx *c, uint32_t ea);
