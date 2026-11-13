/* PC speaker (PIT channel 2 + port 0x61) as a square wave through waveOut. */
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdlib.h>
#include "rt.h"

#define RATE 44100
#define NBUF 4
#define NSMP 512          /* ~12ms per buffer: ~45ms of latency */
#define AMP 2500

static volatile LONG spk_hz;   /* 0 = silent */

void rt_spk(int64_t hz)
{
  static int trace = -1;   /* testing aid: TOS_SND_TRACE=1 logs each tone change to stderr */
  if (trace < 0) trace = getenv("TOS_SND_TRACE") != NULL;
  if (trace) fprintf(stderr, "spk %lld\n", (long long)hz);
  InterlockedExchange(&spk_hz, (LONG)(hz < 0 ? 0 : hz > 20000 ? 20000 : hz));
}

static DWORD WINAPI snd_thread(LPVOID arg)
{
  HANDLE ev = CreateEventW(NULL, FALSE, FALSE, NULL);
  WAVEFORMATEX wf = {WAVE_FORMAT_PCM, 1, RATE, RATE * 2, 2, 16, 0};
  HWAVEOUT wo;
  static int16_t buf[NBUF][NSMP];
  WAVEHDR hdr[NBUF] = {0};
  double phase = 0;
  (void)arg;
  if (waveOutOpen(&wo, WAVE_MAPPER, &wf, (DWORD_PTR)ev, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR)
    return 0;   /* no audio device: stay silent */
  for (int i = 0; i < NBUF; i++) {
    hdr[i].lpData = (LPSTR)buf[i];
    hdr[i].dwBufferLength = sizeof buf[i];
    waveOutPrepareHeader(wo, &hdr[i], sizeof hdr[i]);
    hdr[i].dwFlags |= WHDR_DONE;
  }
  for (;;) {
    for (int i = 0; i < NBUF; i++) {
      if (!(hdr[i].dwFlags & WHDR_DONE)) continue;
      LONG hz = spk_hz;
      for (int j = 0; j < NSMP; j++) {
        if (hz) {
          buf[i][j] = phase < 0.5 ? AMP : -AMP;
          phase += (double)hz / RATE;
          phase -= (int)phase;
        } else
          buf[i][j] = 0;
      }
      hdr[i].dwFlags &= ~WHDR_DONE;
      waveOutWrite(wo, &hdr[i], sizeof hdr[i]);
    }
    WaitForSingleObject(ev, 50);
  }
}

void rt_snd_init(void)
{
  CloseHandle(CreateThread(NULL, 0, snd_thread, NULL, 0, NULL));
}
