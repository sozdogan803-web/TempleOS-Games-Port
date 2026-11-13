/* Windows host: process start-up, the window, keyboard/mouse, the clock, and
   TempleOS's task switcher (one fiber per CTask).
   The 32-bit build runs on Windows 95 and later, so it calls only what Windows 95 and NT 4
   export; newer functions (fibers on 95, vectored exceptions, dbghelp) are looked up at run time. */
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <mmsystem.h>
#include <dbghelp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rt.h"

void __tos_game_main(void);
int64_t h_tos_fixed_area;

/* ---- clock: GetTSC is the performance counter; jiffies are milliseconds ---- */
static LARGE_INTEGER qpf, qpc0;
static int64_t qpc_now(void) { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart - qpc0.QuadPart; }
int64_t h_GetTSC(void) { return qpc_now(); }
int64_t h_SysTimerRead(void) { return (int64_t)((double)qpc_now() * (18333 * 65536 / 1000) / qpf.QuadPart); }
int64_t h_HPET(void) { return 0; }

/* ---- window ---- */
static HWND hwnd;
static int ready, ms_x = 320, ms_y = 240, ms_z, ms_l, ms_r;
static struct { BITMAPINFOHEADER h; RGBQUAD pal[256]; } bmi;

/* Testing aid: TOS_SHOT=<file.bmp> [TOS_SHOT_MS=<ms>] saves the frame shown after that
   many ms (default 4000) and exits. */
static void shot(const uint8_t *bits)
{
  static int64_t when = -1;
  const char *path = getenv("TOS_SHOT");
  BITMAPFILEHEADER fh = {0};
  DWORD n = (DWORD)bmi.h.biWidth * (DWORD)(bmi.h.biHeight < 0 ? -bmi.h.biHeight : bmi.h.biHeight);
  FILE *f;
  if (!path) return;
  if (when < 0) when = getenv("TOS_SHOT_MS") ? _atoi64(getenv("TOS_SHOT_MS")) : 4000;
  if (h_cnts.m_jiffies < when || !(f = fopen(path, "wb"))) return;
  fh.bfType = 0x4D42;
  fh.bfOffBits = sizeof fh + sizeof bmi;
  fh.bfSize = fh.bfOffBits + n;
  fwrite(&fh, sizeof fh, 1, f);
  fwrite(&bmi, sizeof bmi, 1, f);
  fwrite(bits, 1, n, f);
  fclose(f);
  ExitProcess(0);
}

static RECT dest_rect(void)
{ /* the 640x480 screen, as large as the client area allows at 4:3, centered */
  RECT rc, d;
  int w, h;
  GetClientRect(hwnd, &rc);
  if (rc.right * 480 > rc.bottom * 640) { h = rc.bottom; w = h * 640 / 480; }
  else { w = rc.right; h = w * 480 / 640; }
  d.left = (rc.right - w) / 2; d.top = (rc.bottom - h) / 2;
  d.right = d.left + w; d.bottom = d.top + h;
  return d;
}

