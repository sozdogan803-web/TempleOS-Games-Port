/* macOS host (Cocoa + Core Audio), for macOS 10.6 and later on Intel, 11 and later on Apple Silicon.
   Manual retain/release (no ARC: before 10.11 it needs libarclite, which Xcode no longer ships);
   APIs newer than 10.6 are checked with respondsToSelector.: process start-up, the window, keyboard/mouse, the clock,
   the PC speaker, and TempleOS's task switcher on ucontext (one context per CTask).
   The scheduling logic mirrors rt_win32.c; only the context and window calls differ. */
#define _XOPEN_SOURCE 600   /* ucontext */
#define _DARWIN_C_SOURCE
#include <ucontext.h>
#include <signal.h>
#include <execinfo.h>
#include <sys/mman.h>
#include <mach/mach_time.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#include <AudioToolbox/AudioToolbox.h>
#include "rt.h"

void __tos_game_main(void);
int64_t h_tos_fixed_area;

/* ---- clock: GetTSC counts nanoseconds; jiffies are milliseconds ---- */
static mach_timebase_info_data_t tb;
static uint64_t t0;
static int64_t ns_now(void) { return (int64_t)((mach_absolute_time() - t0) * tb.numer / tb.denom); }
int64_t h_GetTSC(void) { return ns_now(); }
int64_t h_SysTimerRead(void) { return (int64_t)((double)ns_now() * (18333 * 65536 / 1000) / 1e9); }
int64_t h_HPET(void) { return 0; }

/* ---- PC speaker: a square wave through an AudioQueue ---- */
#define RATE 44100
#define NSMP 512   /* ~12ms per buffer */
#define AMP 2500
static volatile int32_t spk_hz;   /* 0 = silent */

void rt_spk(int64_t hz)
{
  static int trace = -1;   /* testing aid: TOS_SND_TRACE=1 logs each tone change to stderr */
  if (trace < 0) trace = getenv("TOS_SND_TRACE") != NULL;
  if (trace) fprintf(stderr, "spk %lld\n", (long long)hz);
  spk_hz = (int32_t)(hz < 0 ? 0 : hz > 20000 ? 20000 : hz);
}

static void snd_fill(void *user, AudioQueueRef q, AudioQueueBufferRef b)
{
  static double phase;
  int16_t *s = b->mAudioData;
  int32_t hz = spk_hz;
  (void)user;
  for (int j = 0; j < NSMP; j++) {
    if (hz) {
      s[j] = phase < 0.5 ? AMP : -AMP;
      phase += (double)hz / RATE;
      phase -= (int)phase;
    } else
      s[j] = 0;
  }
  b->mAudioDataByteSize = NSMP * 2;
  AudioQueueEnqueueBuffer(q, b, 0, NULL);
}

void rt_snd_init(void)
{
  AudioStreamBasicDescription f = {0};
  AudioQueueRef q;
  f.mSampleRate = RATE;
  f.mFormatID = kAudioFormatLinearPCM;
  f.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
  f.mBytesPerPacket = f.mBytesPerFrame = 2;
  f.mFramesPerPacket = f.mChannelsPerFrame = 1;
  f.mBitsPerChannel = 16;
  if (AudioQueueNewOutput(&f, snd_fill, NULL, NULL, NULL, 0, &q)) return;   /* no audio device: stay silent */
  for (int i = 0; i < 3; i++) {
    AudioQueueBufferRef b;
    if (AudioQueueAllocateBuffer(q, NSMP * 2, &b)) return;
    snd_fill(NULL, q, b);
  }
  AudioQueueStart(q, NULL);
}

/* ---- window ---- */
static NSWindow *win;
static NSView *view;
static int ready, ms_x = 320, ms_y = 240, ms_z, ms_l, ms_r;
static uint32_t rgba[640 * 480];

/* Testing aid: TOS_SHOT=<file.bmp> [TOS_SHOT_MS=<ms>] saves the frame shown after that
   many ms (default 4000) and exits. */
