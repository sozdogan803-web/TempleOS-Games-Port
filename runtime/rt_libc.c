/* The 32-bit build's C library (TOS_NOCRT): only what the games and the runtime call, on Win32 calls
   that Windows 95 and NT 4 already have.  Microsoft's current C runtime needs Vista or newer.
   Math uses the x87 instructions TempleOS itself uses (FSIN, FPATAN, ...).
   The 64-bit integer helpers are the ones MSVC calls for I64 arithmetic on x86. */
#if defined(_M_IX86) && defined(TOS_NOCRT)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <setjmp.h>

#pragma function(memcpy, memset, memcmp, memmove, strlen, strcmp, strcpy, strncmp, strncpy)
#pragma function(sqrt, sin, cos, tan, atan, atan2, exp, log, log10, pow, fmod, floor, ceil)

int _fltused = 0x9875;
int main(void);

void __cdecl rt_entry(void)
{
  unsigned short cw = 0x027F;   /* x87: double precision, round to nearest, exceptions masked (what NT gives a thread) */
  __asm fldcw cw
  ExitProcess((UINT)main());
}

/* ---- memory and strings ---- */
void *__cdecl memcpy(void *d, const void *s, size_t n) { __movsb(d, s, n); return d; }
void *__cdecl memset(void *d, int c, size_t n) { __stosb(d, (unsigned char)c, n); return d; }
void *__cdecl memmove(void *d, const void *s, size_t n)
{
  unsigned char *dp = d;
  const unsigned char *sp = s;
  if (dp <= sp || dp >= sp + n) __movsb(dp, sp, n);
  else if (n) { /* overlapping, destination above: copy backwards (a C loop here may be compiled into a memmove call) */
    const unsigned char *se = sp + n - 1;
    unsigned char *de = dp + n - 1;
    __asm {
      mov esi, se
      mov edi, de
      mov ecx, n
      std
      rep movsb
      cld
    }
  }
  return d;
}
int __cdecl memcmp(const void *a, const void *b, size_t n)
{
  const unsigned char *x = a, *y = b;
  for (; n; n--, x++, y++)
    if (*x != *y) return *x < *y ? -1 : 1;
  return 0;
}
size_t __cdecl strlen(const char *s) { const char *p = s; while (*p) p++; return (size_t)(p - s); }
int __cdecl strcmp(const char *a, const char *b)
{
  while (*a && *a == *b) a++, b++;
  return (unsigned char)*a - (unsigned char)*b;
}
int __cdecl strncmp(const char *a, const char *b, size_t n)
{
  for (; n; n--, a++, b++) {
    if (*a != *b) return (unsigned char)*a - (unsigned char)*b;
    if (!*a) break;
  }
  return 0;
}
int __cdecl toupper(int c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }
int __cdecl tolower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
int __cdecl isalpha(int c) { return (c | 32) >= 'a' && (c | 32) <= 'z'; }
int __cdecl _stricmp(const char *a, const char *b)
{
  while (*a && tolower((unsigned char)*a) == tolower((unsigned char)*b)) a++, b++;
  return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}
char *__cdecl strcpy(char *d, const char *s) { char *r = d; while ((*d++ = *s++)); return r; }
char *__cdecl strncpy(char *d, const char *s, size_t n)
{
  size_t i = 0;
  for (; i < n && s[i]; i++) d[i] = s[i];
  for (; i < n; i++) d[i] = 0;
  return d;
}
char *__cdecl strchr(const char *s, int c)
{
  for (;; s++) {
    if (*s == (char)c) return (char *)s;
    if (!*s) return NULL;
  }
}
char *__cdecl strrchr(const char *s, int c)
{
  const char *r = NULL;
  for (;; s++) {
    if (*s == (char)c) r = s;
    if (!*s) return (char *)r;
  }
}
char *__cdecl strtok_s(char *s, const char *delim, char **ctx)
{
  char *t;
  if (!s) s = *ctx;
  while (*s && strchr(delim, *s)) s++;
  if (!*s) { *ctx = s; return NULL; }
  t = s;
  while (*s && !strchr(delim, *s)) s++;
  if (*s) *s++ = 0;
  *ctx = s;
  return t;
}