static void present(HDC dc)
{
  c_CDC *d = h_gr.m_dc2;
  const uint8_t *bits;
  RECT rc, r;
  int i;
  if (!ready || !d) return;
  for (i = 0; i < 256; i++) {   /* dc2 holds 4-bit colors; the DAC maps them to RGB */
    uint8_t *c = rt_dac[i & 15];
    bmi.pal[i].rgbRed = c[0] << 2 | c[0] >> 4;
    bmi.pal[i].rgbGreen = c[1] << 2 | c[1] >> 4;
    bmi.pal[i].rgbBlue = c[2] << 2 | c[2] >> 4;
  }
  bmi.h.biSize = sizeof bmi.h;
  bmi.h.biWidth = d->m_width_internal;
  bmi.h.biPlanes = 1;
  bmi.h.biBitCount = 8;
  bmi.h.biClrUsed = 256;
#ifdef _WIN64
  bmi.h.biHeight = -d->m_height;   /* top-down */
  bits = d->m_body;
#else
  { /* bottom-up rows: top-down DIBs are newer than some of the Windows versions this build is for */
    static uint8_t *flip;
    static size_t flip_n;
    size_t row = (size_t)d->m_width_internal, n = row * (size_t)d->m_height;
    int64_t y;
    if (flip_n < n) { free(flip); flip = malloc(n); flip_n = n; }
    for (y = 0; y < d->m_height; y++) memcpy(flip + (size_t)(d->m_height - 1 - y) * row, d->m_body + (size_t)y * row, row);
    bmi.h.biHeight = (LONG)d->m_height;
    bits = flip;
  }
#endif
  GetClientRect(hwnd, &rc);
  r = dest_rect();
  PatBlt(dc, 0, 0, rc.right, r.top, BLACKNESS);   /* letterbox bars */
  PatBlt(dc, 0, r.bottom, rc.right, rc.bottom - r.bottom, BLACKNESS);
  PatBlt(dc, 0, r.top, r.left, r.bottom - r.top, BLACKNESS);
  PatBlt(dc, r.right, r.top, rc.right - r.right, r.bottom - r.top, BLACKNESS);
  SetStretchBltMode(dc, COLORONCOLOR);
  StretchDIBits(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, 0, 0, d->m_width, d->m_height, bits,
                (BITMAPINFO *)&bmi, DIB_RGB_COLORS, SRCCOPY);
  shot(bits);
}

void h_GrUpdateVGAGraphics(void)
{
  HDC dc = GetDC(hwnd);
  present(dc);
  ReleaseDC(hwnd, dc);
}

static void mouse(LPARAM lp, WPARAM wp, int has_pos)
{
  RECT r = dest_rect();
  if (has_pos && r.right > r.left && r.bottom > r.top) {
    ms_x = (GET_X_LPARAM(lp) - r.left) * 640 / (r.right - r.left);
    ms_y = (GET_Y_LPARAM(lp) - r.top) * 480 / (r.bottom - r.top);
    ms_x = ms_x < 0 ? 0 : ms_x > 639 ? 639 : ms_x;
    ms_y = ms_y < 0 ? 0 : ms_y > 479 ? 479 : ms_y;
  }
  ms_l = (GetKeyState(VK_LBUTTON) & 0x8000) != 0;
  ms_r = (GetKeyState(VK_RBUTTON) & 0x8000) != 0;
  if (ms_l || ms_r) SetCapture(hwnd); else ReleaseCapture();
  if (ready) h_MsHardSet(ms_x, ms_y, ms_z, ms_l, ms_r);
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
  switch (m) {
  case WM_CLOSE: ExitProcess(0);
  case WM_KEYDOWN: case WM_SYSKEYDOWN: case WM_KEYUP: case WM_SYSKEYUP:
    if (m == WM_SYSKEYDOWN && wp == VK_F4) ExitProcess(0);
    if (ready) {   /* PC scan code set 1, as the 8042 would deliver it */
      if (lp >> 24 & 1) rt_kbd_push(0xE0);
      rt_kbd_push((uint8_t)((lp >> 16 & 0x7F) | (lp >> 31 & 1) << 7));
    }
    return 0;   /* no Alt menu loop */
  case WM_MOUSEMOVE: case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_RBUTTONDOWN: case WM_RBUTTONUP:
    mouse(lp, wp, 1);
    return 0;
  case WM_MOUSEWHEEL:
    ms_z -= GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
    mouse(lp, wp, 0);
    return 0;
  case WM_SETCURSOR:
    if (LOWORD(lp) == HTCLIENT) { SetCursor(NULL); return TRUE; }   /* TempleOS draws its own pointer */
    break;
  case WM_ERASEBKGND: return 1;
  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps);
    present(dc);
    EndPaint(h, &ps);
    return 0;
  }
  }
  return DefWindowProcA(h, m, wp, lp);
}