static void put32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static void shot(c_CDC *d)
{
  static int64_t when = -1;
  const char *path = getenv("TOS_SHOT");
  uint8_t h[54 + 1024] = {'B', 'M'};
  uint32_t n = (uint32_t)(d->m_width_internal * d->m_height);
  int f;   /* open/write: fopen here would bind fopen$DARWIN_EXTSN, which old macOS lacks */
  if (!path) return;
  if (when < 0) when = getenv("TOS_SHOT_MS") ? atoll(getenv("TOS_SHOT_MS")) : 4000;
  if (h_cnts.m_jiffies < when || (f = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0) return;
  put32(h + 2, sizeof h + n);
  put32(h + 10, sizeof h);
  put32(h + 14, 40);
  put32(h + 18, (uint32_t)d->m_width_internal);
  put32(h + 22, (uint32_t)-(int32_t)d->m_height);   /* top-down */
  h[26] = 1; h[28] = 8;
  put32(h + 46, 256);
  for (int i = 0; i < 256; i++) {
    uint8_t *c = rt_dac[i & 15];
    h[54 + i * 4] = c[2] << 2 | c[2] >> 4;
    h[55 + i * 4] = c[1] << 2 | c[1] >> 4;
    h[56 + i * 4] = c[0] << 2 | c[0] >> 4;
  }
  write(f, h, sizeof h);
  write(f, d->m_body, n);
  close(f);
  exit(0);
}

static NSRect dest_rect(void)
{ /* the 640x480 screen, as large as the view allows at 4:3, centered (view points, y up) */
  NSRect b = view.bounds;
  CGFloat w, h;
  if (b.size.width * 480 > b.size.height * 640) { h = b.size.height; w = h * 640 / 480; }
  else { w = b.size.width; h = w * 480 / 640; }
  return NSMakeRect((b.size.width - w) / 2, (b.size.height - h) / 2, w, h);
}

void h_GrUpdateVGAGraphics(void)
{
  c_CDC *d = h_gr.m_dc2;
  uint32_t pal[16];
  int w, h;
  if (!ready || !d) return;
  w = (int)(d->m_width < 640 ? d->m_width : 640);
  h = (int)(d->m_height < 480 ? d->m_height : 480);
  for (int i = 0; i < 16; i++) {   /* dc2 holds 4-bit colors; the DAC maps them to RGB */
    uint8_t *c = rt_dac[i];
    pal[i] = 0xFF000000u | (uint32_t)(c[2] << 2 | c[2] >> 4) << 16 | (uint32_t)(c[1] << 2 | c[1] >> 4) << 8 | (c[0] << 2 | c[0] >> 4);
  }
  for (int y = 0; y < h; y++) {
    uint8_t *s = d->m_body + y * d->m_width_internal;
    for (int x = 0; x < w; x++) rgba[y * w + x] = pal[s[x] & 15];
  }
  @autoreleasepool {
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef cg = CGBitmapContextCreate(rgba, w, h, 8, w * 4, cs, kCGImageAlphaNoneSkipLast | kCGBitmapByteOrder32Big);   /* bytes R,G,B,pad as pal[] packs them */
    CGImageRef img = CGBitmapContextCreateImage(cg);
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    view.layer.contents = (__bridge id)img;
    [CATransaction commit];
    [CATransaction flush];
    CGImageRelease(img);
    CGContextRelease(cg);
    CGColorSpaceRelease(cs);
  }
  shot(d);
}

@interface TosView : NSView
@end
@implementation TosView
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent *)e { return YES; }
- (void)updateTrackingAreas
{
  for (NSTrackingArea *a in [[self.trackingAreas copy] autorelease]) [self removeTrackingArea:a];
  [self addTrackingArea:[[[NSTrackingArea alloc] initWithRect:NSZeroRect
                                                      options:NSTrackingCursorUpdate | NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect
                                                        owner:self userInfo:nil] autorelease]];
  [super updateTrackingAreas];
}
- (void)cursorUpdate:(NSEvent *)e
{ /* TempleOS draws its own pointer */
  static NSCursor *blank;
  if (!blank) blank = [[NSCursor alloc] initWithImage:[[[NSImage alloc] initWithSize:NSMakeSize(1, 1)] autorelease] hotSpot:NSZeroPoint];
  [blank set];
}
@end

