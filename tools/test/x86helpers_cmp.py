"""x86helpers_cmp.py <64-bit output> <32-bit output>: integer lines must match exactly,
math results within a relative 1e-12 (x87 FSIN etc. against the CRT's SSE2 routines)."""
import struct, sys

def f(h):
    return struct.unpack('<d', struct.pack('<Q', int(h, 16)))[0]

a = open(sys.argv[1]).read().split('\n')
b = open(sys.argv[2]).read().split('\n')
assert len(a) == len(b) and len(a) > 1000, (len(a), len(b))
bad = worst = 0
names = 'sin cos tan atan atan2 exp log log10 log2 pow sqrt floor ceil trunc nearbyint fmod'.split()
for n, (x, y) in enumerate(zip(a, b)):
    if x == y:
        continue
    if x[:2] != 'm ' or y[:2] != 'm ':
        bad += 1
        if bad <= 10:
            print('line %d differs:\n  64: %s\n  32: %s' % (n + 1, x, y))
        continue
    for name, p, q in zip(names, x.split()[1:], y.split()[1:]):
        p, q = f(p), f(q)
        if p == q or (p != p and q != q):
            continue
        rel = abs(p - q) / max(abs(p), abs(q), 1e-300)
        worst = max(worst, rel)
        if rel > 1e-12:
            bad += 1
            if bad <= 10:
                print('line %d %s: %r vs %r' % (n + 1, name, p, q))
print('%d lines, %d mismatches, worst math difference %.2g' % (len(a), bad, worst))
sys.exit(1 if bad else 0)
