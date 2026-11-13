#!/usr/bin/env python3
"""Mac test runner (port of allrun.ps1 + soak.ps1): run each out/mac/<Game>.app with
TOS_SHOT (screenshot, then exit) and optionally TOS_KEYS (random key presses), in parallel.

  python3 tools/test/soak.py --ms 4000 --no-keys          # launch-and-screenshot pass
  python3 tools/test/soak.py --ms 19000                   # 20 s random play
  python3 tools/test/soak.py --ms 39000 --reverse         # 40 s, games reversed (other key sequences)

Writes <out>/<Game>.png, .err.txt, .out.txt, summary.txt and sheet.png (all shots on one page).
"""
import argparse, os, random, re, struct, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
# make-break pairs: Enter, Space, arrows (E0-prefixed), a few letters, digits
KEYS = [("1C", "9C"), ("39", "B9"), ("E0,48", "E0,C8"), ("E0,50", "E0,D0"), ("E0,4B", "E0,CB"), ("E0,4D", "E0,CD"),
        ("1E", "9E"), ("11", "91"), ("1F", "9F"), ("20", "A0"), ("02", "82"), ("03", "83")]


def games():
    out = []
    for line in open(os.path.join(ROOT, 'tools/test/games.txt')):
        p = line.strip().split('/')
        if p[-1]:
            out.append(p[-2] if p[-1].lower().startswith('run.') else os.path.splitext(p[-1])[0])
    return out


def key_seq(seed, ms):
    r = random.Random(seed)
    out = ["1200:1C", "1300:9C", "2500:1C", "2600:9C"]   # dismiss intro pop-ups
    t = 3200
    while t < ms - 500:
        down, up = r.choice(KEYS)
        out += ["%d:%s" % (t, b) for b in down.split(',')]
        t2 = t + 80 + r.randrange(250)
        out += ["%d:%s" % (t2, b) for b in up.split(',')]
        t += 350 + r.randrange(400)
    return ','.join(out)


def read_bmp(path):
    """8-bit top-down BMP as written by rt_mac.m -> (w, h, rgb rows)"""
    d = open(path, 'rb').read()
    off, = struct.unpack_from('<I', d, 10)
    w, h = struct.unpack_from('<ii', d, 18)
    pal = [d[54 + i * 4: 57 + i * 4][::-1] for i in range(256)]
    return w, abs(h), [b''.join(pal[c] for c in d[off + y * w: off + y * w + w]) for y in range(abs(h))]


def write_sheet(shots, path, cols=7, scale=2):
    """all screenshots at 1/scale on one 24-bit BMP, labelled by order (see summary.txt)"""
    tw, th = 640 // scale, 480 // scale
    rows = (len(shots) + cols - 1) // cols
    W, H = cols * (tw + 4), rows * (th + 4)
    img = [bytearray(b'\x40' * (W * 3)) for _ in range(H)]
    for i, (g, bmp) in enumerate(shots):
        if not bmp:
            continue
        w, h, px = read_bmp(bmp)
        x0, y0 = (i % cols) * (tw + 4) + 2, (i // cols) * (th + 4) + 2
        for y in range(th):
            src = px[min(y * scale, h - 1)]
            row = img[y0 + y]
            for x in range(tw):
                s = min(x * scale, w - 1) * 3
                row[(x0 + x) * 3:(x0 + x) * 3 + 3] = src[s:s + 3][::-1]   # RGB -> BMP's BGR
    pad = (4 - W * 3 % 4) % 4
    with open(path, 'wb') as f:
        f.write(b'BM' + struct.pack('<IHHI', 54 + (W * 3 + pad) * H, 0, 0, 54))
        f.write(struct.pack('<IiiHHIIiiII', 40, W, -H, 1, 24, 0, 0, 2835, 2835, 0, 0))
        for row in img:
            f.write(bytes(row) + b'\0' * pad)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('games', nargs='*')
    ap.add_argument('--ms', type=int, default=19000, help='screenshot (and exit) after this many ms')
    ap.add_argument('--par', type=int, default=6)
    ap.add_argument('--no-keys', action='store_true')
    ap.add_argument('--reverse', action='store_true')
    ap.add_argument('--out', default=None)
    ap.add_argument('--arch', default=None, help='x86_64 runs the Intel half (under Rosetta on Apple Silicon)')
    a = ap.parse_args()
    gs = a.games or games()
    if a.reverse:
        gs = gs[::-1]
    out = a.out or os.path.join(ROOT, 'out/mac', 'shots' if a.no_keys else 'soak')
    os.makedirs(out, exist_ok=True)
    os.chdir(ROOT)
    todo, running, summary = list(enumerate(gs)), [], {}
    while todo or running:
        while todo and len(running) < a.par:
            i, g = todo.pop(0)
            env = dict(os.environ, TOS_SHOT=os.path.join(out, g + '.bmp'), TOS_SHOT_MS=str(a.ms))
            if not a.no_keys:
                env['TOS_KEYS'] = key_seq(i, a.ms)
            for ext in ('.bmp', '.png'):
                if os.path.exists(os.path.join(out, g + ext)):
                    os.remove(os.path.join(out, g + ext))
            exe = ['out/mac/%s.app/Contents/MacOS/%s' % (g, g)]
            p = subprocess.Popen((['arch', '-' + a.arch] if a.arch else []) + exe, env=env,
                                 stdout=open(os.path.join(out, g + '.out.txt'), 'w'),
                                 stderr=open(os.path.join(out, g + '.err.txt'), 'w'))
            running.append((g, p, time.time()))
        time.sleep(0.3)
        for g, p, t in list(running):
            if p.poll() is None and time.time() - t < a.ms / 1000 + 25:
                continue
            if p.poll() is None:
                p.kill(); p.wait(); st = 'hung'
            else:
                st = 'exit=%d' % p.returncode
            if os.path.exists(os.path.join(out, g + '.bmp')):
                st += ' shot'
            err = open(os.path.join(out, g + '.err.txt'), errors='replace').read()
            un = sorted(set(re.findall(r'runtime: (\w+) not implemented', err)))
            first = ' / '.join(l for l in err.splitlines()[:2])
            summary[g] = '%s : %s | unimpl: %s | %s' % (g, st, ','.join(un), first)
            print(summary[g], flush=True)
            running.remove((g, p, t))
    shots = [(g, os.path.join(out, g + '.bmp') if os.path.exists(os.path.join(out, g + '.bmp')) else None) for g in gs]
    write_sheet(shots, os.path.join(out, 'sheet.bmp'))
    subprocess.run(['sips', '-s', 'format', 'png', os.path.join(out, 'sheet.bmp'), '--out', os.path.join(out, 'sheet.png')],
                   stdout=subprocess.DEVNULL)
    for g, bmp in shots:
        if bmp:
            subprocess.run(['sips', '-s', 'format', 'png', bmp, '--out', bmp[:-4] + '.png'], stdout=subprocess.DEVNULL)
            os.remove(bmp)
    os.remove(os.path.join(out, 'sheet.bmp'))
    lines = [summary[g] for g in gs]
    open(os.path.join(out, 'summary.txt'), 'w').write('\n'.join(['sheet order: ' + ' '.join(gs)] + lines) + '\n')
    bad = [l for l in lines if 'exit=0 shot' not in l]
    print('\n%d/%d ran clean (exit 0 + screenshot)' % (len(lines) - len(bad), len(lines)))


if __name__ == '__main__':
    main()
