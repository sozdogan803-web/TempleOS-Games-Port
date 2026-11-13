/* Portable C versions of TempleOS's asm/hardware primitives: memory, strings, bits,
   math, queues, hash tables, exceptions and the I/O ports the kernel pokes.
   Each h_Name here replaces the stub hc_emit.py writes for it. */
#include <stdarg.h>
#include <time.h>
#include "rt.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <intrin.h>
#else
#include <execinfo.h>
#include <sys/mman.h>
__attribute__((noinline)) int rt_backtrace(int skip, int n, void **pcs)
{ /* backtrace(), skipping this frame too (Windows: rt_win32.c) */
  void *b[64];
  int m = backtrace(b, 64) - ++skip;
  if (m <= 0) return 0;
  if (m > n) m = n;
  memcpy(pcs, b + skip, (size_t)m * sizeof *pcs);
  return m;
}
#define _AddressOfReturnAddress() __builtin_frame_address(0)
#endif

c_CTask *tos_fs;
c_CCPU *tos_gs;
TosTry *tos_try_top;

void rt_fatal(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  fputs("TempleOS runtime: ", stderr);
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
  va_end(ap);
  fflush(stderr);
  exit(3);
}

void __tos_unimpl(const char *name)
{ /* log each missing primitive once and carry on with a 0 result */
  static const char *seen[256];
  int i;
  for (i = 0; i < 256 && seen[i]; i++)
    if (seen[i] == name) return;
  if (i < 256) seen[i] = name;
  fprintf(stderr, "TempleOS runtime: %s not implemented, called from:\n", name);
  {
    void *pcs[5];
    int n = rt_backtrace(2, 5, pcs);   /* skip this and the stub */
    rt_print_stack(pcs, n);
  }
}

int64_t __tos_ipow(int64_t b, int64_t e)
{
  int64_t r = 1;
  if (e < 0) return b == 1 ? 1 : b == -1 ? (e & 1 ? -1 : 1) : 0;
  while (e) { if (e & 1) r *= b; b *= b; e >>= 1; }
  return r;
}

/* ---- try/catch (see KExcept.HC throw) ---- */
void __tos_try_push(TosTry *t) { t->prev = tos_try_top; tos_try_top = t; }
void __tos_try_pop(TosTry *t) { tos_try_top = t->prev; }
void __tos_try_unwind(int64_t n) { while (n-- > 0 && tos_try_top) tos_try_top = tos_try_top->prev; }

static void *throw_pcs[24];   /* where the last throw came from */
static int throw_n;

void rt_report_except(const char *what)
{
  int64_t ch = tos_fs->m_except_ch;
  char s[9] = {0};
  memcpy(s, &ch, 8);
  fprintf(stderr, "TempleOS runtime: %s exception '%s' in task %s, thrown from:\n", what, s, tos_fs->m_task_name);
  rt_print_stack(throw_pcs, throw_n);
}

static void unhandled(void)
{
  rt_report_except("unhandled");
  exit(3);
}

static void run_next_handler(void)
{
  TosTry *t = tos_try_top;
  if (!t) unhandled();
  tos_try_top = t->prev;
  longjmp(t->jb, 1);
}

void h_throw(int64_t ch, int8_t no_log)
{
  c_CTask *t = tos_fs;
  t->m_except_ch = ch;
  memset(t->m_except_callers, 0, sizeof t->m_except_callers);
  t->m_catch_except = 0;
  throw_n = rt_backtrace(0, 24, throw_pcs);
  run_next_handler();
}

void __tos_catch_end(void)
{ /* a catch block that did not set Fs->catch_except passes the exception outward */
  if (!tos_fs->m_catch_except) run_next_handler();
}

/* ---- memory: the CMemUsed {hc,size} header sits just before each block ---- */
typedef struct { uint64_t magic, pad; void *hc; int64_t size; } MemHdr;
#define MEM_MAGIC 0x6B6C4275734D4554ull
#define MEM_TAIL  0xFDFDFDFDFDFDFDFDull

static void *heap_of(c_CTask *t)
{ /* MAlloc accepts a CTask or a CHeapCtrl */
  if (!t) t = tos_fs;
  if (t && t->m_task_signature == TASK_SIG) return t->m_data_heap;
  return t;
}

/* Testing aid: TOS_HEAPCHECK=1 gives each block its own pages, ending at a no-access page, and never
   reuses them, so an overrun or a write after Free faults (with a stack) where it happens.  Freed
   blocks stay readable: TempleOS code reads freed memory on purpose (ScanMsg after JobDel). */