@interface TosWinDelegate : NSObject <NSWindowDelegate>
@end
@implementation TosWinDelegate
- (BOOL)windowShouldClose:(NSWindow *)w { exit(0); }
@end

static void make_window(void)
{ @autoreleasepool {
  NSRect wa = [NSScreen mainScreen].visibleFrame, r;
  int s = (int)(wa.size.width * 9 / 10 / 640);
  NSString *title = [NSProcessInfo processInfo].processName;
  static TosWinDelegate *del;
  if ((int)(wa.size.height * 9 / 10 / 480) < s) s = (int)(wa.size.height * 9 / 10 / 480);
  if (s < 1) s = 1;
  [NSApplication sharedApplication];
  [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
  {
    NSMenu *bar = [[NSMenu new] autorelease], *app = [[NSMenu new] autorelease];
    NSMenuItem *it = [[NSMenuItem new] autorelease];
    [app addItemWithTitle:[@"Quit " stringByAppendingString:title] action:@selector(terminate:) keyEquivalent:@"q"];
    it.submenu = app;
    [bar addItem:it];
    NSApp.mainMenu = bar;
  }
  [NSApp finishLaunching];
  r = NSMakeRect(0, 0, 640 * s, 480 * s);
  win = [[NSWindow alloc] initWithContentRect:r
                                    styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                                      backing:NSBackingStoreBuffered defer:NO];
  win.title = title;
  win.releasedWhenClosed = NO;
  win.acceptsMouseMovedEvents = YES;
  win.contentMinSize = NSMakeSize(320, 240);
  del = [TosWinDelegate new];
  win.delegate = del;
  view = [[TosView alloc] initWithFrame:r];
  view.wantsLayer = YES;
  view.layer.backgroundColor = CGColorGetConstantColor(kCGColorBlack);   /* letterbox bars */
  view.layer.contentsGravity = kCAGravityResizeAspect;
  view.layer.magnificationFilter = kCAFilterNearest;
  win.contentView = view;
  [win center];
  [win makeKeyAndOrderFront:nil];
  [win makeFirstResponder:view];
  if (!getenv("TOS_SHOT")) [NSApp activateIgnoringOtherApps:YES];
} }

/* Mac virtual key code -> PC scan code set 1 (0x100 = E0-prefixed; 0 = none) */
static const uint16_t sc_of[128] = {
  [0] = 0x1E, [1] = 0x1F, [2] = 0x20, [3] = 0x21, [4] = 0x23, [5] = 0x22, [6] = 0x2C, [7] = 0x2D, [8] = 0x2E,
  [9] = 0x2F, [10] = 0x56, [11] = 0x30, [12] = 0x10, [13] = 0x11, [14] = 0x12, [15] = 0x13, [16] = 0x15, [17] = 0x14,
  [18] = 0x02, [19] = 0x03, [20] = 0x04, [21] = 0x05, [22] = 0x07, [23] = 0x06, [24] = 0x0D, [25] = 0x0A, [26] = 0x08,
  [27] = 0x0C, [28] = 0x09, [29] = 0x0B, [30] = 0x1B, [31] = 0x18, [32] = 0x16, [33] = 0x1A, [34] = 0x17, [35] = 0x19,
  [36] = 0x1C, [37] = 0x26, [38] = 0x24, [39] = 0x28, [40] = 0x25, [41] = 0x27, [42] = 0x2B, [43] = 0x33, [44] = 0x35,
  [45] = 0x31, [46] = 0x32, [47] = 0x34, [48] = 0x0F, [49] = 0x39, [50] = 0x29, [51] = 0x0E, [53] = 0x01,
  [56] = 0x2A, [57] = 0x3A, [58] = 0x38, [59] = 0x1D, [60] = 0x36, [61] = 0x138, [62] = 0x11D,
  [65] = 0x53, [67] = 0x37, [69] = 0x4E, [71] = 0x45, [75] = 0x135, [76] = 0x11C, [78] = 0x4A,
  [82] = 0x52, [83] = 0x4F, [84] = 0x50, [85] = 0x51, [86] = 0x4B, [87] = 0x4C, [88] = 0x4D, [89] = 0x47, [91] = 0x48, [92] = 0x49,
  [96] = 0x3F, [97] = 0x40, [98] = 0x41, [99] = 0x3D, [100] = 0x42, [101] = 0x43, [103] = 0x57, [109] = 0x44, [111] = 0x58,
  [114] = 0x152, [115] = 0x147, [116] = 0x149, [117] = 0x153, [118] = 0x3E, [119] = 0x14F, [120] = 0x3C, [121] = 0x151,
  [122] = 0x3B, [123] = 0x14B, [124] = 0x14D, [125] = 0x150, [126] = 0x148,
};

static void key(unsigned code, int up)
{
  uint16_t sc = code < 128 ? sc_of[code] : 0;
  if (!sc || !ready) return;
  if (sc & 0x100) rt_kbd_push(0xE0);
  rt_kbd_push((uint8_t)(sc | (up ? 0x80 : 0)));
}

static void modifiers(NSEvent *e)
{ /* flagsChanged: which modifier went down or up (device-dependent bits tell left from right) */
  static const struct { unsigned code; NSUInteger bit; } m[] = {
    {56, 0x2}, {60, 0x4}, {59, 0x1}, {62, 0x2000}, {58, 0x20}, {61, 0x40}};
  NSUInteger f = e.modifierFlags;
  if (e.keyCode == 57) { key(57, 0); key(57, 1); return; }   /* Caps Lock reports toggles, not presses */
  for (size_t i = 0; i < sizeof m / sizeof *m; i++)
    if (m[i].code == e.keyCode) key(e.keyCode, !(f & m[i].bit));
}

static void mouse(void)
{
  NSPoint p = [view convertPoint:[win mouseLocationOutsideOfEventStream] fromView:nil];
  NSRect r = dest_rect();
  NSUInteger b = [NSEvent pressedMouseButtons];
  int x = ms_x, y = ms_y, l, rt;
  /* like Windows: the pointer follows only inside the window, or while a button drags it */
  if (r.size.width > 0 && r.size.height > 0 && (NSPointInRect(p, view.bounds) || ms_l || ms_r)) {
    x = (int)((p.x - r.origin.x) * 640 / r.size.width);
    y = (int)((r.origin.y + r.size.height - p.y) * 480 / r.size.height);
    x = x < 0 ? 0 : x > 639 ? 639 : x;
    y = y < 0 ? 0 : y > 479 ? 479 : y;
  }
  l = win.isKeyWindow && (b & 1);
  rt = win.isKeyWindow && (b & 2);
  if (x == ms_x && y == ms_y && l == ms_l && rt == ms_r) return;
  ms_x = x; ms_y = y; ms_l = l; ms_r = rt;
  if (ready) h_MsHardSet(ms_x, ms_y, ms_z, ms_l, ms_r);
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
    at = strtoll(p, &end, 10);
    if (*end != ':' || at > h_cnts.m_jiffies) return;
    rt_kbd_push((uint8_t)strtoul(end + 1, &end, 16));
    p = *end == ',' ? end + 1 : NULL;
  }
}

