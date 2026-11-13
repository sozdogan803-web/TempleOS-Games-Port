"""HolyC lexer: DolDoc pre-pass, tokenizer and preprocessor (#define/#include/#if*)."""
import os, re, struct

class Tok:
    __slots__ = ('k', 'v', 'file', 'line', 'nl')
    def __init__(s, k, v, file, line, nl=False):
        s.k, s.v, s.file, s.line, s.nl = k, v, file, line, nl
    def __repr__(s):
        return '%s:%r' % (s.k, s.v)

class LexError(Exception):
    pass

def split_doc(raw):
    """Return (text, bins{num: bytes}) from a TempleOS DolDoc file."""
    i = raw.find(b'\0')
    if i < 0:
        return raw.decode('latin1'), {}
    text, p, bins = raw[:i].decode('latin1'), i + 1, {}
    while p + 16 <= len(raw):
        num, fl, sz, uc = struct.unpack_from('<4I', raw, p)
        if sz > len(raw) - p - 16:
            break
        bins[num] = raw[p + 16:p + 16 + sz]
        p += 16 + sz
    return text, bins

def doldoc_to_code(text, bins, binref):
    """Strip DolDoc commands the way LexGetChar does.
    Outside literals: $IB$ -> bin reference, $IS$ -> size, $TX$ -> its text, others dropped.
    Inside literals: the command text is kept verbatim (Print renders it later)."""
    out, i, n = [], 0, len(text)
    state = 'code'   # code | line | block | str | chr
    while i < n:
        c = text[i]
        if c == '\x05':
            i += 1; continue
        if c == '\x1f':
            c = ' '
        if c == '$':
            if i + 1 < n and text[i + 1] == '$':
                out.append('$'); i += 2; continue
            j, q = i + 1, False
            while j < n and (text[j] != '$' or q):
                if text[j] == '\\' and q:
                    j += 1
                elif text[j] == '"':
                    q = not q
                elif text[j] == '\n' and not q:
                    break
                j += 1
            if j >= n or text[j] != '$':
                raise LexError('unterminated DolDoc command near %r' % text[i:i + 40])
            cmd = text[i + 1:j]
            i = j + 1
            if state in ('str', 'chr'):
                # raw text in the literal: its quotes are characters, not delimiters
                out.append('$' + cmd.replace('\\', '\\\\').replace('"', '\\"') + '$'); continue
            if state != 'code':
                continue
            tag = cmd.split(',', 1)[0].strip()
            if tag in ('IB', 'IS'):
                m = re.search(r'BI=(\d+)', cmd)
                num = int(m.group(1))
                if num not in bins:
                    raise LexError('missing bin %d' % num)
                out.append(' %s ' % (binref(num) if tag == 'IB' else str(len(bins[num]))))
            elif tag == 'TX':
                m = re.match(r'TX[^"]*"((?:\\.|[^"\\])*)"', cmd)
                if m:
                    out.append(m.group(1).replace('\\"', '"').replace('\\\\', '\\'))
            continue
        out.append(c)
        if state == 'code':
            if c == '/' and i + 1 < n and text[i + 1] == '/':
                state = 'line'; out.append('/'); i += 2; continue
            if c == '/' and i + 1 < n and text[i + 1] == '*':
                state = 'block'; out.append('*'); i += 2; continue
            if c == '"':
                state = 'str'
            elif c == "'":
                state = 'chr'
        elif state == 'line':
            if c == '\n':
                state = 'code'
        elif state == 'block':
            if c == '*' and i + 1 < n and text[i + 1] == '/':
                out.append('/'); i += 2; state = 'code'; continue
        elif state in ('str', 'chr'):
            if c == '\\' and i + 1 < n:
                nc = text[i + 1]
                if nc == '$' and i + 2 < n and text[i + 2] == '$':
                    out.append('$'); i += 3; continue
                out.append(nc); i += 2; continue
            if (c == '"' and state == 'str') or (c == "'" and state == 'chr'):
                state = 'code'
        i += 1
    return ''.join(out)