static int heap_debug = -1;
static size_t blk_len(size_t n) { return (((n + 15) & ~(size_t)15) + 4095) & ~(size_t)4095; }
static void *blk_alloc(size_t n)
{
  uint8_t *p;
  if (heap_debug < 0) heap_debug = getenv("TOS_HEAPCHECK") != NULL;
  if (!heap_debug) return calloc(1, n);
#ifdef _WIN32
  if (!(p = VirtualAlloc(NULL, blk_len(n) + 4096, MEM_RESERVE, PAGE_NOACCESS)) ||
      !VirtualAlloc(p, blk_len(n), MEM_COMMIT, PAGE_READWRITE)) return NULL;
#else
  if ((p = mmap(NULL, blk_len(n) + 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0)) == MAP_FAILED ||
      mprotect(p + blk_len(n), 4096, PROT_NONE)) return NULL;
#endif
  return p + blk_len(n) - ((n + 15) & ~(size_t)15);
}
static void blk_free(void *h, size_t n)
{
  if (!heap_debug) free(h);
#ifdef _WIN32
  else { DWORD old; VirtualProtect((void *)((uintptr_t)h & ~(uintptr_t)4095), blk_len(n), PAGE_READONLY, &old); }
#else
  else mprotect((void *)((uintptr_t)h & ~(uintptr_t)4095), blk_len(n), PROT_READ);
#endif
}

uint8_t *h_MAlloc(int64_t size, c_CTask *t)
{
  MemHdr *h;
  uint64_t tail = MEM_TAIL;
  if (size < 0) size = 0;
  h = blk_alloc(sizeof(MemHdr) + size + 8);
  if (!h) rt_fatal("out of memory (%lld bytes)", (long long)size);
  h->magic = MEM_MAGIC;
  h->hc = heap_of(t);
  h->size = size;
  memcpy((uint8_t *)(h + 1) + size, &tail, 8);
  return (uint8_t *)(h + 1);
}

static MemHdr *hdr_of(uint8_t *p, const char *who)
{ /* NULL if p is not a live MAlloc block (TempleOS would corrupt its heap; we report and skip) */
  MemHdr *h;
  uint64_t tail;
  int64_t s = ((int64_t *)p)[-1];
  if (s < 0) p += s;   /* MAllocAligned stores its offset back to the real block */
  h = (MemHdr *)p - 1;
  if (h->magic != MEM_MAGIC) { fprintf(stderr, "TempleOS runtime: %s(%p): not a heap block\n", who, p); return NULL; }
  memcpy(&tail, p + h->size, 8);
  if (tail != MEM_TAIL) fprintf(stderr, "TempleOS runtime: %s(%p): block of %lld bytes was overrun\n", who, p, (long long)h->size);
  return h;
}
uint8_t *h_CAlloc(int64_t size, c_CTask *t) { return h_MAlloc(size, t); }
uint8_t *h_AMAlloc(int64_t size) { return h_MAlloc(size, h_adam_task); }
uint8_t *h_ACAlloc(int64_t size) { return h_MAlloc(size, h_adam_task); }

void h_Free(uint8_t *p)
{
  MemHdr *h;
  if (!p || !(h = hdr_of(p, "Free"))) return;
  h->magic = 0;
  blk_free(h, sizeof(MemHdr) + h->size + 8);
}

int64_t h_MSize(uint8_t *p)
{
  int64_t s;
  if (!p) return 0;
  s = ((int64_t *)p)[-1];
  if (s < 0) return ((int64_t *)(p + s))[-1] + s;
  return s;
}
int64_t h_MSize2(uint8_t *p) { return p ? h_MSize(p) + (int64_t)sizeof(MemHdr) : 0; }

uint8_t *h_MAllocIdent(uint8_t *src, c_CTask *t)
{
  int64_t n;
  uint8_t *r;
  if (!src) return NULL;
  n = h_MSize(src);
  r = h_MAlloc(n, t);
  memcpy(r, src, n);
  return r;
}
uint8_t *h_AMAllocIdent(uint8_t *src) { return h_MAllocIdent(src, h_adam_task); }

uint8_t *h_MAllocAligned(int64_t size, int64_t align, c_CTask *t, int64_t misalign)
{ /* same layout as MAllocFree.HC */
  int64_t mask = align - 1;
  uint8_t *p = h_MAlloc(size + mask + 8 + misalign, t);
  uint8_t *r = (uint8_t *)((((uintptr_t)p + 8 + (uintptr_t)mask) & ~(uintptr_t)mask) + (uintptr_t)misalign);
  ((int64_t *)r)[-1] = p - r;
  return r;
}
uint8_t *h_CAllocAligned(int64_t size, int64_t align, c_CTask *t, int64_t misalign)
{
  return h_MAllocAligned(size, align, t, misalign);
}

uint8_t *h_StrNew(uint8_t *buf, c_CTask *t)
{
  int64_t n = buf ? (int64_t)strlen((char *)buf) + 1 : 1;
  uint8_t *r = h_MAlloc(n, t);
  if (buf) memcpy(r, buf, n);
  return r;
}
uint8_t *h_AStrNew(uint8_t *buf) { return h_StrNew(buf, h_adam_task); }

