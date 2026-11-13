/* Shared between the runtime's C files (not seen by generated code). */
#pragma once
#include "tos_gen.h"

#ifndef _WIN32
#include <strings.h>
#define _stricmp strcasecmp
#define strtok_s strtok_r
#endif

#define TASK_SIG        0x536B7354   /* 'TskS' */
#define TASKf_KILL_TASK     1
#define TASKf_SUSPENDED     2
#define TASKf_AWAITING_MSG  9

extern c_CTask *tos_fs;
extern c_CCPU *tos_gs;
extern TosTry *tos_try_top;          /* the running task's try stack */

void rt_fatal(const char *fmt, ...);
void rt_print_stack(void **pcs, int n);   /* symbolized, to stderr */
int rt_backtrace(int skip, int n, void **pcs);   /* like RtlCaptureStackBackTrace, which is XP and later */
void rt_report_except(const char *what);  /* Fs->except_ch and where it was thrown, to stderr */

/* 8042 keyboard data port, filled by the host window */
void rt_kbd_push(uint8_t b);
int rt_kbd_pending(void);

/* VGA DAC: 6-bit r,g,b per color */
extern uint8_t rt_dac[256][3];

/* host: PC speaker tone in Hz (0 = silent); the audio thread is started once at boot */
void rt_spk(int64_t hz);
void rt_snd_init(void);

/* host: time and events */
void rt_tick(void);                  /* update cnts.jiffies; pump host events now and then */
void rt_idle(void);                  /* nothing to run: wait ~1ms for time or input */