OPS = ['<<=', '>>=', '...', '->', '++', '--', '<<', '>>', '<=', '>=', '==', '!=', '&&', '||', '^^',
       '+=', '-=', '*=', '/=', '%=', '&=', '|=', '^=', '::',
       '+', '-', '*', '/', '%', '&', '|', '^', '~', '!', '<', '>', '=', '?', ':', ';', ',', '.',
       '(', ')', '[', ']', '{', '}', '`', '#', '@']
ESC = {'0': 0, "'": 39, '`': 96, '"': 34, '\\': 92, 'd': 36, 'n': 10, 'r': 13, 't': 9}

def unescape(s, i, quote):
    """Parse literal body starting at s[i] up to closing quote. Returns (bytes, next_index)."""
    out = bytearray()
    while True:
        if i >= len(s):
            raise LexError('unterminated literal')
        c = s[i]
        if c == quote:
            return bytes(out), i + 1
        if c == '\\':
            e = s[i + 1]
            i += 2
            if e in 'xX':
                m = re.match(r'[0-9A-Fa-f]{1,2}', s[i:])
                out.append(int(m.group(0), 16) if m else 0)
                i += len(m.group(0)) if m else 0
            elif e in ESC:
                out.append(ESC[e])
            else:
                out += e.encode('latin1')
            continue
        if c == '\x1f':
            c = ' '
        out += c.encode('latin1')
        i += 1

RX_NUM = re.compile(r'0[xX][0-9A-Fa-f]+|0[bB][01]+|(\d+\.(?!\.)\d*|\.\d+|\d+)([eE][+-]?\d+)?')
RX_ID = re.compile(r'[A-Za-z_\x80-\xff][A-Za-z0-9_\x80-\xff]*')

def tokenize(code, fname):
    toks, i, line, n, nl = [], 0, 1, len(code), True
    while i < n:
        c = code[i]
        if c == '\n':
            line += 1; i += 1; nl = True; continue
        if c in ' \t\r\f\x1f\x05\x00' or c == '\x0b':
            i += 1; continue
        if c == '\\' and code[i + 1:i + 2] in ('\n', '\r'):   # line continuation
            i = code.index('\n', i) + 1; line += 1; continue
        if c == '$':   # current offset inside class defs / asm
            toks.append(Tok('id', '$', fname, line, nl)); i += 1; nl = False; continue
        if code.startswith('//', i):
            j = code.find('\n', i)
            i = n if j < 0 else j
            continue
        if code.startswith('/*', i):
            depth, j = 1, i + 2
            while depth and j < n:   # HolyC block comments nest
                if code.startswith('/*', j):
                    depth += 1; j += 2
                elif code.startswith('*/', j):
                    depth -= 1; j += 2
                else:
                    j += 1
            k = code.count('\n', i, j)
            line += k
            nl = nl or k > 0
            i = j
            continue
        m = RX_NUM.match(code, i)
        if m and (c.isdigit() or (c == '.' and i + 1 < n and code[i + 1].isdigit())):
            t = m.group(0)
            if t[:2] in ('0x', '0X'):
                toks.append(Tok('int', int(t, 16), fname, line, nl))
            elif t[:2] in ('0b', '0B'):
                toks.append(Tok('int', int(t[2:], 2), fname, line, nl))
            elif re.fullmatch(r'\d+', t):
                toks.append(Tok('int', int(t), fname, line, nl))
            else:
                toks.append(Tok('float', float(t), fname, line, nl))
            i = m.end(); nl = False; continue
        m = RX_ID.match(code, i)
        if m:
            toks.append(Tok('id', m.group(0), fname, line, nl))
            i = m.end(); nl = False; continue
        if c == '"':
            b, j = unescape(code, i + 1, '"')
            line += code.count('\n', i, j)
            toks.append(Tok('str', b, fname, line, nl))
            i = j; nl = False; continue
        if c == "'":
            b, j = unescape(code, i + 1, "'")
            line += code.count('\n', i, j)
            v = 0
            for k, ch in enumerate(b[:8]):
                v |= ch << (8 * k)
            toks.append(Tok('char', (v, len(b)), fname, line, nl))
            i = j; nl = False; continue
        for op in OPS:
            if code.startswith(op, i):
                toks.append(Tok('op', op, fname, line, nl))
                i += len(op); nl = False
                break
        else:
            if c == '\xe3':   # pi glyph in TempleOS charset
                toks.append(Tok('id', 'pi', fname, line, nl)); i += 1; nl = False; continue
            raise LexError('%s:%d bad char %r' % (fname, line, c))
    return toks