c_CHeapCtrl *h_HeapCtrlInit(c_CHeapCtrl *hc, c_CTask *task, c_CBlkPool *bp)
{ /* shortcut: blocks come from the C heap and are not tracked per task, so a dying task's blocks leak */
  if (!hc) hc = calloc(1, sizeof *hc);
  hc->m_bp = bp;
  hc->m_mem_task = task;
  hc->m_next_mem_blk = hc->m_last_mem_blk = (c_CMemBlk *)&hc->m_next_mem_blk;
  return hc;
}
void h_HeapCtrlDel(c_CHeapCtrl *hc)
{
  uintptr_t fixed = (uintptr_t)h_tos_fixed_area;
  if (hc && ((uintptr_t)hc < fixed || (uintptr_t)hc >= fixed + sizeof(c_CSysFixedArea))) free(hc);
}

/* ---- memory ops (REP MOVSB/STOS return the end pointer) ---- */
uint8_t *h_MemCpy(uint8_t *d, uint8_t *s, int64_t n)
{
  if (n <= 0) return d;
  if (d > s && d < s + n) { int64_t i; for (i = 0; i < n; i++) d[i] = s[i]; }   /* forward copy, like REP MOVSB */
  else memmove(d, s, n);
  return d + n;
}
uint8_t *h_MemSet(uint8_t *d, int64_t v, int64_t n) { if (n > 0) { memset(d, (int)v, n); d += n; } return d; }
uint8_t *h_MemSetU8(uint8_t *d, int64_t v, int64_t n) { return h_MemSet(d, v, n); }
uint16_t *h_MemSetU16(uint16_t *d, int64_t v, int64_t n) { while (n-- > 0) *d++ = (uint16_t)v; return d; }
uint32_t *h_MemSetU32(uint32_t *d, int64_t v, int64_t n) { while (n-- > 0) *d++ = (uint32_t)v; return d; }
int64_t *h_MemSetI64(int64_t *d, int64_t v, int64_t n) { while (n-- > 0) *d++ = v; return d; }
int64_t h_MemCmp(uint8_t *a, uint8_t *b, int64_t n)
{
  int r = n > 0 ? memcmp(a, b, n) : 0;
  return r < 0 ? -1 : r > 0;
}

/* ---- strings ---- */
int64_t h_StrLen(uint8_t *s) { return s ? (int64_t)strlen((char *)s) : 0; }
void h_StrCpy(uint8_t *d, uint8_t *s) { if (!s) *d = 0; else memmove(d, s, strlen((char *)s) + 1); }
int64_t h_ToUpper(uint8_t c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }

static int64_t scmp(uint8_t *a, uint8_t *b, int64_t n, int fold)
{
  for (; n; n--, a++, b++) {
    int64_t x = fold ? h_ToUpper(*a) : *a, y = fold ? h_ToUpper(*b) : *b;
    if (x != y) return x - y;
    if (!x) break;
  }
  return 0;
}
int64_t h_StrCmp(uint8_t *a, uint8_t *b) { return scmp(a, b, -1, 0); }
int64_t h_StrICmp(uint8_t *a, uint8_t *b) { return scmp(a, b, -1, 1); }
int64_t h_StrNCmp(uint8_t *a, uint8_t *b, int64_t n) { return scmp(a, b, n, 0); }
int64_t h_StrNICmp(uint8_t *a, uint8_t *b, int64_t n) { return scmp(a, b, n, 1); }

static uint8_t *smatch(uint8_t *needle, uint8_t *hay, int fold)
{
  int64_t n = (int64_t)strlen((char *)needle);
  for (; *hay; hay++)
    if (!scmp(needle, hay, n, fold)) return hay;
  return n ? NULL : hay;
}
uint8_t *h_StrMatch(uint8_t *needle, uint8_t *hay) { return smatch(needle, hay, 0); }
uint8_t *h_StrIMatch(uint8_t *needle, uint8_t *hay) { return smatch(needle, hay, 1); }

/* ---- bits (BT* take a signed bit index from the base address) ---- */
#define BYTE_OF(p, b) (((uint8_t *)(p))[(b) >> 3])
#define MASK_OF(b) ((uint8_t)(1u << ((b) & 7)))
int8_t h_Bt(uint8_t *p, int64_t b) { return (BYTE_OF(p, b) & MASK_OF(b)) != 0; }
int8_t h_Bts(uint8_t *p, int64_t b) { int8_t r = h_Bt(p, b); BYTE_OF(p, b) |= MASK_OF(b); return r; }
int8_t h_Btr(uint8_t *p, int64_t b) { int8_t r = h_Bt(p, b); BYTE_OF(p, b) &= ~MASK_OF(b); return r; }
int8_t h_Btc(uint8_t *p, int64_t b) { int8_t r = h_Bt(p, b); BYTE_OF(p, b) ^= MASK_OF(b); return r; }
int8_t h_LBts(uint8_t *p, int64_t b) { return h_Bts(p, b); }   /* one host thread runs all tasks: no lock needed */
int8_t h_LBtr(uint8_t *p, int64_t b) { return h_Btr(p, b); }
int8_t h_LBtc(uint8_t *p, int64_t b) { return h_Btc(p, b); }
int8_t h_BEqu(uint8_t *p, int64_t b, int8_t v) { return v ? h_Bts(p, b) : h_Btr(p, b); }
int8_t h_LBEqu(uint8_t *p, int64_t b, int8_t v) { return h_BEqu(p, b, v); }
int64_t h_Bsf(int64_t v) { int64_t i; if (!v) return -1; for (i = 0; !((uint64_t)v >> i & 1); i++); return i; }
int64_t h_Bsr(int64_t v) { int64_t i; if (!v) return -1; for (i = 63; !((uint64_t)v >> i & 1); i--); return i; }
uint32_t h_BFieldExtU32(uint8_t *p, int64_t bit, int64_t size)
{
  uint64_t w;
  memcpy(&w, p + (bit >> 3), 8);
  w >>= bit & 7;
  return (uint32_t)(size >= 32 ? w : w & ((1ull << size) - 1));
}
void h_BFieldOrU32(uint8_t *p, int64_t bit, uint32_t pattern)
{
  uint64_t w;
  memcpy(&w, p + (bit >> 3), 8);
  w |= (uint64_t)pattern << (bit & 7);
  memcpy(p + (bit >> 3), &w, 8);
}