static void make_window(void)
{
  WNDCLASSA wc = {0};
  RECT r = {0, 0, 640, 480}, wa;
  int s, fit;
  char title[MAX_PATH], *p;
  GetModuleFileNameA(NULL, title, MAX_PATH);
  if ((p = strrchr(title, '\\'))) memmove(title, p + 1, strlen(p + 1) + 1);
  if ((p = strrchr(title, '.'))) *p = 0;
  wc.lpfnWndProc = wndproc;
  wc.hInstance = GetModuleHandleA(NULL);
  wc.lpszClassName = "TempleOSGame";
  wc.hIcon = LoadIconA(wc.hInstance, MAKEINTRESOURCEA(1));   /* the game's TempleOS icon (build.cmd links it in) */
  if (!wc.hIcon) wc.hIcon = LoadIconA(NULL, IDI_APPLICATION);
  RegisterClassA(&wc);
  if (!SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0))   /* whole-number scale that fits 90% of the screen */
    SetRect(&wa, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
  s = (wa.right - wa.left) * 9 / 10 / 640;
  if ((wa.bottom - wa.top) * 9 / 10 / 480 < s) s = (wa.bottom - wa.top) * 9 / 10 / 480;
  if (!(fit = s >= 1)) s = 1;
  r.right *= s; r.bottom *= s;
  AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
  r.right -= r.left; r.bottom -= r.top;   /* now the window's width and height */
  r.left = wa.left + (wa.right - wa.left - r.right) / 2;
  r.top = wa.top + (wa.bottom - wa.top - r.bottom) / 2;
  hwnd = CreateWindowA(wc.lpszClassName, title, WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                       r.left > wa.left ? r.left : wa.left, r.top > wa.top ? r.top : wa.top,
                       r.right, r.bottom, NULL, NULL, wc.hInstance, NULL);
  if (!fit) ShowWindow(hwnd, SW_MAXIMIZE);   /* e.g. a 640x480 screen: shrink the picture a little to fit */
}

/* Testing aid: TOS_KEYS="1500:39,1600:B9" feeds scan-code bytes (hex) at those ms. */
static void scripted_keys(void)
{
  static const char *p;
  static int init;
  char *end;
  int64_t at;
  if (!init) { init = 1; p = getenv("TOS_KEYS"); }
  while (p && *p) {
    at = _strtoi64(p, &end, 10);
    if (*end != ':' || at > h_cnts.m_jiffies) return;
    rt_kbd_push((uint8_t)strtoul(end + 1, &end, 16));
    p = *end == ',' ? end + 1 : NULL;
  }
}

static void pump(void)
{
  MSG msg;
  while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
  scripted_keys();
  while (rt_kbd_pending()) h_KbdPktRead();   /* the keyboard IRQ's job: one call per byte at port 0x60 */
}

static int64_t last_pump;
void rt_tick(void)
{
  int64_t t = qpc_now();
  h_cnts.m_jiffies = t * 1000 / qpf.QuadPart;
  if (t - last_pump >= qpf.QuadPart / 500) { last_pump = t; pump(); }
}

void rt_idle(void)
{
  Sleep(1);
  last_pump = 0;
  rt_tick();
}
void h_SysHlt(void) { rt_idle(); }

/* ---- fibers ---- */
static void WINAPI fiber_main(void *p);
#ifdef _WIN64
static void *fiber_new(void *r) { return CreateFiberEx(64 << 10, 8 << 20, FIBER_FLAG_FLOAT_SWITCH, fiber_main, r); }
static void *fiber_of_thread(void) { return ConvertThreadToFiberEx(NULL, FIBER_FLAG_FLOAT_SWITCH); }
#define fiber_switch SwitchToFiber
#define fiber_del DeleteFiber
#else
/* Windows' fibers where it has them (98, NT 3.51 SP3 and later); on Windows 95 a stack switch
   of our own, which TOS_NOFIBERS=1 also selects (to test it). Tasks only switch inside
   fiber_switch, at a call, so the x87 stack is empty and no FPU state needs saving. */
static LPVOID (WINAPI *k_CreateFiber)(SIZE_T, LPFIBER_START_ROUTINE, LPVOID);
static LPVOID (WINAPI *k_ConvertThreadToFiber)(LPVOID);
static VOID (WINAPI *k_SwitchToFiber)(LPVOID);
static VOID (WINAPI *k_DeleteFiber)(LPVOID);

typedef struct Ctx { void *esp, *seh, *stk_base, *stk_limit; uint8_t *mem; } Ctx;   /* offsets used by ctx_switch */
static Ctx *ctx_cur;
#define CTX_STACK (4 << 20)