# Game bugs TempleOS hides (it maps memory at address 0, so a read through NULL returns junk
# instead of crashing): file -> [(old, new)], each `old` occurring exactly once in that file.
FIXES = {
    'Apps/KeepAway/KeepAway.HC': [   # last_owner with no team-mate picked yet compared against NULL
        ('o->get_ball_dt<nearest_ball[o->team]->get_ball_dt)',
         'nearest_ball[o->team] && o->get_ball_dt<nearest_ball[o->team]->get_ball_dt)')],
    'Apps/X-Caliber/X-Caliber.HC': [   # ShotNew falls off the end; TempleOS left tmps in RAX
        ('QueIns(tmps,shot_head.last);\n}', 'QueIns(tmps,shot_head.last);\n  return tmps;\n}')],
    # Not bugs: Spawn's data is an 8-byte stack slot, and Strut and CastleFrankenstein pass an F64
    # through it (Spawn(&Zoom,2.0(I64))). An I64 keeps all 8 bytes where pointers are 4 (32-bit).
    'Kernel/KernelC.HH': [
        ('CTask *Spawn(U0 (*fp_addr)(U8 *data),U8 *data=NULL,', 'CTask *Spawn(U0 (*fp_addr)(I64 data),I64 data=NULL,')],
    'Kernel/KTask.HC': [
        ('CTask *Spawn(U0 (*fp_start_addr)(U8 *data),U8 *data=NULL,',
         'CTask *Spawn(U0 (*fp_start_addr)(I64 data),I64 data=NULL,')],
}
# Corrupt bytes in TinkerOS's embedded sprites: (file, bin) -> [(offset, old, new)].
BIN_FIXES = {
    ('Apps/Titanium/Titanium.HC', 9): [(4041, 0x6e, 0)],   # sprite END byte; bins 7 and 8 have 0 here
}

class Source:
    """Loads a TempleOS file: code text from `code_root`, bins from `bin_root` (may be same)."""
    def __init__(s, code_root, bin_root):
        s.code_root, s.bin_root = code_root, bin_root
        s.bins = {}        # (relpath, num) -> bytes
        s.binsym = {}      # (relpath, num) -> C symbol

    def resolve(s, name, cur):
        n = name.replace('\\', '/')
        if n.startswith('::/'):
            n = n[3:]
        elif n.startswith('/'):
            n = n[1:]
        elif n.startswith('~/'):
            n = 'Home/' + n[2:]
        else:
            n = os.path.normpath(os.path.join(os.path.dirname(cur), n)).replace('\\', '/')
        cands = [n] if os.path.splitext(n)[1] else [n + '.HC', n + '.HH', n + '.HC.Z', n]
        for c in cands:
            if os.path.exists(os.path.join(s.code_root, c)):
                return c
        for c in cands:   # port glue files (TosBoot.HC, ...) live next to this script
            p = os.path.join(os.path.dirname(os.path.abspath(__file__)), c)
            if os.path.exists(p):
                return p
        raise LexError('cannot find include %r from %s' % (name, cur))

    def load(s, rel):
        raw = open(os.path.join(s.code_root, rel), 'rb').read()
        text, bins = split_doc(raw)
        for old, new in FIXES.get(rel, ()):
            if text.count(old) != 1:
                raise LexError('%s: fix does not match exactly once: %r' % (rel, old))
            text = text.replace(old, new)
        bp = os.path.join(s.bin_root, rel)
        if os.path.exists(bp):
            _, bins = split_doc(open(bp, 'rb').read())
        for (frel, num), fixes in BIN_FIXES.items():
            if frel == rel:
                b = bytearray(bins[num])
                for off, old, new in fixes:
                    if b[off] != old:
                        raise LexError('%s bin %d: fix expects %#x at %d' % (rel, num, old, off))
                    b[off] = new
                bins[num] = bytes(b)
        tag = re.sub(r'\W', '_', os.path.splitext(rel)[0])
        def binref(num):
            key = (rel, num)
            s.bins[key] = bins[num]
            s.binsym[key] = '__bin_%s_%d' % (tag, num)
            return s.binsym[key]
        return tokenize(doldoc_to_code(text, bins, binref), rel)