/* Kernel/Compress.HC's _ARC_ENTRY_GET (asm), from the HolyC kept in its comment. */
void h_ArcEntryGet(c_CArcCtrl *c)
{
  int64_t i;
  c_CArcEntry *tmp, *tmp1;
  if (!c->m_entry_used) return;
  i = c->m_free_idx;
  c->m_entry_used = 0;
  c->m_cur_entry = c->m_next_entry;
  c->m_cur_bits_in_use = c->m_next_bits_in_use;
  if (c->m_next_bits_in_use < 12) {   /* ARC_BITS_MAX */
    c->m_next_entry = &c->m_compress[i++];
    if (i == c->m_free_limit) {
      c->m_next_bits_in_use++;
      c->m_free_limit = 1LL << c->m_next_bits_in_use;
    }
  } else {
    do if (++i == c->m_free_limit) i = c->m_min_table_entry;
    while (c->m_hash[i].p);
    tmp = &c->m_compress[i];
    c->m_next_entry = tmp;
    tmp1 = (c_CArcEntry *)&c->m_hash[tmp->m_basecode];   /* a hash slot doubles as an entry: next is at offset 0 */
    while (tmp1) {
      if (tmp1->m_next == tmp) { tmp1->m_next = tmp->m_next; break; }
      tmp1 = tmp1->m_next;
    }
  }
  c->m_free_idx = i;
}

/* One core: the only target is core 0, which is this thread; JobQue's wake-up has nothing to do. */
void h_MPInt(uint8_t num, int64_t cpu_num) { (void)num; (void)cpu_num; }

/* Games only Call() addresses of translated functions (ToTheFront's player tables), never raw bytes. */
int64_t h_Call(uint8_t *machine_code) { return ((int64_t (*)(void))machine_code)(); }

/* ---- integer/float intrinsics ---- */
double h_Abs(double d) { return fabs(d); }
int64_t h_AbsI64(int64_t i) { return i < 0 ? -i : i; }
int64_t h_MaxI64(int64_t a, int64_t b) { return a > b ? a : b; }
int64_t h_MinI64(int64_t a, int64_t b) { return a < b ? a : b; }
uint64_t h_MaxU64(uint64_t a, uint64_t b) { return a > b ? a : b; }
uint64_t h_MinU64(uint64_t a, uint64_t b) { return a < b ? a : b; }
int64_t h_ClampI64(int64_t n, int64_t lo, int64_t hi) { return n < lo ? lo : n > hi ? hi : n; }
uint64_t h_ClampU64(uint64_t n, uint64_t lo, uint64_t hi) { return n < lo ? lo : n > hi ? hi : n; }
uint64_t h_ModU64(uint64_t *q, uint64_t d) { uint64_t r = *q % d; *q /= d; return r; }
int64_t h_SignI64(int64_t i) { return (i > 0) - (i < 0); }
double h_Sign(double d) { return (d > 0) - (d < 0); }
double h_Sqr(double d) { return d * d; }
int64_t h_SqrI64(int64_t i) { return i * i; }
uint64_t h_SqrU64(uint64_t i) { return i * i; }
double h_Sqrt(double d) { return sqrt(d); }
double h_Sin(double d) { return sin(d); }
double h_Cos(double d) { return cos(d); }
double h_Tan(double d) { return tan(d); }
double h_ATan(double d) { return atan(d); }
double h_Arg(double x, double y) { return atan2(y, x); }
double h_Exp(double d) { return exp(d); }
double h_Ln(double d) { return log(d); }
double h_Log10(double d) { return log10(d); }
double h_Log2(double d) { return log2(d); }
double h_Pow(double b, double p) { return pow(b, p); }
double h_Pow10(double d) { return pow(10.0, d); }
double h_Ceil(double d) { return ceil(d); }
double h_Floor(double d) { return floor(d); }
double h_Round(double d) { return nearbyint(d); }   /* x87 default: round half to even */
double h_Trunc(double d) { return trunc(d); }
double h_ToF64(int64_t i) { return (double)i; }
int64_t h_ToI64(double d) { return (int64_t)d; }
int8_t h_ToBool(int64_t i) { return i != 0; }
void h_SwapU8(uint8_t *a, uint8_t *b) { uint8_t t = *a; *a = *b; *b = t; }
void h_SwapU16(uint16_t *a, uint16_t *b) { uint16_t t = *a; *a = *b; *b = t; }
void h_SwapU32(uint32_t *a, uint32_t *b) { uint32_t t = *a; *a = *b; *b = t; }
void h_SwapI64(int64_t *a, int64_t *b) { int64_t t = *a; *a = *b; *b = t; }
int64_t h_XchgI64(int64_t *p, int64_t v) { int64_t t = *p; *p = v; return t; }
int64_t h_LXchgI64(int64_t *p, int64_t v) { return h_XchgI64(p, v); }
uint32_t h_XchgU32(uint32_t *p, uint32_t v) { uint32_t t = *p; *p = v; return t; }
uint32_t h_LXchgU32(uint32_t *p, uint32_t v) { return h_XchgU32(p, v); }
uint16_t h_XchgU16(uint16_t *p, uint16_t v) { uint16_t t = *p; *p = v; return t; }
uint16_t h_LXchgU16(uint16_t *p, uint16_t v) { return h_XchgU16(p, v); }
uint8_t h_XchgU8(uint8_t *p, uint8_t v) { uint8_t t = *p; *p = v; return t; }
uint8_t h_LXchgU8(uint8_t *p, uint8_t v) { return h_XchgU8(p, v); }
void h_Fclex(void) {}
void h_Fldcw(uint16_t w) {}
uint16_t h_Fstcw(void) { return 0x037F; }
uint16_t h_Fstsw(void) { return 0; }