static unsigned __int64 parse_u(const char *s, char **end, int base, int *neg)
{
  unsigned __int64 v = 0;
  int d;
  while (*s == ' ' || *s == '\t') s++;
  *neg = *s == '-';
  if (*s == '-' || *s == '+') s++;
  if ((base == 0 || base == 16) && s[0] == '0' && (s[1] | 32) == 'x') { s += 2; base = 16; }
  if (!base) base = 10;
  for (;; s++) {
    d = *s >= '0' && *s <= '9' ? *s - '0' : (*s | 32) >= 'a' && (*s | 32) <= 'z' ? (*s | 32) - 'a' + 10 : 99;
    if (d >= base) break;
    v = v * base + d;
  }
  if (end) *end = (char *)s;
  return v;
}
unsigned long __cdecl strtoul(const char *s, char **end, int base)
{
  int neg;
  unsigned long v = (unsigned long)parse_u(s, end, base, &neg);
  return neg ? 0 - v : v;
}
__int64 __cdecl _strtoi64(const char *s, char **end, int base)
{
  int neg;
  unsigned __int64 v = parse_u(s, end, base, &neg);
  return neg ? -(__int64)v : (__int64)v;
}
__int64 __cdecl _atoi64(const char *s) { return _strtoi64(s, NULL, 10); }

/* ---- heap, environment, exit ---- */
void *__cdecl malloc(size_t n) { return HeapAlloc(GetProcessHeap(), 0, n ? n : 1); }
void *__cdecl calloc(size_t n, size_t sz) { return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n * sz ? n * sz : 1); }
void *__cdecl realloc(void *p, size_t n)
{
  if (!p) return malloc(n);
  return HeapReAlloc(GetProcessHeap(), 0, p, n ? n : 1);
}
void __cdecl free(void *p) { if (p) HeapFree(GetProcessHeap(), 0, p); }
__declspec(noreturn) void __cdecl exit(int code) { ExitProcess((UINT)code); }

char *__cdecl getenv(const char *name)
{ /* each variable is read once; callers may keep the pointer */
  static struct Env { struct Env *next; char *name, *val; } *list;
  struct Env *e;
  DWORD n;
  for (e = list; e; e = e->next)
    if (!strcmp(e->name, name)) return e->val;
  e = calloc(1, sizeof *e);
  e->name = malloc(strlen(name) + 1);
  strcpy(e->name, name);
  if ((n = GetEnvironmentVariableA(name, NULL, 0)) > 0) {
    e->val = malloc(n);
    if (!GetEnvironmentVariableA(name, e->val, n)) { free(e->val); e->val = NULL; }
  }
  e->next = list;
  list = e;
  return e->val;
}

/* ---- stdio: unbuffered, on file handles ---- */
typedef struct { HANDLE h; } RtFile;
static RtFile std_files[3];

FILE *__cdecl __acrt_iob_func(unsigned i)
{
  static const DWORD ids[3] = {STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE};
  if (i > 2) i = 2;
  if (!std_files[i].h) std_files[i].h = GetStdHandle(ids[i]);
  return (FILE *)&std_files[i];
}
FILE *__cdecl fopen(const char *path, const char *mode)
{
  int w = strchr(mode, 'w') != NULL;
  HANDLE h = CreateFileA(path, w ? GENERIC_WRITE : GENERIC_READ, FILE_SHARE_READ, NULL, w ? CREATE_ALWAYS : OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, NULL);
  RtFile *f;
  if (h == INVALID_HANDLE_VALUE) return NULL;
  f = malloc(sizeof *f);
  f->h = h;
  return (FILE *)f;
}
int __cdecl fclose(FILE *f) { CloseHandle(((RtFile *)f)->h); free(f); return 0; }
size_t __cdecl fwrite(const void *p, size_t sz, size_t n, FILE *f)
{
  DWORD done = 0;
  HANDLE h = ((RtFile *)f)->h;
  if (h && h != INVALID_HANDLE_VALUE && sz * n) WriteFile(h, p, (DWORD)(sz * n), &done, NULL);
  return sz ? done / sz : 0;
}
int __cdecl fputs(const char *s, FILE *f) { fwrite(s, 1, strlen(s), f); return 0; }
int __cdecl fputc(int c, FILE *f) { char ch = (char)c; fwrite(&ch, 1, 1, f); return c; }
int __cdecl fflush(FILE *f) { (void)f; return 0; }