static void pump(double wait)
{
  static double wheel;
  @autoreleasepool {
    NSEvent *e;
    NSDate *until = wait > 0 ? [NSDate dateWithTimeIntervalSinceNow:wait] : nil;
    while ((e = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:until inMode:NSDefaultRunLoopMode dequeue:YES])) {
      until = nil;
      switch (e.type) {
      case NSEventTypeKeyDown:
        if (e.modifierFlags & NSEventModifierFlagCommand) { [NSApp sendEvent:e]; continue; }   /* Cmd-Q etc. */
        key(e.keyCode, 0);
        continue;   /* not to AppKit: no beep */
      case NSEventTypeKeyUp:
        key(e.keyCode, 1);
        continue;
      case NSEventTypeFlagsChanged:
        modifiers(e);
        continue;
      case NSEventTypeScrollWheel:
        if ([e respondsToSelector:@selector(scrollingDeltaY)])   /* 10.7+: trackpads report pixels */
          wheel += e.hasPreciseScrollingDeltas ? e.scrollingDeltaY / 10 : e.scrollingDeltaY;
        else
          wheel += e.deltaY;
        while (wheel >= 1) { wheel--; ms_z--; }
        while (wheel <= -1) { wheel++; ms_z++; }
        if (ready) h_MsHardSet(ms_x, ms_y, ms_z, ms_l, ms_r);
        break;
      default: break;
      }
      [NSApp sendEvent:e];
    }
  }
  mouse();
  scripted_keys();
  while (rt_kbd_pending()) h_KbdPktRead();   /* the keyboard IRQ's job: one call per byte at port 0x60 */
}

