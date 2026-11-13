/* Windows host: process start-up, the window, keyboard/mouse, the clock, and
   TempleOS's task switcher on Win32 fibers (one fiber per CTask). */
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <dbghelp.h>
#include <stdio.h>
#include <stdlib.h>
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
static void shot(c_CDC *d)
{
  static int64_t when = -1;
  const char *path = getenv("TOS_SHOT");
  BITMAPFILEHEADER fh = {0};
  FILE *f;
  if (!path) return;
  if (when < 0) when = getenv("TOS_SHOT_MS") ? _atoi64(getenv("TOS_SHOT_MS")) : 4000;
  if (h_cnts.m_jiffies < when || !(f = fopen(path, "wb"))) return;
  fh.bfType = 0x4D42;
  fh.bfOffBits = sizeof fh + sizeof bmi;
  fh.bfSize = fh.bfOffBits + (DWORD)(d->m_width_internal * d->m_height);
  fwrite(&fh, sizeof fh, 1, f);
  fwrite(&bmi, sizeof bmi, 1, f);
  fwrite(d->m_body, 1, (size_t)(d->m_width_internal * d->m_height), f);
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
  bmi.h.biHeight = -d->m_height;
  bmi.h.biPlanes = 1;
  bmi.h.biBitCount = 8;
  bmi.h.biClrUsed = 256;
  GetClientRect(hwnd, &rc);
  r = dest_rect();
  PatBlt(dc, 0, 0, rc.right, r.top, BLACKNESS);   /* letterbox bars */
  PatBlt(dc, 0, r.bottom, rc.right, rc.bottom - r.bottom, BLACKNESS);
  PatBlt(dc, 0, r.top, r.left, r.bottom - r.top, BLACKNESS);
  PatBlt(dc, r.right, r.top, rc.right - r.right, r.bottom - r.top, BLACKNESS);
  SetStretchBltMode(dc, COLORONCOLOR);
  StretchDIBits(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, 0, 0, d->m_width, d->m_height, d->m_body,
                (BITMAPINFO *)&bmi, DIB_RGB_COLORS, SRCCOPY);
  shot(d);
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
  return DefWindowProcW(h, m, wp, lp);
}

static void make_window(void)
{
  WNDCLASSW wc = {0};
  RECT r = {0, 0, 640, 480}, wa;
  int s;
  wchar_t title[MAX_PATH], *p;
  GetModuleFileNameW(NULL, title, MAX_PATH);
  if ((p = wcsrchr(title, L'\\'))) wcscpy(title, p + 1);
  if ((p = wcsrchr(title, L'.'))) *p = 0;
  wc.lpfnWndProc = wndproc;
  wc.hInstance = GetModuleHandleW(NULL);
  wc.lpszClassName = L"TempleOSGame";
  wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));   /* the game's TempleOS icon (build.cmd links it in) */
  if (!wc.hIcon) wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
  RegisterClassW(&wc);
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);   /* whole-number scale that fits 90% of the screen */
  s = (wa.right - wa.left) * 9 / 10 / 640;
  if ((wa.bottom - wa.top) * 9 / 10 / 480 < s) s = (wa.bottom - wa.top) * 9 / 10 / 480;
  if (s < 1) s = 1;
  r.right *= s; r.bottom *= s;
  AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
  r.right -= r.left; r.bottom -= r.top;   /* now the window's width and height */
  r.left = wa.left + (wa.right - wa.left - r.right) / 2;
  r.top = wa.top + (wa.bottom - wa.top - r.bottom) / 2;
  hwnd = CreateWindowW(wc.lpszClassName, title, WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                       r.left > wa.left ? r.left : wa.left, r.top > wa.top ? r.top : wa.top,
                       r.right, r.bottom, NULL, NULL, wc.hInstance, NULL);
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
  while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
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

/* ---- tasks on fibers (Sched.HC's _YIELD and _TASK_END_NOW) ---- */
typedef struct TaskRt {
  c_CTask *task;
  void *fiber;
  TosTry *try_top;
  void (*entry)(uint8_t *);
  uint8_t *data;
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
  r->entry = (void (*)(uint8_t *))t->m_rip;              /* Spawn put the start address in rip */
  r->data = (uint8_t *)((int64_t *)t->m_rsp)[1];         /* ... and pushed data, then &Exit */
  r->next = rts;
  rts = r;
  return r;
}