#pragma warning(disable: 4733)   /* ctx_switch sets the SEH head (fs:[0]) on purpose */
static __declspec(naked) void __fastcall ctx_switch(Ctx *from, Ctx *to)
{ /* save callee-saved registers and the TIB's SEH head and stack bounds; load to's */
  __asm {
    push ebp
    push ebx
    push esi
    push edi
    mov eax, fs:[0]
    mov [ecx+4], eax
    mov eax, fs:[4]
    mov [ecx+8], eax
    mov eax, fs:[8]
    mov [ecx+12], eax
    mov [ecx], esp
    mov esp, [edx]
    mov eax, [edx+4]
    mov fs:[0], eax
    mov eax, [edx+8]
    mov fs:[4], eax
    mov eax, [edx+12]
    mov fs:[8], eax
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret
  }
}
static LPVOID WINAPI my_ConvertThreadToFiber(LPVOID p) { (void)p; return ctx_cur = calloc(1, sizeof(Ctx)); }
static LPVOID WINAPI my_CreateFiber(SIZE_T n, LPFIBER_START_ROUTINE fn, LPVOID arg)
{
  Ctx *c = calloc(1, sizeof *c);
  uintptr_t *sp;
  (void)n;
  if (!c || !(c->mem = VirtualAlloc(NULL, CTX_STACK, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE))) { free(c); return NULL; }
  sp = (uintptr_t *)(c->mem + CTX_STACK - 16);
  *--sp = (uintptr_t)arg;   /* fn's argument */
  *--sp = 0;                /* fn's return address: it never returns */
  *--sp = (uintptr_t)fn;    /* where ctx_switch's ret goes */
  sp -= 4;                  /* ebp ebx esi edi */
  memset(sp, 0, 16);
  c->esp = sp;
  c->seh = (void *)(intptr_t)-1;   /* empty SEH chain */
  c->stk_base = c->mem + CTX_STACK;
  c->stk_limit = c->mem;
  return c;
}
static VOID WINAPI my_SwitchToFiber(LPVOID to)
{
  Ctx *from = ctx_cur;
  ctx_cur = to;
  ctx_switch(from, to);
}
static VOID WINAPI my_DeleteFiber(LPVOID p)
{
  Ctx *c = p;
  if (c->mem) VirtualFree(c->mem, 0, MEM_RELEASE);
  free(c);
}
static void fibers_init(void)
{
  HMODULE k = GetModuleHandleA("kernel32.dll");
  k_CreateFiber = (void *)GetProcAddress(k, "CreateFiber");
  k_ConvertThreadToFiber = (void *)GetProcAddress(k, "ConvertThreadToFiber");
  k_SwitchToFiber = (void *)GetProcAddress(k, "SwitchToFiber");
  k_DeleteFiber = (void *)GetProcAddress(k, "DeleteFiber");
  if (!k_CreateFiber || !k_ConvertThreadToFiber || !k_SwitchToFiber || !k_DeleteFiber || getenv("TOS_NOFIBERS")) {
    k_CreateFiber = my_CreateFiber;
    k_ConvertThreadToFiber = my_ConvertThreadToFiber;
    k_SwitchToFiber = my_SwitchToFiber;
    k_DeleteFiber = my_DeleteFiber;
  }
}
static void *fiber_new(void *r) { return k_CreateFiber(64 << 10, fiber_main, r); }
static void *fiber_of_thread(void) { fibers_init(); return k_ConvertThreadToFiber(NULL); }
#define fiber_switch k_SwitchToFiber
#define fiber_del k_DeleteFiber
#endif

/* ---- tasks on fibers (Sched.HC's _YIELD and _TASK_END_NOW) ---- */
typedef struct TaskRt {
  c_CTask *task;
  void *fiber;
  TosTry *try_top;
  void (*entry)(int64_t);   /* Spawn's function and data (an 8-byte slot, see FIXES in tools/hc_lex.py) */
  int64_t data;
  struct TaskRt *next;
} TaskRt;
static TaskRt *rts, *cur_rt;
static void *grave;   /* fiber of a task that ended itself; deleted once off its stack */
static int in_end;    /* inside TaskEnd, which runs from the scheduler */