/* printf subset: flags - 0, width, .precision, l ll I64 z, and d i u x X p s c % */
int __cdecl vsnprintf(char *buf, size_t cap, const char *fmt, va_list ap)
{
  size_t o = 0;
#define PUT(ch) do { if (o + 1 < cap) buf[o] = (ch); o++; } while (0)
  for (; *fmt; fmt++) {
    char tmp[24], *s;
    int left = 0, zero = 0, width = 0, prec = -1, big = 0, len, neg = 0;
    unsigned __int64 v;
    if (*fmt != '%') { PUT(*fmt); continue; }
    fmt++;
    for (;; fmt++) {
      if (*fmt == '-') left = 1;
      else if (*fmt == '0') zero = 1;
      else break;
    }
    if (*fmt == '*') { width = va_arg(ap, int); fmt++; }
    else while (*fmt >= '0' && *fmt <= '9') width = width * 10 + *fmt++ - '0';
    if (*fmt == '.') {
      fmt++;
      prec = 0;
      if (*fmt == '*') { prec = va_arg(ap, int); fmt++; }
      else while (*fmt >= '0' && *fmt <= '9') prec = prec * 10 + *fmt++ - '0';
    }
    if (fmt[0] == 'I' && fmt[1] == '6' && fmt[2] == '4') { big = 1; fmt += 3; }
    else if (fmt[0] == 'l' && fmt[1] == 'l') { big = 1; fmt += 2; }
    else if (*fmt == 'l' || *fmt == 'z' || *fmt == 'h') fmt++;
    switch (*fmt) {
    case 'd': case 'i': case 'u': case 'x': case 'X': case 'p': {
      int base = *fmt == 'd' || *fmt == 'i' || *fmt == 'u' ? 10 : 16;
      const char *digs = *fmt == 'X' || *fmt == 'p' ? "0123456789ABCDEF" : "0123456789abcdef";
      if (*fmt == 'p') { v = (uintptr_t)va_arg(ap, void *); width = 8; zero = 1; }
      else if (big) v = va_arg(ap, unsigned __int64);
      else v = (*fmt == 'd' || *fmt == 'i') ? (unsigned __int64)(__int64)va_arg(ap, int) : va_arg(ap, unsigned int);
      if ((*fmt == 'd' || *fmt == 'i') && (__int64)v < 0) { neg = 1; v = 0 - v; }
      s = tmp + sizeof tmp;
      *--s = 0;
      do { *--s = digs[v % base]; v /= base; } while (v);
      break;
    }
    case 's':
      s = va_arg(ap, char *);
      if (!s) s = "(null)";
      break;
    case 'c':
      tmp[0] = (char)va_arg(ap, int);
      tmp[1] = 0;
      s = tmp;
      break;
    case '%': PUT('%'); continue;
    case 0: fmt--; continue;
    default: PUT('%'); PUT(*fmt); continue;
    }
    len = (int)strlen(s);
    if (*fmt == 's' && prec >= 0 && prec < len) len = prec;
    width -= len + neg;
    if (neg && zero) PUT('-');
    if (!left) while (width-- > 0) PUT(zero ? '0' : ' ');
    if (neg && !zero) PUT('-');
    while (len--) PUT(*s++);
    while (width-- > 0) PUT(' ');
  }
  if (cap) buf[o < cap ? o : cap - 1] = 0;
#undef PUT
  return (int)o;
}
int __cdecl snprintf(char *buf, size_t cap, const char *fmt, ...)
{
  va_list ap;
  int r;
  va_start(ap, fmt);
  r = vsnprintf(buf, cap, fmt, ap);
  va_end(ap);
  return r;
}
int __cdecl vfprintf(FILE *f, const char *fmt, va_list ap)
{
  char buf[1024];
  int n = vsnprintf(buf, sizeof buf, fmt, ap);
  fwrite(buf, 1, n < (int)sizeof buf ? n : sizeof buf - 1, f);
  return n;
}
int __cdecl printf(const char *fmt, ...)
{
  va_list ap;
  int r;
  va_start(ap, fmt);
  r = vfprintf(stdout, fmt, ap);
  va_end(ap);
  return r;
}
int __cdecl fprintf(FILE *f, const char *fmt, ...)
{
  va_list ap;
  int r;
  va_start(ap, fmt);
  r = vfprintf(f, fmt, ap);
  va_end(ap);
  return r;
}