/* ---- CD3 vectors ---- */
c_CD3 *h_D3Equ(c_CD3 *d, double x, double y, double z) { d->m_x = x; d->m_y = y; d->m_z = z; return d; }
c_CD3 *h_D3Zero(c_CD3 *d) { return h_D3Equ(d, 0, 0, 0); }
c_CD3 *h_D3Copy(c_CD3 *d, c_CD3 *s) { *d = *s; return d; }
c_CD3 *h_D3Add(c_CD3 *r, c_CD3 *a, c_CD3 *b) { return h_D3Equ(r, a->m_x + b->m_x, a->m_y + b->m_y, a->m_z + b->m_z); }
c_CD3 *h_D3Sub(c_CD3 *r, c_CD3 *a, c_CD3 *b) { return h_D3Equ(r, a->m_x - b->m_x, a->m_y - b->m_y, a->m_z - b->m_z); }
c_CD3 *h_D3AddEqu(c_CD3 *r, c_CD3 *d) { return h_D3Add(r, r, d); }
c_CD3 *h_D3SubEqu(c_CD3 *r, c_CD3 *d) { return h_D3Sub(r, r, d); }
c_CD3 *h_D3Mul(c_CD3 *r, double s, c_CD3 *d) { return h_D3Equ(r, s * d->m_x, s * d->m_y, s * d->m_z); }
c_CD3 *h_D3MulEqu(c_CD3 *r, double s) { return h_D3Mul(r, s, r); }
c_CD3 *h_D3Div(c_CD3 *r, c_CD3 *d, double s) { return h_D3Equ(r, d->m_x / s, d->m_y / s, d->m_z / s); }
c_CD3 *h_D3DivEqu(c_CD3 *r, double s) { return h_D3Div(r, r, s); }
double h_D3Dot(c_CD3 *a, c_CD3 *b) { return a->m_x * b->m_x + a->m_y * b->m_y + a->m_z * b->m_z; }
double h_D3NormSqr(c_CD3 *d) { return h_D3Dot(d, d); }
double h_D3Norm(c_CD3 *d) { return sqrt(h_D3Dot(d, d)); }
double h_D3DistSqr(c_CD3 *a, c_CD3 *b) { c_CD3 t; h_D3Sub(&t, a, b); return h_D3Dot(&t, &t); }
double h_D3Dist(c_CD3 *a, c_CD3 *b) { return sqrt(h_D3DistSqr(a, b)); }
c_CD3 *h_D3Cross(c_CD3 *r, c_CD3 *a, c_CD3 *b)
{
  c_CD3 t;
  h_D3Equ(&t, a->m_y * b->m_z - a->m_z * b->m_y, a->m_z * b->m_x - a->m_x * b->m_z, a->m_x * b->m_y - a->m_y * b->m_x);
  *r = t;
  return r;
}
c_CD3 *h_D3Unit(c_CD3 *d)
{
  double n = h_D3Norm(d);
  return n ? h_D3DivEqu(d, n) : d;
}

/* ---- queues ---- */
void h_QueInit(c_CQue *h) { h->m_next = h->m_last = h; }
void h_QueIns(c_CQue *e, c_CQue *pred)
{
  c_CQue *n = pred->m_next;
  e->m_last = pred; e->m_next = n;
  n->m_last = e; pred->m_next = e;
}
void h_QueInsRev(c_CQue *e, c_CQue *succ)
{
  c_CQue *l = succ->m_last;
  e->m_next = succ; e->m_last = l;
  l->m_next = e; succ->m_last = e;
}
void h_QueRem(c_CQue *e)
{
  e->m_next->m_last = e->m_last;
  e->m_last->m_next = e->m_next;
}