static int64_t last_pump;
void rt_tick(void)
{
  int64_t t = ns_now();
  h_cnts.m_jiffies = t / 1000000;
  if (t - last_pump >= 2000000) { last_pump = t; pump(0); }
}

void rt_idle(void)
{
  pump(0.001);   /* wait up to 1ms for input */
  last_pump = ns_now();
  rt_tick();
}
void h_SysHlt(void) { rt_idle(); }

/* ---- tasks on ucontext (Sched.HC's _YIELD and _TASK_END_NOW) ---- */
#define STACK_SIZE (8 << 20)
#define GUARD 16384
typedef struct TaskRt {
  c_CTask *task;
  ucontext_t ctx;
  uint8_t *stack;   /* NULL for Adam, which runs on the main thread's stack */
  int live;         /* ctx holds a context to resume */
  TosTry *try_top;
  void (*entry)(int64_t);   /* Spawn's function and data (an 8-byte slot, see FIXES in tools/hc_lex.py) */
  int64_t data;
  struct TaskRt *next;
} TaskRt;
static TaskRt *rts, *cur_rt;
static uint8_t *grave;      /* stack of a task that ended itself; freed once off it */
static ucontext_t dead_ctx; /* where a switch away from an ended task saves its (unused) state */
static int in_end;          /* inside TaskEnd, which runs from the scheduler */

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

static void free_stack(uint8_t *s) { if (s) munmap(s, STACK_SIZE); }
static void bury(void) { free_stack(grave); grave = NULL; }

static void task_main(void)
{
  TaskRt *r = cur_rt;
  bury();
  r->entry(r->data);
  h_Exit();
}

