/* Support code the generated C relies on. */
#pragma once
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <setjmp.h>

#ifdef _MSC_VER
#pragma warning(disable: 4005 4013 4018 4024 4028 4033 4047 4090 4100 4101 4102 4113 4133 4146 4189 4200 4244 4267 4305 4306 4307 4308 4309 4310 4312 4389 4554 4700 4715 4716 4723 4756 4789)
#define TOS_TLS __declspec(thread)
#if defined(_M_IX86) && defined(TOS_NOCRT)   /* call rt_libc.c's x87 math, not the CRT's intrinsic forms */
#pragma function(sqrt, sin, cos, tan, atan, atan2, exp, log, log10, pow, fmod, floor, ceil)
#endif
#define TOS_NOOPT            /* hc_emit.py also wraps these in #pragma optimize("", off) */
#define TOS_WEAK
#define TOS_STUB(n) __stub_##n   /* linked in by TOS_ALTNAME unless the runtime defines n */
#ifdef _M_IX86                   /* x86 C names have a leading _ */
#define TOS_ALTNAME(n) __pragma(comment(linker, "/alternatename:_" #n "=___stub_" #n))
#else
#define TOS_ALTNAME(n) __pragma(comment(linker, "/alternatename:" #n "=__stub_" #n))
#endif
#else
#define TOS_ALTNAME(n)
#define TOS_TLS __thread
#define TOS_NOOPT __attribute__((optnone, noinline))   /* functions with try: locals must survive longjmp */
#define TOS_WEAK __attribute__((weak))                  /* the runtime's own definition wins */
#define TOS_STUB(n) n
#endif

/* 32-bit x86: the translator's adapters for functions called through a differently typed pointer */
#if defined(_M_IX86) || defined(__i386__)
#define TOS_X86 1
#define TOS_X86_OR(x86, other) (x86)
#else
#define TOS_X86_OR(x86, other) (other)
#endif

/* HolyC pointers take 8 bytes in memory; a T** or an array of pointers points at these slots,
   so the layout is the same where C pointers are 4 bytes (32-bit builds). */
typedef union { void *p; uint64_t v; } TosPS;

static inline double __i2f(int64_t i) { double d; memcpy(&d, &i, 8); return d; }
static inline int64_t __f2i(double d) { int64_t i; memcpy(&i, &d, 8); return i; }
int64_t __tos_ipow(int64_t b, int64_t e);
void __tos_unimpl(const char *name);
void __tos_pause(void);

/* try/catch: a per-task stack of setjmp frames */
typedef struct TosTry { jmp_buf jb; struct TosTry *prev; } TosTry;
typedef struct TosFile { const char *name; const uint8_t *data; int64_t size; } TosFile;  /* embedded read-only files */

/* Functions callable by name (PopUp/ExePrint strings, CallExtStr).  A thunk takes HolyC's
   8-byte arg slots; skip marks args left empty ("F(,x)") so the default is used; m bit i
   marks arg i as F64 bits, and TOS_TYPED says the caller knows (else slots are passed raw). */
typedef int64_t (*TosThunk)(int64_t argc, int64_t *argv, uint64_t skip, uint64_t m);
typedef struct TosFun { const char *name; TosThunk call; void *addr; } TosFun;
#define TOS_TYPED (1ULL << 63)
#define __TOS_HAS(i) ((i) < argc && !(skip >> (i) & 1))
static inline int64_t __tos_ai(int64_t *a, int i, uint64_t m) { return (m & TOS_TYPED) && (m >> i & 1) ? (int64_t)__i2f(a[i]) : a[i]; }
static inline double __tos_af(int64_t *a, int i, uint64_t m) { return (m & TOS_TYPED) && !(m >> i & 1) ? (double)a[i] : __i2f(a[i]); }
void __tos_try_push(TosTry *t);
void __tos_try_pop(TosTry *t);
void __tos_try_unwind(int64_t n);
void __tos_catch_end(void);