/* ---- math on the x87 ---- */
double __cdecl sqrt(double x) { double r; __asm { fld x } __asm { fsqrt } __asm { fstp r } return r; }
double __cdecl sin(double x) { double r; __asm { fld x } __asm { fsin } __asm { fstp r } return r; }
double __cdecl cos(double x) { double r; __asm { fld x } __asm { fcos } __asm { fstp r } return r; }
double __cdecl tan(double x) { double r; __asm { fld x } __asm { fptan } __asm { fstp st(0) } __asm { fstp r } return r; }
double __cdecl atan(double x) { double r; __asm { fld x } __asm { fld1 } __asm { fpatan } __asm { fstp r } return r; }
double __cdecl atan2(double y, double x) { double r; __asm { fld y } __asm { fld x } __asm { fpatan } __asm { fstp r } return r; }
double __cdecl log(double x) { double r; __asm { fldln2 } __asm { fld x } __asm { fyl2x } __asm { fstp r } return r; }
double __cdecl log10(double x) { double r; __asm { fldlg2 } __asm { fld x } __asm { fyl2x } __asm { fstp r } return r; }
double __cdecl log2(double x) { double r; __asm { fld1 } __asm { fld x } __asm { fyl2x } __asm { fstp r } return r; }
static double exp2_(double t)
{
  double r;
  if (t != t) return t;
  if (t > 1100) return HUGE_VAL;
  if (t < -1100) return 0;
  __asm {
    fld t
    fld st(0)
    frndint          ; n t
    fsub st(1), st   ; n f
    fxch             ; f n
    f2xm1
    fld1
    faddp st(1), st  ; 2^f n
    fscale
    fstp st(1)
    fstp r
  }
  return r;
}
double __cdecl exp(double x) { return exp2_(x * 1.4426950408889634074); }
double __cdecl pow(double b, double e)
{
  double r;
  if (e == 0) return 1;
  if (b == 0) return e > 0 ? 0 : HUGE_VAL;
  if (b > 0) return exp2_(e * log2(b));
  if (e != floor(e)) return (b - b) / (b - b);   /* NaN */
  r = exp2_(e * log2(-b));
  return fmod(e, 2) ? -r : r;
}
static double round_cw(double x, unsigned short mode)
{
  unsigned short old, cw;
  double r;
  __asm {
    fnstcw old
    mov ax, word ptr old
    and ax, 0F3FFh
    or ax, word ptr mode
    mov word ptr cw, ax
    fldcw cw
    fld x
    frndint
    fstp r
    fldcw old
  }
  return r;
}
double __cdecl floor(double x) { return round_cw(x, 0x0400); }
double __cdecl ceil(double x) { return round_cw(x, 0x0800); }
double __cdecl trunc(double x) { return round_cw(x, 0x0C00); }
double __cdecl nearbyint(double x) { double r; __asm { fld x } __asm { frndint } __asm { fstp r } return r; }
double __cdecl fmod(double x, double y)
{
  double r;
  __asm {
    fld y
    fld x
  again:
    fprem
    fnstsw ax
    sahf
    jp again
    fstp st(1)
    fstp r
  }
  return r;
}

/* ---- setjmp/longjmp: callee-saved registers, stack, return address and the SEH chain ---- */
__declspec(naked) int __cdecl _setjmp3(jmp_buf jb, int n, ...)
{
  __asm {
    mov edx, [esp+4]
    mov [edx], ebp
    mov [edx+4], ebx
    mov [edx+8], edi
    mov [edx+12], esi
    lea ecx, [esp+4]
    mov [edx+16], ecx
    mov ecx, [esp]
    mov [edx+20], ecx
    mov ecx, fs:[0]
    mov [edx+24], ecx
    xor eax, eax
    ret
  }
}
__declspec(naked) void __cdecl longjmp(jmp_buf jb, int v)
{
  __asm {
    mov edx, [esp+4]
    mov eax, [esp+8]
    test eax, eax
    jnz have_v
    inc eax
  have_v:
    mov ecx, [edx+24]
    mov fs:[0], ecx
    mov ebp, [edx]
    mov ebx, [edx+4]
    mov edi, [edx+8]
    mov esi, [edx+12]
    mov esp, [edx+16]
    jmp dword ptr [edx+20]
  }
}