void __tos_pause(void)
{ /* PAUSE spins on a lock another task holds; on one host thread that task must get to run */
  if (!in_end) h_Yield();
}

static TaskRt *rt_of(c_CTask *t, int create)
{
  TaskRt *r;
  for (r = rts; r; r = r->next)
    if (r->task == t) return r;
  if (!create) return NULL;
  r = calloc(1, sizeof *r);
  r->task = t;
  r->entry = (void (*)(int64_t))t->m_rip;   /* Spawn put the start address in rip */
  r->data = ((int64_t *)t->m_rsp)[1];       /* ... and pushed data, then &Exit */
  r->next = rts;
  rts = r;
  return r;
}

static void bury(void)
{
  if (grave) { fiber_del(grave); grave = NULL; }
}

static void WINAPI fiber_main(void *p)
{
  TaskRt *r = p;
  bury();
  r->entry(r->data);
  h_Exit();
}

static void switch_to(c_CTask *t)
{
  TaskRt *r = rt_of(t, 1);
  if (r == cur_rt) return;
  if (cur_rt) cur_rt->try_top = tos_try_top;
  cur_rt = r;
  tos_fs = t;
  tos_try_top = r->try_top;
  if (!r->fiber) r->fiber = fiber_new(r);
  if (!r->fiber) rt_fatal("CreateFiber failed (%lu)", GetLastError());
  fiber_switch(r->fiber);
  bury();
}

static c_CTask *end_task(c_CTask *t)
{ /* run TaskEnd as task t; returns where to keep looking */
  c_CTask *save = tos_fs, *next;
  TaskRt *r;
  tos_fs = t;
  in_end++;
  next = (c_CTask *)h_TaskEnd();
  in_end--;
  tos_fs = save;
  r = rt_of(t, 0);
  if (t->m_task_signature != TASK_SIG) {   /* left the run queue */
    if (r) {
      TaskRt **pp = &rts;
      while (*pp != r) pp = &(*pp)->next;
      *pp = r->next;
      if (r == cur_rt) { grave = r->fiber; cur_rt = NULL; }
      else if (r->fiber) fiber_del(r->fiber);
      free(r);
    }
  } else if (next == t && !(t->m_task_flags >> TASKf_KILL_TASK & 1)) {   /* task_end_cb: restart at rip */
    if (r && r == cur_rt) {
      tos_try_top = NULL;
      ((void (*)(void))t->m_rip)();
      h_Exit();
    } else if (r) {
      if (r->fiber) fiber_del(r->fiber);
      r->fiber = NULL;
      r->entry = (void (*)(int64_t))t->m_rip;
      r->data = 0;
      r->try_top = NULL;
    }
  }
  return next;
}

static c_CTask *pick(c_CTask *t)
{
  int laps = 0;
  for (;;) {
    if (t->m_task_flags >> TASKf_KILL_TASK & 1) { t = end_task(t); continue; }
    if (!(t->m_task_flags & (1u << TASKf_AWAITING_MSG | 1u << TASKf_SUSPENDED)) &&
        t->m_wake_jiffy <= h_cnts.m_jiffies)
      return t;
    t = t->m_next_task;
    if (t == tos_gs->m_seth_task && laps++) rt_idle();   /* a whole lap with nothing ready */
  }
}

void h_Yield(void)
{
  rt_tick();
  switch_to(pick(tos_fs->m_next_task));
}

void h_TaskEndNow(void)
{
  switch_to(pick(end_task(tos_fs)));
  for (;;) h_Yield();   /* still alive (children not dead yet): carry on as a dying task */
}

/* ---- game entry/exit, called from TosStart.HC ---- */
void h_TosGameMain(void) { __tos_game_main(); }
void h_TosQuit(int8_t by_except)
{
  if (by_except) rt_report_except("game stopped by");
  ExitProcess(by_except ? 5 : 0);
}

/* ---- crash reports: symbols come from dbghelp.dll and the .pdb when both are there ---- */
static BOOL (WINAPI *d_SymFromAddr)(HANDLE, DWORD64, PDWORD64, PSYMBOL_INFO);
static BOOL (WINAPI *d_SymGetLineFromAddr64)(HANDLE, DWORD64, PDWORD, PIMAGEHLP_LINE64);