static void switch_to(c_CTask *t)
{
  TaskRt *r = rt_of(t, 1), *old = cur_rt;
  if (r == old) return;
  if (old) old->try_top = tos_try_top;
  cur_rt = r;
  tos_fs = t;
  tos_try_top = r->try_top;
  if (!r->live) {
    if (!r->stack) {
      r->stack = mmap(NULL, STACK_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
      if (r->stack == MAP_FAILED) rt_fatal("no memory for a task stack");
      mprotect(r->stack, GUARD, PROT_NONE);   /* guard page: an overflow faults */
    }
    getcontext(&r->ctx);
    r->ctx.uc_stack.ss_sp = r->stack + GUARD;   /* below ss_sp: Darwin's makecontext writes at ss_sp itself */
    r->ctx.uc_stack.ss_size = STACK_SIZE - GUARD - 256;   /* zeroed top: backtrace() finds a NULL frame there instead of walking off the end */
    r->ctx.uc_link = NULL;
    makecontext(&r->ctx, task_main, 0);
    r->live = 1;
  }
  if (old) old->live = 1;
  swapcontext(old ? &old->ctx : &dead_ctx, &r->ctx);
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
      if (r == cur_rt) { grave = r->stack; cur_rt = NULL; }
      else free_stack(r->stack);
      free(r);
    }
  } else if (next == t && !(t->m_task_flags >> TASKf_KILL_TASK & 1)) {   /* task_end_cb: restart at rip */
    if (r && r == cur_rt) {
      tos_try_top = NULL;
      ((void (*)(void))t->m_rip)();
      h_Exit();
    } else if (r) {
      r->live = 0;   /* its stack is reused from the top */
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
  exit(by_except ? 5 : 0);
}

void rt_print_stack(void **pcs, int n)
{ /* names only; for lines: atos -o <app>/Contents/MacOS/<Game> <addresses>, or run under lldb */
  backtrace_symbols_fd(pcs, n, 2);
}

static void on_crash(int sig, siginfo_t *si, void *uc_)
{ /* print the stack, then exit: the translated code has no other debugger */
  ucontext_t *uc = uc_;
  void *pcs[32];
  int n;
#if defined(__aarch64__)
  void *pc = (void *)uc->uc_mcontext->__ss.__pc;
#else
  void *pc = (void *)uc->uc_mcontext->__ss.__rip;
#endif
  fprintf(stderr, "TempleOS runtime: signal %d (%s) at %p in task '%s'\n", sig, strsignal(sig), pc,
          tos_fs ? (char *)tos_fs->m_task_name : "?");
  if (sig == SIGSEGV || sig == SIGBUS) fprintf(stderr, "  address %p\n", si->si_addr);
  n = backtrace(pcs, 32);
  backtrace_symbols_fd(pcs, n, 2);
  _exit(sig == SIGABRT ? 6 : 4);   /* SIGABRT: malloc found heap corruption */
}

static void boot(void)
{
  c_CSysFixedArea *fa;
  c_CTask *adam;
  c_CCPU *cpu;
  uint8_t *vga;
  TaskRt *r;
  struct sigaction sa = {0};
  stack_t ss = {0};

  ss.ss_sp = malloc(1 << 16);   /* on_crash() must run even after a stack overflow */
  ss.ss_size = 1 << 16;
  sigaltstack(&ss, NULL);
  sa.sa_sigaction = on_crash;
  sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
  sigaction(SIGSEGV, &sa, NULL);
  sigaction(SIGBUS, &sa, NULL);
  sigaction(SIGILL, &sa, NULL);
  sigaction(SIGFPE, &sa, NULL);
  sigaction(SIGABRT, &sa, NULL);
  mach_timebase_info(&tb);
  t0 = mach_absolute_time();
  rt_snd_init();

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
  h_cnts.m_time_stamp_freq = h_cnts.m_time_stamp_freq_initial = 1000000000;
  h_cnts.m_time_stamp_kHz_freq = 1000000;
  h_cnts.m_time_stamp_calibrated = 1;
  h_TaskInit(adam, 0);

  r = calloc(1, sizeof *r);
  r->task = adam;
  r->live = 1;   /* Adam runs on main()'s stack: its context is saved on the first switch away */
  rts = cur_rt = r;

  make_window();
  __init_lib();    /* kernel + Adam top-level code, TosBoot, TosStart (spawns the game task) */
  ready = 1;
  h_SrvTaskCont(); /* Adam's server loop; never returns */
}

int main(void)
{ /* Adam gets a 16 MB stack (as Windows links /STACK:16777216).  Not -Wl,-stack_size: with the
     LC_UNIXTHREAD start-up that a 10.6 target needs, Rosetta kills the process. */
  static ucontext_t from, to;
  uint8_t *s = mmap(NULL, 16 << 20, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
  if (s == MAP_FAILED) rt_fatal("no memory for the main stack");
  mprotect(s, GUARD, PROT_NONE);
  getcontext(&to);
  to.uc_stack.ss_sp = s + GUARD;
  to.uc_stack.ss_size = (16 << 20) - GUARD - 256;
  to.uc_link = NULL;
  makecontext(&to, boot, 0);
  swapcontext(&from, &to);
  return 0;
}