BUILTIN_MACROS = {'__DATE__', '__TIME__', '__LINE__', '__CMD_LINE__', '__FILE__', '__DIR__'}

class Preproc:
    """Token stream with #define expansion, #include and conditional compilation."""
    def __init__(s, src, defines=None):
        s.src = src
        s.defines = defines if defines is not None else {}
        s.stack = []          # list of [toks, pos]
        s.cond = []           # list of (active, taken)
        s.included = set()
        s.pending = []
        s.exe_blocks = []
        s.pinned = set()      # names whose first #define (from the prelude) later #defines can't replace

    def push_file(s, rel):
        s.stack.append([s.src.load(rel), 0, rel])

    def push_text(s, text, name):
        s.stack.append([tokenize(text, name), 0, name])

    def active(s):
        return all(a for a, _ in s.cond)

    def _raw(s):
        while s.stack:
            toks, p, rel = s.stack[-1]
            if p < len(toks):
                s.stack[-1][1] += 1
                return toks[p]
            s.stack.pop()
        return None

    def _peek_raw(s):
        if s.stack:
            toks, p, rel = s.stack[-1]
            if p < len(toks):
                return toks[p]
        return None

    def _line_rest(s, t0):
        out = []
        while True:
            t = s._peek_raw()
            if t is None or t.nl or t.file != t0.file:
                return out
            out.append(s._raw())

    def next(s):
        while True:
            if s.pending:
                return s.pending.pop(0)
            t = s._raw()
            if t is None:
                if s.cond:
                    raise LexError('unterminated #if')
                return Tok('eof', None, '', 0)
            if t.k == 'op' and t.v == '#':
                d = s._raw()
                s.directive(t, d)
                continue
            if not s.active():
                continue
            if t.k == 'id' and t.v in s.defines:
                body = s.defines[t.v]
                s.pending = [Tok(b.k, b.v, t.file, t.line) for b in body] + s.pending
                # expand nested defines lazily: re-run through loop
                exp = []
                guard = 0
                while s.pending and guard < 10000:
                    guard += 1
                    b = s.pending.pop(0)
                    if b.k == 'id' and b.v in s.defines and b.v != t.v:
                        s.pending = [Tok(x.k, x.v, t.file, t.line) for x in s.defines[b.v]] + s.pending
                    else:
                        exp.append(b)
                s.pending = exp
                continue
            return t

    def directive(s, h, d):
        name = d.v
        rest = s._line_rest(d)
        if name in ('ifdef', 'ifndef'):
            v = rest[0].v in s.defines
            act = v if name == 'ifdef' else not v
            s.cond.append((act, act))
        elif name in ('ifjit', 'ifaot'):
            act = name == 'ifjit'
            s.cond.append((act, act))
        elif name == 'if':
            s.cond.append((False, False))  # shortcut: #if exprs unused by games, treated false
        elif name == 'else':
            a, taken = s.cond.pop()
            s.cond.append((not taken, True))
        elif name == 'endif':
            s.cond.pop()
        elif not s.active():
            return
        elif name == 'define':
            nm = rest[0].v
            if nm not in BUILTIN_MACROS and not (nm in s.pinned and nm in s.defines):   # BUILTIN_MACROS use #exe; the parser expands them
                s.defines[nm] = rest[1:]
        elif name == 'include':
            rel = s.src.resolve(rest[0].v.decode('latin1'), h.file)
            s.stack.append([s.src.load(rel), 0, rel])
        elif name == 'exe':
            # #exe{...} runs code at compile time; games don't use it. Skip the block.
            toks = rest if rest else [s._raw()]
            depth = sum(1 for x in toks if x.v == '{') - sum(1 for x in toks if x.v == '}')
            while depth > 0:
                x = s._raw()
                depth += (x.v == '{') - (x.v == '}')
        elif name in ('help_index', 'help_file', 'assert', 'pragma', 'undef'):
            if name == 'undef':
                s.defines.pop(rest[0].v, None)
        else:
            raise LexError('%s:%d unknown directive #%s' % (h.file, h.line, name))