/* ---- hash tables (KHashA.HC) ---- */
int64_t h_HashStr(uint8_t *s)
{
  uint64_t rbx = 0, rax;
  if (!s) return 0;
  for (; *s; s++) rbx = (rbx << 1) + *s + (rbx >> 63);   /* SHL1 RBX; ADC RBX,RAX */
  rax = rbx + (rbx >> 16) + ((rbx >> 15) & 1);           /* SHR RBX,16; ADC RAX,RBX */
  return (int64_t)rax;
}

TosPS *h_HashBucketFind(uint8_t *s, c_CHashTable *t)
{
  return &t->m_body[h_HashStr(s) & t->m_mask];
}

static c_CHash *single_find(uint8_t *s, int64_t hash, c_CHashTable *t, int64_t mask, int64_t *inst, c_CHash ***where)
{
  c_CHash **pp = (c_CHash **)&t->m_body[hash & t->m_mask], *h;
  for (; (h = *pp); pp = &h->m_next)
    if ((h->m_type & (uint32_t)mask) && !strcmp((char *)h->m_str, (char *)s) && !--*inst) {
      h->m_use_cnt++;
      if (where) *where = pp;
      return h;
    }
  return NULL;
}

c_CHash *h_HashSingleTableFind(uint8_t *s, c_CHashTable *t, int64_t mask, int64_t inst)
{
  if (inst <= 0 || !s) return NULL;
  return single_find(s, h_HashStr(s), t, mask, &inst, NULL);
}

c_CHash *h_HashFind(uint8_t *s, c_CHashTable *t, int64_t mask, int64_t inst)
{
  int64_t hash;
  c_CHash *h;
  if (inst <= 0 || !s) return NULL;
  hash = h_HashStr(s);
  for (; t; t = t->m_next)
    if ((h = single_find(s, hash, t, mask, &inst, NULL))) return h;
  return NULL;
}

void h_HashAdd(c_CHash *h, c_CHashTable *t)
{
  c_CHash **b = (c_CHash **)h_HashBucketFind(h->m_str, t);
  h->m_next = *b;
  *b = h;
}

void h_HashAddAfter(c_CHash *h, c_CHash *pred, c_CHashTable *t)
{ /* KHashA.HC: arg2 is the link to insert at */
  c_CHash **b = (c_CHash **)pred;
  h->m_next = *b;
  *b = h;
}

int8_t h_HashRemDel(c_CHash *h, c_CHashTable *t, int64_t inst)
{
  c_CHash **pp = NULL, *f;
  if (!h) return 0;
  f = single_find(h->m_str, h_HashStr(h->m_str), t, h->m_type & ~0xFF000000u, &inst, &pp);
  if (f != h) return 0;
  *pp = h->m_next;
  h_HashDel((c_CHashSrcSym *)h);
  return 1;
}

uint8_t h_DrvTextAttrGet(uint8_t drv_let) { return 0x0F; }   /* BLACK<<4|WHITE: no drives here */

/* KTask.HC's version also demands 0<task<=I32_MAX; host heaps can sit above 4 GB (always on Apple Silicon). */
int8_t h_TaskValidate(c_CTask *task)
{
  return (uintptr_t)task >= 4096 && task->m_addr == task && task->m_task_signature == TASK_SIG;
}

/* ---- CPU state that has no meaning here ---- */
c_CTask *h_Fs(void) { return tos_fs; }
c_CCPU *h_Gs(void) { return tos_gs; }
int64_t h_GetRFlags(void) { return 0x202; }   /* IF set */
void h_SetRFlags(int64_t f) {}
uint8_t *h_GetRBP(void) { return (uint8_t *)_AddressOfReturnAddress(); }
uint8_t *h_GetRSP(void) { return (uint8_t *)_AddressOfReturnAddress(); }
int64_t h_GetRAX(void) { return 0; }
void h_SetRAX(int64_t d) {}
int64_t h_Carry(void) { return 0; }
uint8_t *h_Caller(int64_t n) { return NULL; }
void h_CLFlush(uint8_t *a) {}
void h_CPUId(uint32_t rax, c_CRAXRBCRCXRDX *r)
{
  int v[4] = {0};
#ifdef _WIN32
  __cpuid(v, (int)rax);
#endif
  r->m_rax = (uint32_t)v[0]; r->m_rbx = (uint32_t)v[1]; r->m_rcx = (uint32_t)v[2]; r->m_rdx = (uint32_t)v[3];
}
int8_t h_DbgMode(int8_t val) { return 0; }
void h_Dbg(uint8_t *msg, int64_t num)
{
  void *pcs[12];
  int n = rt_backtrace(1, 12, pcs);
  fprintf(stderr, "TempleOS runtime: Dbg(\"%s\", %lld) from:\n", msg ? (char *)msg : "", (long long)num);
  rt_print_stack(pcs, n);
  exit(3);
}
void h_Panic(uint8_t *msg, int64_t num, int8_t panic)
{
  rt_fatal("Panic: %s %lld", msg ? (char *)msg : "", (long long)num);
}