/* ---- stack probe for frames over a page (EAX = size); touches each page in order ---- */
__declspec(naked) void __cdecl _chkstk(void)
{
  __asm {
    push ecx
    lea ecx, [esp+8]       ; the caller's esp before the call
    sub ecx, eax           ; its esp after the allocation
    mov eax, esp
  probe:
    sub eax, 1000h
    cmp eax, ecx
    jb probed
    test [eax], eax
    jmp probe
  probed:
    test [ecx], eax
    mov eax, esp
    mov esp, ecx
    mov ecx, [eax]
    push dword ptr [eax+4]
    ret
  }
}
__declspec(naked) void __cdecl _alloca_probe(void) { __asm jmp _chkstk }

/* ---- x87 float -> integer (truncating), as MSVC calls them: value in ST0, result in EDX:EAX ---- */
__declspec(naked) void __cdecl _ftol2(void)
{
  __asm {
    push ebp
    mov ebp, esp
    sub esp, 12
    fnstcw [ebp-2]
    movzx eax, word ptr [ebp-2]
    or eax, 0C00h
    mov [ebp-4], ax
    fldcw [ebp-4]
    fistp qword ptr [ebp-12]
    fldcw [ebp-2]
    mov eax, [ebp-12]
    mov edx, [ebp-8]
    mov esp, ebp
    pop ebp
    ret
  }
}
__declspec(naked) void __cdecl _ftol2_sse(void) { __asm jmp _ftol2 }
__declspec(naked) void __cdecl _ftol(void) { __asm jmp _ftol2 }
static const double two63 = 9223372036854775808.0;
__declspec(naked) void __cdecl _ftoul2_legacy(void)
{ /* to unsigned 64-bit: values from 2^63 up are converted less 2^63, then the top bit set */
  __asm {
    fld qword ptr [two63]
    fcomp st(1)
    fnstsw ax
    sahf
    ja below
    fsub qword ptr [two63]
    call _ftol2
    xor edx, 80000000h
    ret
  below:
    jmp _ftol2
  }
}