static void print_frame(DWORD64 pc)
{
  static int sym_ready;
  HANDLE pr = GetCurrentProcess();
  char buf[sizeof(SYMBOL_INFO) + 256];
  SYMBOL_INFO *si = (SYMBOL_INFO *)buf;
  IMAGEHLP_LINE64 ln;
  DWORD64 disp;
  DWORD d32;
  if (!sym_ready++) {
    HMODULE m = LoadLibraryA("dbghelp.dll");
    BOOL (WINAPI *init)(HANDLE, PCSTR, BOOL) = m ? (void *)GetProcAddress(m, "SymInitialize") : NULL;
    DWORD (WINAPI *opts)(DWORD) = m ? (void *)GetProcAddress(m, "SymSetOptions") : NULL;
    if (init && opts) {
      opts(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
      if (init(pr, NULL, TRUE)) {
        d_SymFromAddr = (void *)GetProcAddress(m, "SymFromAddr");
        d_SymGetLineFromAddr64 = (void *)GetProcAddress(m, "SymGetLineFromAddr64");
      }
    }
  }
  si->SizeOfStruct = sizeof(SYMBOL_INFO);
  si->MaxNameLen = 255;
  ln.SizeOfStruct = sizeof ln;
  if (d_SymFromAddr && d_SymFromAddr(pr, pc, &disp, si)) fprintf(stderr, "  %s", si->Name);
  else fprintf(stderr, "  %p", (void *)(uintptr_t)pc);
  if (d_SymGetLineFromAddr64 && d_SymGetLineFromAddr64(pr, pc, &d32, &ln)) fprintf(stderr, "  %s:%lu", ln.FileName, ln.LineNumber);
  fputc('\n', stderr);
}

void rt_print_stack(void **pcs, int n)
{
  int i;
  for (i = 0; i < n; i++) print_frame((DWORD64)(uintptr_t)pcs[i]);
  fflush(stderr);
}

#ifndef _WIN64
static int ebp_walk(uintptr_t fp, void **pcs, int n)
{ /* x86 is built with frame pointers (/Oy-): follow the EBP chain within this stack */
  NT_TIB *tib = (NT_TIB *)NtCurrentTeb();
  uintptr_t lo = (uintptr_t)tib->StackLimit, hi = (uintptr_t)tib->StackBase;
  int i = 0;
  while (i < n && fp >= lo && fp + 8 <= hi && !(fp & 3)) {
    pcs[i++] = ((void **)fp)[1];
    if (((uintptr_t *)fp)[0] <= fp) break;
    fp = ((uintptr_t *)fp)[0];
  }
  return i;
}
#endif

int rt_backtrace(int skip, int n, void **pcs)
{ /* return addresses of the callers of rt_backtrace's caller, after skipping skip of them */
#ifdef _WIN64
  return RtlCaptureStackBackTrace(skip + 1, n, pcs, NULL);
#else
  void *all[64];
  uintptr_t fp;
  int k;
  __asm mov fp, ebp
  k = ebp_walk(fp, all, n + skip < 64 ? n + skip : 64);   /* all[0] is in our caller */
  k = k > skip ? k - skip : 0;
  memcpy(pcs, all + skip, k * sizeof *pcs);
  return k;
#endif
}

static LONG WINAPI crash(EXCEPTION_POINTERS *ep)
{ /* print a symbolized stack, then exit: the translated code has no other debugger */
  CONTEXT c = *ep->ContextRecord;
  int i;
  fprintf(stderr, "TempleOS runtime: exception %08lX at %p in task '%s'\n", ep->ExceptionRecord->ExceptionCode,
          ep->ExceptionRecord->ExceptionAddress, tos_fs ? (char *)tos_fs->m_task_name : "?");
  if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2)
    fprintf(stderr, "  %s address %p\n", ep->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading",
            (void *)ep->ExceptionRecord->ExceptionInformation[1]);
#ifdef _WIN64
  for (i = 0; i < 32 && c.Rip; i++) {
    PRUNTIME_FUNCTION f;
    DWORD64 base;
    print_frame(c.Rip);
    f = RtlLookupFunctionEntry(c.Rip, &base, NULL);
    if (!f) { c.Rip = *(DWORD64 *)c.Rsp; c.Rsp += 8; }
    else { void *hd; DWORD64 ef; RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, c.Rip, f, &c, &hd, &ef, NULL); }
  }