int64_t h_CallExtNum(int64_t num, int64_t argc, int64_t *argv)
{ /* ext[] holds ordinary translated functions; pass the args as I64 registers */
  typedef int64_t (*F)(int64_t, int64_t, int64_t, int64_t, int64_t, int64_t);
  int64_t a[6] = {0};
  F f = (F)h_ext[num].p;
  if (!f) return 0;
  if (argc > 6) rt_fatal("CallExtNum: %lld args", (long long)argc);
  memcpy(a, argv, argc * 8);
  return f(a[0], a[1], a[2], a[3], a[4], a[5]);
}

/* ---- functions by name: the translator's TosFun tables (game first: it may shadow the lib) ---- */
extern const TosFun __tos_funs[], __tos_game_funs[];

uint8_t *h_TosFunFind(uint8_t *name)
{
  const TosFun *tabs[2] = {__tos_game_funs, __tos_funs};
  if (!name) return NULL;
  for (int t = 0; t < 2; t++)
    for (const TosFun *f = tabs[t]; f->name; f++)
      if (!strcmp(f->name, (const char *)name)) return (uint8_t *)f;
  return NULL;
}

int64_t h_TosFunCall(uint8_t *fun, int64_t argc, int64_t *argv, int64_t skip, int64_t m)
{
  return ((const TosFun *)fun)->call(argc, argv, (uint64_t)skip, (uint64_t)m);
}

int64_t h_TosFunAddr(uint8_t *fun) { return (int64_t)((const TosFun *)fun)->addr; }

int64_t h_CallExtStr(uint8_t *name, int64_t argc, int64_t *argv)
{ /* args are raw 8-byte slots, as HolyC passes them */
  uint8_t *f = h_TosFunFind(name);
  if (!f) {
    fprintf(stderr, "TempleOS runtime: CallExtStr(\"%s\"): no such function\n", name);
    return 0;
  }
  return h_TosFunCall(f, argc, argv, 0, 0);
}

/* ---- I/O ports: 8042 keyboard, VGA DAC, CMOS clock; others read as 0 ---- */
uint8_t rt_dac[256][3];
static uint8_t dac_widx, dac_ridx, dac_wc, dac_rc, cmos_reg;
static uint8_t kbd_q[256];
static unsigned kbd_head, kbd_tail;

void rt_kbd_push(uint8_t b) { if (kbd_tail - kbd_head < sizeof kbd_q) kbd_q[kbd_tail++ % sizeof kbd_q] = b; }
int rt_kbd_pending(void) { return kbd_tail != kbd_head; }

static uint8_t cmos_read(uint8_t reg)
{
#ifdef _WIN32   /* the no-CRT 32-bit build has no time()/localtime() */
  SYSTEMTIME s;
  struct tm t = {0}, *st = &t;
  GetLocalTime(&s);
  t.tm_sec = s.wSecond; t.tm_min = s.wMinute; t.tm_hour = s.wHour; t.tm_wday = s.wDayOfWeek;
  t.tm_mday = s.wDay; t.tm_mon = s.wMonth - 1; t.tm_year = s.wYear - 1900;
#else
  time_t now = time(NULL);
  struct tm *st = localtime(&now);
#endif
  switch (reg) {
  case 0: return (uint8_t)st->tm_sec;
  case 2: return (uint8_t)st->tm_min;
  case 4: return (uint8_t)st->tm_hour;
  case 6: return (uint8_t)st->tm_wday;
  case 7: return (uint8_t)st->tm_mday;
  case 8: return (uint8_t)(st->tm_mon + 1);
  case 9: return (uint8_t)(st->tm_year % 100);
  case 0x0B: return 0x06;   /* binary values, 24 hour */
  default: return 0;
  }
}

/* PC speaker: PIT channel 2 sets the pitch (0x43 mode, 0x42 divisor lo then hi), port 0x61 bits 0-1 gate it */
static uint8_t spk_port, pit_hi;
static uint16_t pit_div;
static void spk_update(void) { rt_spk((spk_port & 3) == 3 && pit_div ? 1193182 / pit_div : 0); }

uint8_t h_InU8(int64_t port)
{
  switch (port) {
  case 0x61: return spk_port;
  case 0x60: return rt_kbd_pending() ? kbd_q[kbd_head++ % sizeof kbd_q] : 0;
  case 0x64: return rt_kbd_pending() ? 1 : 0;
  case 0x71: return cmos_read(cmos_reg);
  case 0x3C9: {
    uint8_t v = rt_dac[dac_ridx][dac_rc];
    if (++dac_rc == 3) { dac_rc = 0; dac_ridx++; }
    return v;
  }
  }
  return 0;
}