/* ---- 64-bit integer helpers (operands on the stack, callee pops 16 bytes; shifts take EDX:EAX, CL) ---- */
__declspec(naked) void __cdecl _allmul(void)
{
  __asm {
    mov eax, [esp+8]
    mov ecx, [esp+16]
    or ecx, eax
    mov ecx, [esp+12]
    jnz hard
    mov eax, [esp+4]
    mul ecx
    ret 16
  hard:
    push ebx
    mul ecx                 ; a.hi * b.lo
    mov ebx, eax
    mov eax, [esp+8]
    mul dword ptr [esp+20]  ; a.lo * b.hi
    add ebx, eax
    mov eax, [esp+8]
    mul ecx                 ; a.lo * b.lo
    add edx, ebx
    pop ebx
    ret 16
  }
}
__declspec(naked) void __cdecl _aulldiv(void)
{
  __asm {
    push ebx
    push esi
    mov eax, [esp+24]       ; divisor hi
    or eax, eax
    jnz big
    mov ecx, [esp+20]       ; divisor lo
    mov eax, [esp+16]       ; dividend hi
    xor edx, edx
    div ecx
    mov ebx, eax
    mov eax, [esp+12]
    div ecx
    mov edx, ebx
    jmp done
  big:
    mov ecx, eax
    mov ebx, [esp+20]
    mov edx, [esp+16]
    mov eax, [esp+12]
  shift:
    shr ecx, 1
    rcr ebx, 1
    shr edx, 1
    rcr eax, 1
    or ecx, ecx
    jnz shift
    div ebx
    mov esi, eax            ; quotient, maybe one too big
    mul dword ptr [esp+24]
    mov ecx, eax
    mov eax, [esp+20]
    mul esi
    add edx, ecx
    jc fix
    cmp edx, [esp+16]
    ja fix
    jb ok
    cmp eax, [esp+12]
    jbe ok
  fix:
    dec esi
  ok:
    xor edx, edx
    mov eax, esi
  done:
    pop esi
    pop ebx
    ret 16
  }
}
__declspec(naked) void __cdecl _aullrem(void)
{
  __asm {
    push ebx
    push dword ptr [esp+20]
    push dword ptr [esp+20]
    push dword ptr [esp+20]
    push dword ptr [esp+20]
    call _aulldiv
    push dword ptr [esp+20]
    push dword ptr [esp+20]
    push edx
    push eax
    call _allmul            ; quotient * divisor
    mov ecx, [esp+8]
    mov ebx, [esp+12]
    sub ecx, eax
    sbb ebx, edx
    mov eax, ecx
    mov edx, ebx
    pop ebx
    ret 16
  }
}
/* signed: divide magnitudes; the quotient is negative when the signs differ, the remainder takes the dividend's sign */
__declspec(naked) void __cdecl _alldiv(void)
{
  __asm {
    push edi
    xor edi, edi
    mov eax, [esp+12]
    or eax, eax
    jge a_pos
    xor edi, 1
    mov edx, [esp+8]
    neg eax
    neg edx
    sbb eax, 0
    mov [esp+12], eax
    mov [esp+8], edx
  a_pos:
    mov eax, [esp+20]
    or eax, eax
    jge b_pos
    xor edi, 1
    mov edx, [esp+16]
    neg eax
    neg edx
    sbb eax, 0
    mov [esp+20], eax
    mov [esp+16], edx
  b_pos:
    push dword ptr [esp+20]
    push dword ptr [esp+20]
    push dword ptr [esp+20]
    push dword ptr [esp+20]
    call _aulldiv
    or edi, edi
    jz done
    neg edx
    neg eax
    sbb edx, 0
  done:
    pop edi
    ret 16
  }
}
__declspec(naked) void __cdecl _allrem(void)
{
  __asm {
    push edi
    xor edi, edi
    mov eax, [esp+12]
    or eax, eax
    jge a_pos
    mov edi, 1
    mov edx, [esp+8]
    neg eax
    neg edx
    sbb eax, 0
    mov [esp+12], eax
    mov [esp+8], edx
  a_pos:
    mov eax, [esp+20]
    or eax, eax
    jge b_pos
    mov edx, [esp+16]
    neg eax
    neg edx
    sbb eax, 0
    mov [esp+20], eax
    mov [esp+16], edx
  b_pos:
    push dword ptr [esp+20]
    push dword ptr [esp+20]
    push dword ptr [esp+20]
    push dword ptr [esp+20]
    call _aullrem
    or edi, edi
    jz done
    neg edx
    neg eax
    sbb edx, 0
  done:
    pop edi
    ret 16
  }
}
/* quotient in EDX:EAX, remainder in EBX:ECX */
__declspec(naked) void __cdecl _aulldvrm(void)
{
  __asm {
    push dword ptr [esp+16]
    push dword ptr [esp+16]
    push dword ptr [esp+16]
    push dword ptr [esp+16]
    call _aullrem
    push edx
    push eax
    push dword ptr [esp+24]
    push dword ptr [esp+24]
    push dword ptr [esp+24]
    push dword ptr [esp+24]
    call _aulldiv
    pop ecx
    pop ebx
    ret 16
  }
}
__declspec(naked) void __cdecl _alldvrm(void)
{
  __asm {
    push dword ptr [esp+16]
    push dword ptr [esp+16]
    push dword ptr [esp+16]
    push dword ptr [esp+16]
    call _allrem
    push edx
    push eax
    push dword ptr [esp+24]
    push dword ptr [esp+24]
    push dword ptr [esp+24]
    push dword ptr [esp+24]
    call _alldiv
    pop ecx
    pop ebx
    ret 16
  }
}
/* shift counts are taken mod 64, as the 64-bit CPU (and TempleOS) does */
__declspec(naked) void __cdecl _allshl(void)
{
  __asm {
    and cl, 63
    cmp cl, 32
    jae hi_shift
    shld edx, eax, cl
    shl eax, cl
    ret
  hi_shift:
    mov edx, eax
    xor eax, eax
    and cl, 31
    shl edx, cl
    ret
  }
}
__declspec(naked) void __cdecl _allshr(void)
{
  __asm {
    and cl, 63
    cmp cl, 32
    jae hi_shift
    shrd eax, edx, cl
    sar edx, cl
    ret
  hi_shift:
    mov eax, edx
    sar edx, 31
    and cl, 31
    sar eax, cl
    ret
  }
}
__declspec(naked) void __cdecl _aullshr(void)
{
  __asm {
    and cl, 63
    cmp cl, 32
    jae hi_shift
    shrd eax, edx, cl
    shr edx, cl
    ret
  hi_shift:
    mov eax, edx
    xor edx, edx
    and cl, 31
    shr eax, cl
    ret
  }
}
#endif