#else
  {
    void *pcs[32];
    int n = ebp_walk(c.Ebp, pcs, 32);
    print_frame(c.Eip);
    for (i = 0; i < n; i++) print_frame((DWORD64)(uintptr_t)pcs[i]);
  }
#endif
  fflush(stderr);
  ExitProcess(4);
  return EXCEPTION_EXECUTE_HANDLER;
}
static LONG WINAPI first_chance(EXCEPTION_POINTERS *ep)
{ /* heap corruption goes straight to the debugger, past SetUnhandledExceptionFilter; on 32-bit,
     tasks' stacks have no handler of Windows' own, so their faults are caught here too */
  DWORD code = ep->ExceptionRecord->ExceptionCode;
  if (code == 0xC0000374) return crash(ep);
#ifndef _WIN64
  if (code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_ILLEGAL_INSTRUCTION || code == EXCEPTION_INT_DIVIDE_BY_ZERO ||
      code == EXCEPTION_STACK_OVERFLOW || code == EXCEPTION_PRIV_INSTRUCTION || code == EXCEPTION_INT_OVERFLOW) {
    MEMORY_BASIC_INFORMATION mi;   /* only faults in this program's own code */
    if (VirtualQuery(ep->ExceptionRecord->ExceptionAddress, &mi, sizeof mi) && mi.AllocationBase == GetModuleHandleA(NULL))
      return crash(ep);
  }
#endif
  return EXCEPTION_CONTINUE_SEARCH;
}

int main(void)
{
  c_CSysFixedArea *fa;
  c_CTask *adam;
  c_CCPU *cpu;
  uint8_t *vga;
  TaskRt *r;
  PVOID (WINAPI *add_veh)(ULONG, PVECTORED_EXCEPTION_HANDLER) =
    (void *)GetProcAddress(GetModuleHandleA("kernel32.dll"), "AddVectoredExceptionHandler");   /* XP and later */

  SetUnhandledExceptionFilter(crash);
  if (add_veh) add_veh(1, first_chance);
  timeBeginPeriod(1);
  rt_snd_init();
  QueryPerformanceFrequency(&qpf);
  QueryPerformanceCounter(&qpc0);

  fa = calloc(1, sizeof *fa);   /* SYS_FIXED_AREA (prelude.HC points it here) */
  h_tos_fixed_area = (int64_t)(uintptr_t)fa;
  vga = calloc(1, 0x20000);
  h_dev.m_uncached_alias = vga - 0xA0000;   /* text.vga_alias = uncached_alias + 0xA0000 */

  adam = &fa->m_adam;
  cpu = &fa->m_boot_cpu;
  cpu->m_addr = cpu;
  cpu->m_seth_task = adam;
  cpu->m_next_dying = cpu->m_last_dying = (c_CTaskDying *)&cpu->m_next_dying;
  h_cpu_structs = cpu;
  h_sys_code_bp = &fa->m_sys_code_bp;
  adam->m_task_signature = TASK_SIG;
  adam->m_gs = cpu;
  adam->m_code_heap = adam->m_data_heap = h_HeapCtrlInit(&fa->m_adam_hc, adam, &fa->m_sys_code_bp);
  tos_fs = adam;
  tos_gs = cpu;
  h_adam_task = adam;
  h_cnts.m_time_stamp_freq = h_cnts.m_time_stamp_freq_initial = qpf.QuadPart;
  h_cnts.m_time_stamp_kHz_freq = qpf.QuadPart / 1000;
  h_cnts.m_time_stamp_calibrated = 1;
  h_TaskInit(adam, 0);

  r = calloc(1, sizeof *r);
  r->task = adam;
  r->fiber = fiber_of_thread();
  rts = cur_rt = r;

  make_window();
  __init_lib();    /* kernel + Adam top-level code, TosBoot, TosStart (spawns the game task) */
  ready = 1;
  h_SrvTaskCont(); /* Adam's server loop; never returns */
  return 0;
}