void h_OutU8(int64_t port, int64_t val)
{
  switch (port) {
  case 0x43: pit_hi = 0; break;
  case 0x42:
    pit_div = pit_hi ? (uint16_t)((pit_div & 0xFF) | (val & 0xFF) << 8) : (uint16_t)((pit_div & 0xFF00) | (val & 0xFF));
    if ((pit_hi ^= 1) == 0) spk_update();
    break;
  case 0x61: spk_port = (uint8_t)val; spk_update(); break;
  case 0x70: cmos_reg = (uint8_t)(val & 0x7F); break;
  case 0x3C7: dac_ridx = (uint8_t)val; dac_rc = 0; break;
  case 0x3C8: dac_widx = (uint8_t)val; dac_wc = 0; break;
  case 0x3C9:
    rt_dac[dac_widx][dac_wc] = (uint8_t)(val & 63);
    if (++dac_wc == 3) { dac_wc = 0; dac_widx++; }
    break;
  }
}
uint16_t h_InU16(int64_t port) { return 0; }
uint32_t h_InU32(int64_t port) { return 0; }
void h_OutU16(int64_t port, int64_t val) {}
void h_OutU32(int64_t port, int64_t val) {}

/* ---- screen pieces whose HolyC used inline asm (GrScrn.HC) ---- */
#define ATTRF_BLINK  0x10000000
#define ATTRF_INVERT 0x20000000
#define ATTRF_SEL    0x40000000

void h_GrUpdateTextBG(void)
{ /* text background colors -> gr.dc2; the asm wrote one 8x8 cell per char */
  c_CDC *d2 = h_gr.m_dc2;
  int64_t w1 = d2->m_width_internal, w3 = 7 * w1, w4 = 0;
  int64_t num_rows = 60, num_cols = 80, row, col, i, j, k, cur, c;
  uint32_t *src = h_gr.m_text_base;
  int blink = h_Blink(2.5);
  uint8_t *dst = d2->m_body, *dst2 = dst, a;

  if (h_gr.m_pan_text_x || h_gr.m_hide_col) {
    h_gr.m_pan_text_x = h_ClampI64(h_gr.m_pan_text_x, -7, 7);
    j = h_AbsI64(h_gr.m_pan_text_x) / 8 + 1;
    num_cols -= j;
    if (h_gr.m_pan_text_x < 0) { src += j; i = 8 * j + h_gr.m_pan_text_x; }
    else i = h_gr.m_pan_text_x;
    dst2 = dst + i;
    w4 = j;
    w3 += j * 8;
    j *= 8;
    dst = d2->m_body;
    for (row = num_rows * 8; row--;) {
      for (col = i; col--;) *dst++ = 0;
      dst += w1 - i - j;
      for (col = j; col--;) *dst++ = 0;
    }
  }
  dst = dst2;
  if (h_gr.m_pan_text_y || h_gr.m_hide_row) {
    h_gr.m_pan_text_y = h_ClampI64(h_gr.m_pan_text_y, -7, 7);
    j = h_AbsI64(h_gr.m_pan_text_y) / 8 + 1;
    num_rows -= j;
    if (h_gr.m_pan_text_y < 0) { src += w1 / 8 * j; i = w1 * (8 * j + h_gr.m_pan_text_y); }
    else i = w1 * h_gr.m_pan_text_y;
    dst2 = dst + i;
    j *= w1 * 8;
    dst = d2->m_body;
    for (row = i; row--;) *dst++ = 0;
    dst = d2->m_body + 60 * 80 * 8 * 8 - j;
    for (row = j; row--;) *dst++ = 0;
  }
  dst = dst2;
  for (row = num_rows; row--;) {
    for (col = num_cols; col--;) {
      cur = *src++;
      a = (uint8_t)(cur >> 8);
      if (cur & (ATTRF_SEL | ATTRF_INVERT | ATTRF_BLINK)) {
        if (cur & ATTRF_SEL) a ^= 0xFF;
        if (cur & ATTRF_INVERT) a = (uint8_t)((a << 4) + (a >> 4));
        if (cur & ATTRF_BLINK && blink) a = (uint8_t)((a << 4) + (a >> 4));
      }
      c = h_gr.m_to_8_colors[a >> 4];
      for (k = 0; k < 8; k++) memcpy(dst + k * w1, &c, 8);
      dst += 8;
    }
    src += w4;
    dst += w3;
  }
}

void h_DCBlotColor4(uint8_t *dst, int64_t *img, int64_t *img_cache, int64_t cnt) {}   /* the host shows gr.dc2 directly */

/* No compiler at run time, so no source position to record on hash entries. */
void h_HashSrcFileSet(c_CCmpCtrl *cc, c_CHashSrcSym *h, int64_t line_num_offset) {}

/* The translator's #define table (gen/<Game>.c), so strings parsed at run time can use them. */
extern const char *const __tos_defines[];
void h_TosDefinesLoad(void)
{
  const char *const *d;
  for (d = __tos_defines; *d; d += 2) h_DefineLoad((uint8_t *)d[0], (uint8_t *)d[1]);
}