static void bury(void)
{
  if (grave) { DeleteFiber(grave); grave = NULL; }
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
  if (!r->fiber) r->fiber = CreateFiberEx(64 << 10, 8 << 20, FIBER_FLAG_FLOAT_SWITCH, fiber_main, r);
  if (!r->fiber) rt_fatal("CreateFiber failed (%lu)", GetLastError());
  SwitchToFiber(r->fiber);
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
      else if (r->fiber) DeleteFiber(r->fiber);
      free(r);
    }
  } else if (next == t && !(t->m_task_flags >> TASKf_KILL_TASK & 1)) {   /* task_end_cb: restart at rip */
    if (r && r == cur_rt) {
      tos_try_top = NULL;
      ((void (*)(void))t->m_rip)();
      h_Exit();
    } else if (r) {
      if (r->fiber) DeleteFiber(r->fiber);
      r->fiber = NULL;
      r->entry = (void (*)(uint8_t *))t->m_rip;
      r->data = NULL;
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
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
    SymInitialize(pr, NULL, TRUE);
  }
  si->SizeOfStruct = sizeof(SYMBOL_INFO);
  si->MaxNameLen = 255;
  ln.SizeOfStruct = sizeof ln;
  fprintf(stderr, "  %s", SymFromAddr(pr, pc, &disp, si) ? si->Name : "?");
  if (SymGetLineFromAddr64(pr, pc, &d32, &ln)) fprintf(stderr, "  %s:%lu", ln.FileName, ln.LineNumber);
  fputc('\n', stderr);
}

void rt_print_stack(void **pcs, int n)
{
  int i;
  for (i = 0; i < n; i++) print_frame((DWORD64)pcs[i]);
  fflush(stderr);
}

static LONG WINAPI crash(EXCEPTION_POINTERS *ep)
{ /* print a symbolized stack, then exit: the translated code has no other debugger */
  CONTEXT c = *ep->ContextRecord;
  DWORD64 base;
  int i;
  fprintf(stderr, "TempleOS runtime: exception %08lX at %p in task '%s'\n", ep->ExceptionRecord->ExceptionCode,
          ep->ExceptionRecord->ExceptionAddress, tos_fs ? (char *)tos_fs->m_task_name : "?");
  if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2)
    fprintf(stderr, "  %s address %p\n", ep->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading",
            (void *)ep->ExceptionRecord->ExceptionInformation[1]);
  for (i = 0; i < 32 && c.Rip; i++) {
    PRUNTIME_FUNCTION f;
    print_frame(c.Rip);
    f = RtlLookupFunctionEntry(c.Rip, &base, NULL);
    if (!f) { c.Rip = *(DWORD64 *)c.Rsp; c.Rsp += 8; }
    else { void *hd; DWORD64 ef; RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, c.Rip, f, &c, &hd, &ef, NULL); }
  }
  fflush(stderr);
  ExitProcess(4);
  return EXCEPTION_EXECUTE_HANDLER;
}
static LONG WINAPI heap_crash(EXCEPTION_POINTERS *ep)
{ /* the C heap reports corruption straight to the debugger, past SetUnhandledExceptionFilter */
  return ep->ExceptionRecord->ExceptionCode == 0xC0000374 ? crash(ep) : EXCEPTION_CONTINUE_SEARCH;
}

int main(void)
{
  c_CSysFixedArea *fa;
  c_CTask *adam;
  c_CCPU *cpu;
  uint8_t *vga;
  TaskRt *r;

  SetUnhandledExceptionFilter(crash);
  AddVectoredExceptionHandler(1, heap_crash);
  timeBeginPeriod(1);
  rt_snd_init();
  QueryPerformanceFrequency(&qpf);
  QueryPerformanceCounter(&qpc0);

  fa = calloc(1, sizeof *fa);   /* SYS_FIXED_AREA (prelude.HC points it here) */
  h_tos_fixed_area = (int64_t)fa;
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
  r->fiber = ConvertThreadToFiberEx(NULL, FIBER_FLAG_FLOAT_SWITCH);
  rts = cur_rt = r;

  make_window();
  __init_lib();    /* kernel + Adam top-level code, TosBoot, TosStart (spawns the game task) */
  ready = 1;
  h_SrvTaskCont(); /* Adam's server loop; never returns */
  return 0;
}
