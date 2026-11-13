/* Prints integer and float results that MSVC computes with helper calls on 32-bit x86
   (runtime/rt_libc.c's _allmul, _alldiv, _ftol2, x87 math ...).  x86helpers.ps1 builds it as
   64-bit (the hardware does it) and as the 32-bit no-CRT build, and compares the two outputs. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#if defined(_M_IX86) && defined(TOS_NOCRT)   /* as runtime/tos_rt.h */
#pragma function(sqrt, sin, cos, tan, atan, atan2, exp, log, log10, pow, fmod, floor, ceil)
#endif

static uint64_t st = 0x9E3779B97F4A7C15ull;
static uint64_t rnd(void) { st ^= st << 13; st ^= st >> 7; st ^= st << 17; return st; }
static int64_t val(void)
{ /* mixed magnitudes: tiny, 16, 32, 48 and 64 bits, both signs */
  static const int bits[] = {4, 16, 31, 32, 33, 48, 63, 64};
  uint64_t v = rnd(), m = rnd();
  int b = bits[m % 8];
  if (b < 64) v &= (1ull << b) - 1;
  return (m >> 8 & 1) ? -(int64_t)v : (int64_t)v;
}
static uint64_t fbits(double d) { uint64_t u; memcpy(&u, &d, 8); return u; }

int main(void)
{
  int i;
  for (i = 0; i < 20000; i++) {
    int64_t a = val(), b = val();
    uint64_t ua = (uint64_t)a, ub = (uint64_t)b;
    int c = (int)(rnd() % 64);
    double d = (double)val() / (double)((rnd() % 1000) + 1);
    printf("i %llx %llx %llx %llx %llx\n", (unsigned long long)(a * b), (unsigned long long)(ua << c),
           (unsigned long long)(a >> c), (unsigned long long)(ua >> c), (unsigned long long)(int64_t)d);
    if (b && !(a == INT64_MIN && b == -1))
      printf("d %llx %llx\n", (unsigned long long)(a / b), (unsigned long long)(a % b));
    if (ub) printf("u %llx %llx\n", (unsigned long long)(ua / ub), (unsigned long long)(ua % ub));
    if (d >= 0) printf("c %llx\n", (unsigned long long)(uint64_t)d);
    printf("f %llx %llx\n", (unsigned long long)fbits((double)a), (unsigned long long)fbits((double)ua));
  }
  for (i = 0; i < 4000; i++) {
    double x = ((double)(rnd() >> 11) / 9007199254740992.0 - 0.5) * 40, y = ((double)(rnd() >> 11) / 9007199254740992.0) * 6 + 0.01;
    printf("m %llx %llx %llx %llx %llx %llx %llx %llx %llx %llx %llx %llx %llx %llx %llx %llx\n",
           (unsigned long long)fbits(sin(x)), (unsigned long long)fbits(cos(x)), (unsigned long long)fbits(tan(x / 20)),
           (unsigned long long)fbits(atan(x)), (unsigned long long)fbits(atan2(x, y - 3)), (unsigned long long)fbits(exp(x)),
           (unsigned long long)fbits(log(y)), (unsigned long long)fbits(log10(y)), (unsigned long long)fbits(log2(y)),
           (unsigned long long)fbits(pow(y, x / 4)), (unsigned long long)fbits(sqrt(y)), (unsigned long long)fbits(floor(x)),
           (unsigned long long)fbits(ceil(x)), (unsigned long long)fbits(trunc(x)), (unsigned long long)fbits(nearbyint(x)),
           (unsigned long long)fbits(fmod(x, y)));
  }
  return 0;
}
