"""HolyC typed AST -> C.

    python hc_emit.py <code_root> <bin_root> <out_dir> [game.HC ...]

writes out_dir/tos_gen.h + tos_lib.c (the TempleOS library) and one <Game>.c per game.
Functions with no translatable body become aborting stubs that the C runtime overrides
by defining the same h_Name symbol (MSVC /alternatename makes the stub a weak default)."""
import os, sys, math, struct, traceback
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.setrecursionlimit(20000)
from hc_parse import (Ty, N, Sym, INTS, prim, ptr, I64, U64, F64, U0, U8, is_f, int_base, is_i, is_p,
                      sizeof_t, items_size, member_offset, const_val)
import hc_build

CINT = {'I8': 'int8_t', 'U8': 'uint8_t', 'I16': 'int16_t', 'U16': 'uint16_t', 'I32': 'int32_t',
        'U32': 'uint32_t', 'I64': 'int64_t', 'U64': 'uint64_t', 'Bool': 'int8_t', 'F64': 'double', 'U0': 'void'}
CMP = ('<', '>', '<=', '>=', '==', '!=')
BITOPS = ('&', '|', '^', '<<', '>>')

class EmitError(Exception):
    pass

def cid(name):
    return ''.join(c if (c.isalnum() and ord(c) < 128) or c == '_' else '_x%02X' % ord(c) for c in name)

def cname(sym):
    return cid(sym.cname)

def clsname(cd):
    return 'c_' + cid(cd.cname)

def cdecl(t, inner=''):
    k = t.k
    if k == 'prim':
        return (CINT[t.name] + ' ' + inner).rstrip()
    if k == 'cls':
        cd = t.cdef
        return ((CINT[cd.ibase] if cd.ibase else clsname(cd)) + ' ' + inner).rstrip()
    if k == 'ptr':
        if t.base.k in ('arr', 'func'):
            return cdecl(t.base, '(*' + inner + ')')
        return cdecl(t.base, '*' + inner)
    if k == 'arr':
        return cdecl(t.base, inner + '[%d]' % (t.n or 1))
    if k == 'func':
        return cdecl(t.ret, inner + '(' + param_types(t) + ')')
    raise EmitError('type %r' % t)

def ctype(t):
    return cdecl(t)

def param_types(ft):
    ps = [cdecl(p.ty) for p in ft.params]
    if ft.var:
        ps += ['int64_t', 'int64_t*']
    return ', '.join(ps) or 'void'

def kind(t):
    """i int, f F64, p pointer/function, s struct, v void"""
    if t.k == 'prim':
        return 'f' if t.name == 'F64' else 'v' if t.name == 'U0' else 'i'
    if t.k == 'cls':
        return ('f' if t.cdef.ibase == 'F64' else 'i') if t.cdef.ibase else 's'
    return 'p'

def is_struct(t):
    return t.k == 'cls' and not t.cdef.ibase

def elem_size(t):
    """HolyC element size for pointer arithmetic on t (0 for U0/func/empty classes)."""
    b = t.base
    return 0 if b.k == 'func' else sizeof_t(b)

def lit_int(v, t=I64):
    v &= (1 << 64) - 1
    if int_base(t) == 'U64' or (t.k == 'prim' and t.name == 'U64'):
        return '%dULL' % v
    if v >= 1 << 63:
        v -= 1 << 64
    if v == -(1 << 63):
        return '(-9223372036854775807LL-1)'
    return '(-%dLL)' % -v if v < 0 else '%dLL' % v

def lit_flt(v):
    if math.isinf(v) or math.isnan(v):
        return '__i2f(%s)' % lit_int(struct.unpack('<q', struct.pack('<d', v))[0])
    r = repr(float(v))
    return '(%s)' % r if r.startswith('-') else r

def cstr_lit(b):
    out, cur = [], []
    for i, c in enumerate(b):
        if 32 <= c < 127 and c not in (34, 92, 63):
            cur.append(chr(c))
        else:
            cur.append('\\%03o' % c)
        if len(cur) >= 1500:
            out.append('"%s"' % ''.join(cur)); cur = []
    out.append('"%s"' % ''.join(cur))
    return '\n  '.join(out)

def byte_list(b):
    rows = [','.join(str(x) for x in b[i:i + 40]) for i in range(0, len(b), 40)]
    return '{\n' + ',\n'.join(rows) + '}'

def ret_zero(t):
    k = kind(t)
    if k == 'v':
        return 'return;'
    if k == 's':
        return '{%s __z; memset(&__z, 0, sizeof __z); return __z;}' % ctype(t)
    return 'return 0;'

# ---------------------------------------------------------------- per translation unit
class Unit:
    def __init__(s, prog, binmap):
        s.prog, s.binmap = prog, binmap
        s.strs, s.nstr = [], 0
        s.bins = {}
        s.failed = []
        s.cmem = {}
        s.static_done = set()

    def string(s, b):
        name = '__s%d' % s.nstr
        s.nstr += 1
        if len(b) > 60000:
            s.strs.append('static uint8_t %s[%d] = %s;' % (name, len(b) + 1, byte_list(b + b'\0')))
        else:
            s.strs.append('static uint8_t %s[%d] = %s;' % (name, len(b) + 1, cstr_lit(b)))
        return name

    def bin(s, name):
        if name not in s.bins:
            s.bins[name] = s.binmap[name]
        return cid(name)

    # -- classes
    def class_defs(s, cds):
        out = []
        for cd in cds:
            out.append('typedef %s %s %s;' % ('union' if cd.is_union else 'struct', clsname(cd), clsname(cd)))
        done, order = set(), []
        def deps(items, acc):
            for it in items:
                if it[0] == 'm':
                    t = it[2]
                    while t.k == 'arr':
                        t = t.base
                    if is_struct(t):
                        acc.append(t.cdef)
                else:
                    deps(it[1], acc)
            return acc
        def visit(cd):
            if cd in done:
                return
            done.add(cd)
            for d in deps(cd.items, []):
                if d in cds_set:
                    visit(d)
            order.append(cd)
        cds_set = set(cds)
        for cd in cds:
            visit(cd)
        out.append('#pragma pack(push, 1)')
        for cd in order:
            out += s.class_body(cd)
        out.append('#pragma pack(pop)')
        return out

    def class_body(s, cd):
        present = set()
        lines = ['%s %s {' % ('union' if cd.is_union else 'struct', clsname(cd))]
        def walk(items, ind):
            for it in items:
                if it[0] == 'm':
                    name, ty = it[1], it[2]
                    if sizeof_t(ty) == 0:
                        continue
                    cn = 'm_' + cid(name)
                    if name in present:   # duplicate member name: HolyC lookups hit the first one
                        cn += '__dup%d' % len(lines)
                    present.add(name)
                    lines.append(ind + cdecl(ty, cn) + ';')
                else:
                    if items_size(it[1], it[0] == 'u') == 0:
                        continue
                    lines.append(ind + ('union {' if it[0] == 'u' else 'struct {'))
                    walk(it[1], ind + '  ')
                    lines.append(ind + '};')
        walk(cd.items, '  ')
        size = items_size(cd.items, cd.is_union)
        if not present:
            lines.append('  uint8_t __empty;')
        lines.append('};')
        if present:
            lines.append('typedef char __chk_%s[(sizeof(%s)==%d)?1:-1];' % (clsname(cd), clsname(cd), size))
        s.cmem[cd] = present
        return lines

    def members(s, cd):
        if cd not in s.cmem:
            s.class_body(cd)
        return s.cmem[cd]

    # -- globals
    def gdecl(s, sym, ext=False):
        t = sym.ty
        if sym.dyn is not None:
            t = ptr(t.base)
        if t.k == 'prim' and t.name == 'U0':
            t = I64
        return ('extern ' if ext else '') + cdecl(t, cname(sym))

    def static_init(s, e, t):
        """C constant initializer for init e (('expr', N) | ('list', [...])) of type t, or None."""
        if e[0] == 'list':
            if t.k != 'arr' or is_struct(t.base) or len(e[1]) > (t.n or 0):
                return None
            parts = [s.static_init(x, t.base) for x in e[1]]
            if None in parts:
                return None
            return '{' + ', '.join(parts) + '}' if parts else '{0}'
        x = e[1]
        k = kind(t)
        if t.k == 'arr':
            if x.k == 'str' and t.base.k == 'prim' and t.base.name in ('U8', 'I8') and len(x.v) <= (t.n or 0):
                return cstr_lit(x.v) if len(x.v) < 60000 else byte_list(x.v)
            return None
        if k in ('i', 'f'):
            v = const_val(x)
            if v is None:
                return None
            if k == 'f':
                return lit_flt(float(v))
            if isinstance(v, float):
                if math.isinf(v) or math.isnan(v):
                    return None
                v = int(v)
            return '((%s)%s)' % (ctype(t), lit_int(v, t))
        if k == 'p':
            ct = ctype(t)
            while x.k == 'cast' and x.v == 'conv':
                x = x.a[0]
            if x.k == 'str':
                return '((%s)%s)' % (ct, s.string(x.v))
            if x.k == 'bin' and not x.a:
                return '((%s)%s)' % (ct, s.bin(x.v))
            if x.k == 'fref':
                return '((%s)%s)' % (ct, cname(x.v))
            if x.k == 'addr' and x.a[0].k == 'var' and x.a[0].v.kind == 'var' and x.a[0].v.dyn is None:
                return '((%s)&%s)' % (ct, cname(x.a[0].v))
            if x.k == 'var' and x.v.kind == 'var' and x.v.ty.k == 'arr' and x.v.dyn is None:
                return '((%s)%s)' % (ct, cname(x.v))
            v = const_val(x)
            if v is not None and not isinstance(v, float):
                return '((%s)%s)' % (ct, lit_int(v))
        return None

    def global_defs(s, gdefs, skip):
        out = []
        for sym, init, unit in gdefs:
            if sym.name in skip:
                continue
            si = None
            if init is not None and sym.dyn is None:
                si = s.static_init(init, sym.ty)
            if si is not None:
                s.static_done.add(sym)
                out.append('%s = %s;' % (s.gdecl(sym), si))
            else:
                out.append('%s;' % s.gdecl(sym))
        return out

    # -- functions
    def proto(s, sym, name=None):
        return cdecl(sym.ty.ret, '%s(%s)' % (name or cname(sym), param_types(sym.ty)))

    def stub(s, sym):
        nm = cname(sym)
        return ['TOS_WEAK %s { __tos_unimpl("%s"); %s }' % (s.proto_named(sym, 'TOS_STUB(%s)' % nm), sym.name, ret_zero(sym.ty.ret)),
                '#pragma comment(linker, "/alternatename:%s=__stub_%s")' % (nm, nm)]

    def proto_named(s, sym, name):
        ft = sym.ty
        ps = [cdecl(p.ty if p.ty.k != 'arr' else ptr(p.ty.base), 'p%d' % i) for i, p in enumerate(ft.params)]
        if ft.var:
            ps += ['int64_t p_argc', 'int64_t *p_argv']
        return cdecl(ft.ret, '%s(%s)' % (name, ', '.join(ps) or 'void'))

    def bins_out(s):
        return ['static uint8_t %s[%d] = %s;' % (cid(n), max(len(b), 1), byte_list(b or b'\0'))
                for n, b in sorted(s.bins.items())]

# ---------------------------------------------------------------- per function
class FnEm:
    def __init__(s, u, fn):
        s.u, s.fn = u, fn
        s.tmps = []
        s.names = {}
        s.brk = []           # [target or None for a C loop, try depth at entry]
        s.trydepth = 0
        s.nlab = 0
        s.ret = fn.ty.ret
        used = set()
        for i, ps in enumerate(fn.params):
            nm = 'p_' + cid(ps.name) if ps.name else 'p%d' % i
            used.add(nm)
            s.names[ps] = nm
        for l in fn.locals:
            nm, k = 'l_' + cid(l.name), 1
            while nm in used:
                k += 1
                nm = 'l_%s_%d' % (cid(l.name), k)
            used.add(nm)
            s.names[l] = nm

    def tmp(s, t):
        if t.k == 'arr':
            t = ptr(t.base)
        nm = '__t%d' % len(s.tmps)
        s.tmps.append(cdecl(t, nm) + ';')
        return nm

    # ------------------------------------------------ expressions
    def convc(s, c, frm, to):
        kt, kf = kind(to), kind(frm)
        if kt == 'v':
            return c
        if kf == 'v':
            return '((void)(%s), (%s)0)' % (c, ctype(to))
        if kt == 's' or kf == 's' or to.k in ('arr', 'func'):
            return c
        ct = ctype(to)
        if frm.k != 'arr' and frm.k != 'func' and ctype(frm) == ct:
            return c
        if kf == 'f' and kt == 'p':
            return '((%s)(int64_t)(%s))' % (ct, c)
        if kf == 'p' and kt == 'f':
            return '((double)(int64_t)(%s))' % c
        if kf == 'p' and to.k == 'prim' and to.name == 'Bool':   # a pointer's low byte can be 0
            return '((%s)((%s) != 0))' % (ct, c)
        return '((%s)(%s))' % (ct, c)

    def conv(s, e, to):
        return s.convc(s.ex(e), e.t, to)

    def truth(s, e):
        if kind(e.t) == 'v':
            return '((void)(%s), 0)' % s.ex(e)
        return '(%s)' % s.ex(e)

    def ex(s, e):
        k = e.k
        if k == 'int':
            return lit_int(e.v, e.t)
        if k == 'flt':
            return lit_flt(e.v)
        if k == 'str':
            return s.u.string(e.v)
        if k == 'bin' and not e.a:
            return s.u.bin(e.v)
        if k == 'var':
            return s.var(e.v)
        if k == 'fref':
            return cname(e.v)
        if k == 'call':
            return s.call(e)
        if k == 'asg':
            return s.asg(e)
        if k == 'bin':
            return s.binop(e)
        if k == 'chain':
            return s.chain(e)
        if k == 'un':
            x = e.a[0]
            if e.v == '!':
                return '((int64_t)!%s)' % s.truth(x)
            t = e.t if kind(e.t) != 'f' or e.v == '-' else I64
            return '(%s(%s))' % (e.v, s.conv(x, t))
        if k == 'deref':
            return '(*(%s))' % s.ex(e.a[0])
        if k == 'addr':
            return '(&%s)' % s.lv(e.a[0])
        if k in ('pre', 'post'):
            x = e.a[0]
            L = s.lv(x)
            if is_p(x.t) and elem_size(x.t) == 0:
                return '(%s = (%s)((uint8_t*)(%s) %s 1))' % (L, ctype(x.t), L, e.v[0])
            return '(%s%s)' % (e.v, L) if k == 'pre' else '(%s%s)' % (L, e.v)
        if k == 'idx':
            a, i = e.a
            ac = s.ex(a)
            if elem_size(a.t) == 0 and a.t.base.k != 'func':
                ac = '((uint8_t*)(%s))' % ac
            return '(%s)[%s]' % (ac, s.conv(i, I64))
        if k == 'mem':
            return s.mem(e)
        if k == 'sub':
            et = e.t.base.name
            return '((%s*)%s)' % (CINT[et], s.addr_of(e.a[0]))
        if k in ('sizeof', 'sizeofm', 'offset'):
            return lit_int(const_val(e))
        if k == 'cast':
            return s.cast(e)
        if k == 'lastclass':
            return s.u.string(b'')
        raise EmitError('expr kind %s' % k)

    def var(s, sym):
        if sym.kind in ('lvar', 'param'):
            if sym not in s.names:
                raise EmitError('unknown local %s' % sym.name)
            return s.names[sym]
        return cname(sym)

    def addr_of(s, x):
        """Address of x's storage (a temp for rvalues)."""
        if x.lv:
            return '(&%s)' % s.lv(x)
        t = s.tmp(x.t)
        return '(%s = %s, &%s)' % (t, s.ex(x), t)

    def lv(s, e):
        if e.k == 'cast':
            x = e.a[0]
            if x.lv and sizeof_t(x.t) == sizeof_t(e.t) or x.k in ('var', 'deref', 'idx', 'mem'):
                return '(*(%s)&%s)' % (ctype(ptr(e.t)), s.lv(x))
            return s.ex(e)
        if e.k == 'var':
            return s.var(e.v)
        return s.ex(e)

    def mem(s, e):
        obj, m = e.a[0], e.v
        cd = obj.t.cdef
        t = e.t
        present = m in s.u.members(cd)
        if cd.ibase or not present:
            base = s.addr_of(obj)
            if present:
                return '(((%s*)%s)->m_%s)' % (clsname(cd), base, cid(m))
            off = member_offset(cd, m)
            if t.k == 'arr':
                return '(*(%s)((uint8_t*)%s + %d))' % (cdecl(t.base, '(*)[1]'), base, off)
            if kind(t) == 'v':   # U0 marker member: only its address is meaningful
                return '(*((uint8_t*)%s + %d))' % (base, off)
            return '(*(%s)((uint8_t*)%s + %d))' % (ctype(ptr(t)), base, off)
        return '(%s).m_%s' % (s.lv(obj), cid(m))

    def cast(s, e):
        x = e.a[0]
        if e.v == 'i2f':
            return '__i2f((int64_t)(%s))' % s.ex(x)
        if e.v == 'f2i':
            return s.convc('__f2i(%s)' % s.ex(x), I64, e.t)
        if is_struct(e.t) or is_struct(x.t):
            if x.lv:
                return '(*(%s)&%s)' % (ctype(ptr(e.t)), s.lv(x))
            if is_struct(e.t):
                t = s.tmp(x.t)
                return '(%s = %s, *(%s)&%s)' % (t, s.ex(x), ctype(ptr(e.t)), t)
            return s.ex(x)
        return s.convc(s.ex(x), x.t, e.t)

    def call(s, e):
        ft, f, args = e.v, e.a[0], e.a[1:]
        fc = cname(f.v) if f.k == 'fref' else '(%s)' % s.ex(f)
        out = [s.conv(a, p.ty) for p, a in zip(ft.params, args)]
        if ft.var:
            extra = args[len(ft.params):]
            if extra:
                out += [str(len(extra)), '(int64_t[]){%s}' % ', '.join(s.varg(a) for a in extra)]
            else:
                out += ['0', 'NULL']
        return '%s(%s)' % (fc, ', '.join(out))

    def varg(s, a):
        k = kind(a.t)
        c = s.ex(a)
        if k == 'f':
            return '__f2i(%s)' % c
        if k == 'v':
            return '((void)(%s), 0LL)' % c
        if k == 's':
            return '(int64_t)%s' % s.addr_of(a)
        return '(int64_t)(%s)' % c

    def arith_t(s, op, lt, rt):
        if op in CMP or op in ('&&', '||', '^^'):
            return I64
        if is_f(lt) or is_f(rt):
            return I64 if op in BITOPS else F64
        if int_base(lt) == 'U64' or int_base(rt) == 'U64':
            return U64
        return I64

    def binop(s, e):
        op, (l, r), t = e.v, e.a, e.t
        if op in ('&&', '||'):
            return '((int64_t)(%s %s %s))' % (s.truth(l), op, s.truth(r))
        if op == '^^':
            return '((int64_t)(!%s != !%s))' % (s.truth(l), s.truth(r))
        if op in CMP:
            d = s.cmp_dom(l.t, r.t)
            return '((int64_t)(%s %s %s))' % (s.conv(l, d), op, s.conv(r, d))
        if op == '-' and is_p(l.t) and is_p(r.t):
            return '((int64_t)((uint8_t*)(%s) - (uint8_t*)(%s)) / %d)' % (s.ex(l), s.ex(r), elem_size(l.t) or 1)
        if is_p(t):
            p, i = (l, r) if is_p(l.t) else (r, l)
            ic = s.conv(i, I64)
            if elem_size(p.t) == 0:
                return '((%s)((uint8_t*)(%s) %s %s))' % (ctype(t), s.ex(p), op, ic)
            return '(%s %s %s)' % (s.ex(p), op, ic)
        return s.arith(op, s.ex(l), l.t, s.ex(r), r.t, t)

    def arith(s, op, lc, lt, rc, rt, t):
        if op in ('<<', '>>'):
            a, b = s.convc(lc, lt, t), s.convc(rc, rt, I64)
        else:
            a, b = s.convc(lc, lt, t), s.convc(rc, rt, t)
        if is_f(t):
            if op == '%':
                return 'fmod(%s, %s)' % (a, b)
            if op == '`':
                return 'pow(%s, %s)' % (a, b)
        if op == '`':
            return '((%s)__tos_ipow((int64_t)%s, (int64_t)%s))' % (ctype(t), a, b)
        return '(%s %s %s)' % (a, op, b)

    def cmp_dom(s, a, b):
        if kind(a) == 'f' or kind(b) == 'f':
            return F64
        if kind(a) == 'p' or kind(b) == 'p' or int_base(a) == 'U64' or int_base(b) == 'U64':
            return U64
        return I64

    def chain(s, e):
        parts = e.a
        ops = parts[0::2]
        cs = [s.ex(x) for x in ops]
        first, second = list(cs), list(cs)
        for i in range(1, len(ops) - 1):
            if ops[i].k not in ('var', 'int', 'flt'):
                t = s.tmp(ops[i].t)
                first[i], second[i] = '(%s = %s)' % (t, cs[i]), t
        terms = []
        for i in range(len(ops) - 1):
            l, r, op = ops[i], ops[i + 1], parts[2 * i + 1]
            d = s.cmp_dom(l.t, r.t)
            lc = second[i] if i > 0 else first[i]
            terms.append('(%s %s %s)' % (s.convc(lc, l.t, d), op, s.convc(first[i + 1], r.t, d)))
        return '((int64_t)(%s))' % ' && '.join(terms)

    def asg(s, e):
        op, (l, r) = e.v, e.a
        L, lt = s.lv(l), l.t
        if op == '=':
            if lt.k == 'arr':
                return 'memcpy(%s, %s, %d)' % (L, s.ex(r), sizeof_t(lt))
            return '(%s = %s)' % (L, s.conv(r, lt))
        bop = op[:-1]
        if is_p(lt) and bop in ('+', '-'):
            if elem_size(lt) == 0:
                return '(%s = (%s)((uint8_t*)(%s) %s %s))' % (L, ctype(lt), L, bop, s.conv(r, I64))
            return '(%s %s %s)' % (L, op, s.conv(r, I64))
        if bop == '`' or (bop in BITOPS + ('%',) and (is_f(lt) or is_f(r.t))) or is_p(lt):
            t = s.arith_t(bop, lt, r.t)
            return '(%s = %s)' % (L, s.convc(s.arith(bop, L, lt, s.ex(r), r.t, t), t, lt))
        rt = s.arith_t(bop, lt, r.t) if bop not in ('<<', '>>') else I64
        return '(%s %s %s)' % (L, op, s.conv(r, rt))

    # ------------------------------------------------ statements
    def emit(s, name=None):
        fn = s.fn
        body = []
        s.stmt(fn.body if isinstance(fn.body, tuple) else ('block', fn.body), body, '  ')
        sym = fn.sym
        ps = ['%s' % cdecl(p.ty if p.ty.k != 'arr' else ptr(p.ty.base), s.names[p]) for p in fn.params]
        if not ps:
            ps = ['void']
        head = cdecl(sym.ty.ret, '%s(%s)' % (name or cname(sym), ', '.join(ps)))
        out = []
        if fn.has_try:
            out.append('#pragma optimize("", off)')
        out.append(('TOS_NOOPT ' if fn.has_try else '') + head + ' {')
        for l in fn.locals:
            out.append('  ' + s.local_decl(l))
        for t in s.tmps:
            out.append('  ' + t)
        out += body
        if kind(s.ret) != 'v':
            out.append('  ' + ret_zero(s.ret))
        out.append('}')
        if fn.has_try:
            out.append('#pragma optimize("", on)')
        return out

    def local_decl(s, l):
        t = l.ty
        if t.k == 'prim' and t.name == 'U0':
            t = I64
        d = cdecl(t, s.names[l])
        if l.static:
            init = s.static_inits.get(l) if hasattr(s, 'static_inits') else None
            return 'static %s%s;' % (d, ' = ' + init if init else '')
        return d + ';'

    def stmt(s, st, out, ind):
        k = st[0]
        if k == 'block':
            if len(st) > 2:
                for x in st[1]:
                    s.stmt(x, out, ind)
                return
            out.append(ind[:-2] + '{' if False else ind + '{')
            for x in st[1]:
                s.stmt(x, out, ind + '  ')
            out.append(ind + '}')
        elif k == 'nop':
            pass
        elif k == 'expr':
            out.append(ind + s.ex(st[1]) + ';')
        elif k == 'label':
            out.append(ind + 'L_%s:;' % cid(st[1]))
        elif k == 'asm':
            if len(st) > 1 and st[1] == 'PAUSE':
                out.append(ind + '__tos_pause();')
        elif k == 'if':
            out.append(ind + 'if %s {' % s.cond(st[1]))
            s.stmt(st[2], out, ind + '  ')
            if st[3] is not None:
                out.append(ind + '} else {')
                s.stmt(st[3], out, ind + '  ')
            out.append(ind + '}')
        elif k == 'while':
            out.append(ind + 'while %s {' % s.cond(st[1]))
            s.loop_body(st[2], out, ind)
            out.append(ind + '}')
        elif k == 'do':
            out.append(ind + 'do {')
            s.loop_body(st[1], out, ind)
            out.append(ind + '} while %s;' % s.cond(st[2]))
        elif k == 'for':
            init = ', '.join(s.ex(x) for x in st[1]) if st[1] else ''
            c = s.truth(st[2]) if st[2] is not None else ''
            step = ', '.join(s.ex(x) for x in st[3]) if st[3] else ''
            out.append(ind + 'for (%s; %s; %s) {' % (init, c, step))
            s.loop_body(st[4], out, ind)
            out.append(ind + '}')
        elif k == 'break':
            if not s.brk:
                raise EmitError('break outside loop/switch')
            tgt, depth = s.brk[-1]
            pre = '__tos_try_unwind(%d); ' % (s.trydepth - depth) if s.trydepth > depth else ''
            out.append(ind + pre + ('break;' if tgt is None else 'goto %s;' % tgt[0]))
        elif k == 'return':
            s.ret_stmt(st[1], out, ind)
        elif k == 'goto':
            out.append(ind + 'goto L_%s;' % cid(st[1]))
        elif k == 'try':
            s.fn.has_try = True
            n = s.nlab
            s.nlab += 1
            out.append(ind + '{ TosTry __tf%d; if (!setjmp(__tf%d.jb)) { __tos_try_push(&__tf%d);' % (n, n, n))
            s.trydepth += 1
            s.stmt(st[1], out, ind + '  ')
            s.trydepth -= 1
            out.append(ind + '  __tos_try_pop(&__tf%d);' % n)
            out.append(ind + '} else {')
            s.stmt(st[2], out, ind + '  ')
            out.append(ind + '  __tos_catch_end();')
            out.append(ind + '} }')
        elif k == 'switch':
            s.switch(st, out, ind)
        elif k == 'decl':
            sym, init = st[1], st[2]
            if init is None or (sym.static and sym in getattr(s, 'static_inits', {})):
                return
            if sym.static:
                g = '__once_%s' % s.names[sym]
                s.tmps.append('static int8_t %s;' % g)
                out.append(ind + 'if (!%s) { %s = 1;' % (g, g))
                s.init_assign(s.names[sym], sym.ty, init, out, ind + '  ')
                out.append(ind + '}')
            else:
                s.init_assign(s.names[sym], sym.ty, init, out, ind)
        elif k == 'ginit':
            sym, init = st[1], st[2]
            if sym in s.u.static_done or sym.name in s.u.skip:
                return
            tgt = cname(sym)
            if sym.dyn is not None:
                out.append(ind + '%s = calloc(%s, %d);' % (tgt, s.conv(sym.dyn, I64), sizeof_t(sym.ty.base) or 1))
                if init is not None and init[0] == 'list':
                    for i, it in enumerate(init[1]):
                        s.init_assign('%s[%d]' % (tgt, i), sym.ty.base, it, out, ind)
                return
            if init is not None:
                s.init_assign(tgt, sym.ty, init, out, ind)
        else:
            raise EmitError('stmt kind %s' % k)

    def cond(s, e):
        return '(%s)' % s.truth(e)

    def loop_body(s, body, out, ind):
        s.brk.append((None, s.trydepth))
        s.stmt(body, out, ind + '  ')
        s.brk.pop()

    def ret_stmt(s, e, out, ind):
        unwind = '__tos_try_unwind(%d); ' % s.trydepth if s.trydepth else ''
        if kind(s.ret) == 'v':
            if e is not None:
                out.append(ind + '(void)(%s);' % s.ex(e))
            out.append(ind + unwind + 'return;')
            return
        if e is None:
            out.append(ind + unwind + ret_zero(s.ret))
            return
        v = s.conv(e, s.ret)
        if unwind:
            t = s.tmp(s.ret)
            out.append(ind + '%s = %s; %sreturn %s;' % (t, v, unwind, t))
        else:
            out.append(ind + 'return %s;' % v)

    def init_assign(s, tgt, t, init, out, ind):
        if init[0] == 'list':
            items = init[1]
            if t.k == 'arr':
                out.append(ind + 'memset(%s, 0, %d);' % (tgt, sizeof_t(t)))
                for i, it in enumerate(items):
                    s.init_assign('%s[%d]' % (tgt, i), t.base, it, out, ind)
                return
            if is_struct(t):
                mem = [(n, ty) for n, ty in t.cdef.order if sizeof_t(ty)]
                out.append(ind + 'memset(&%s, 0, sizeof %s);' % (tgt, tgt))
                for (n, ty), it in zip(mem, items):
                    s.init_assign('(%s).m_%s' % (tgt, cid(n)), ty, it, out, ind)
                return
            if items:
                s.init_assign(tgt, t, items[0], out, ind)
            return
        e = init[1]
        if t.k == 'arr':
            if e.k == 'str':
                n = min(len(e.v) + 1, sizeof_t(t)) if t.n else len(e.v) + 1
                out.append(ind + 'memcpy(%s, %s, %d);' % (tgt, s.u.string(e.v), n))
                return
            if e.t.k not in ('arr', 'ptr') or t.base.k == 'ptr':
                s.init_assign('%s[0]' % tgt, t.base, init, out, ind)   # scalar for an array member: first element (CInit.HC: pad[3] <- 0)
                return
            out.append(ind + 'memcpy(%s, %s, %d);' % (tgt, s.ex(e), sizeof_t(t)))
            return
        out.append(ind + '%s = %s;' % (tgt, s.conv(e, t)))

    def case_val(s, e):
        v = const_val(e, True)
        if v is None or isinstance(v, float):
            raise EmitError('non-constant case')
        return int(v)

    def switch(s, st, out, ind):
        e, items = st[1], st[2]
        n = s.nlab
        s.nlab += 1
        sv = s.tmp(I64)
        end = 'L_sw%d_end' % n
        cases, groups, last, dflt, g = [], [], -1, None, None
        for it in items:
            if it[0] == 'case':
                lo = last + 1 if it[1] is None else s.case_val(it[1])
                hi = s.case_val(it[2]) if it[2] is not None else lo
                last = hi
                cases.append((lo, hi, 'L_sw%d_c%d' % (n, len(cases)), g))
            elif it[0] == 'default':
                dflt = 'L_sw%d_def' % n
            elif it[0] == 'start':
                g = len(groups)
                groups.append([])
            elif it[0] == 'end':
                g = None
        sel = s.tmp(I64) if groups else None
        for i, c in enumerate(cases):
            if c[3] is not None:
                groups[c[3]].append(i)
        def go(i):
            lo, hi, lab, grp = cases[i]
            if grp is None:
                return 'goto %s;' % lab
            return '{ %s = %d; goto L_sw%d_g%d; }' % (sel, i, n, grp)
        out.append(ind + '{ %s = %s;' % (sv, s.conv(e, I64)))
        singles, seen = [], set()
        for i, (lo, hi, lab, grp) in enumerate(cases):
            if lo != hi:
                out.append(ind + 'if (%s >= %s && %s <= %s) %s' % (sv, lit_int(lo), sv, lit_int(hi), go(i)))
            elif lo not in seen:
                seen.add(lo)
                singles.append((lo, i))
        out.append(ind + 'switch (%s) {' % sv)
        for lo, i in singles:
            out.append(ind + '  case %s: %s' % (lit_int(lo), go(i)))
        out.append(ind + '  default: goto %s;' % (dflt or end))
        out.append(ind + '}')
        tgt = [end]
        s.brk.append((tgt, s.trydepth))
        ci, g, gfirst = 0, None, set()
        for it in items:
            if it[0] == 'case':
                lo, hi, lab, grp = cases[ci]
                if grp is not None and grp not in gfirst:
                    gfirst.add(grp)
                    out.append(ind + 'switch (%s) {' % sel)
                    for j in groups[grp]:
                        out.append(ind + '  case %d: goto %s;' % (j, cases[j][2]))
                    out.append(ind + '}')
                out.append(ind + '%s:;' % lab)
                ci += 1
            elif it[0] == 'default':
                out.append(ind + '%s:;' % dflt)
            elif it[0] == 'start':
                g = 0 if g is None else g + 1
                g = len([x for x in gfirst]) if False else g
                out.append(ind + 'L_sw%d_g%d:;' % (n, s._gidx(items, it)))
                tgt[0] = 'L_sw%d_g%d_end' % (n, s._gidx(items, it))
            elif it[0] == 'end':
                out.append(ind + '%s:;' % tgt[0])
                tgt[0] = end
            else:
                s.stmt(it, out, ind + '  ')
        s.brk.pop()
        out.append(ind + '%s:; }' % end)

    @staticmethod
    def _gidx(items, it):
        k = 0
        for x in items:
            if x is it:
                return k
            if x[0] == 'start':
                k += 1
        raise EmitError('start label')

# ---------------------------------------------------------------- drivers
HEAD = '/* generated by hc_emit.py from TempleOS HolyC sources -- do not edit */\n'

def binmap_of(p):
    src = p.pp.src
    return {name: src.bins[key] for key, name in src.binsym.items()}

def static_locals(u, fn):
    """Constant initializers of static locals become C static initializers."""
    out = {}
    def walk(x):
        if isinstance(x, tuple) and x:
            if x[0] == 'decl' and x[1].static and x[2] is not None:
                si = u.static_init(x[2], x[1].ty)
                if si is not None:
                    out[x[1]] = si
            for y in x[1:]:
                walk(y)
        elif isinstance(x, list):
            for y in x:
                walk(y)
    walk(fn.body)
    return out

def emit_funcs(u, fns, names=None):
    out = []
    for fn in fns:
        em_names = names.get(fn) if names else None
        sl = static_locals(u, fn)
        try:
            em = FnEm(u, fn)
            em.static_inits = sl
            out += em.emit(em_names)
        except Exception as e:
            u.failed.append((fn.sym.name, '%s: %s' % (type(e).__name__, e)))
            if os.environ.get('TB'):
                traceback.print_exc()
            sym = fn.sym
            out.append('%s { __tos_unimpl("%s (translation failed)"); %s }' %
                       (u.proto_named(sym, em_names or cname(sym)), sym.name, ret_zero(sym.ty.ret)))
        out.append('')
    return out

def emit_lib(p, out_dir):
    prog = p.prog
    u = Unit(prog, binmap_of(p))
    u.skip = p.rt
    # header
    h = [HEAD, '#pragma once', '#include "tos_rt.h"', '']
    h += u.class_defs(prog.class_order)
    h.append('')
    for sym in prog.funcs:
        h.append(u.proto(sym) + ';')
    h.append('void __init_lib(void);')
    h.append('')
    for sym in prog.gvars:
        h.append(u.gdecl(sym, ext=True) + ';')
    open(os.path.join(out_dir, 'tos_gen.h'), 'w').write('\n'.join(h) + '\n')
    # library body
    fdefs = [f for f in prog.fdefs if not f.sym.native]
    init = prog.inits['lib']
    funcs = emit_funcs(u, fdefs) + emit_funcs(u, [init], {init: '__init_lib'})
    defined = {sym for sym, _, _ in prog.gdefs}
    gl = u.global_defs([g for g in prog.gdefs if g[0].name not in p.rt], set())
    for sym in prog.gvars:   # declared extern but never defined anywhere: weak zero definition
        if sym not in defined and sym.name not in p.rt:
            gl.append('TOS_WEAK %s;' % u.gdecl(sym, ).replace(cname(sym), 'TOS_STUB(%s)' % cname(sym), 1))
            gl.append('#pragma comment(linker, "/alternatename:%s=__stub_%s")' % (cname(sym), cname(sym)))
    stubs = []
    for sym in prog.funcs:
        if (sym.native or not getattr(sym, 'func', None)) and sym.name not in p.rt:
            stubs += u.stub(sym)
    ftab = fun_table(u, [f for f in prog.funcs if prog.globals.get(f.name) is f], '__tos_funs')
    c = [HEAD, '#include "tos_gen.h"', ''] + u.bins_out() + u.strs + [''] + gl + [''] + stubs + [''] + funcs
    c += file_table(p.pp.src, hc_build.EMBED_FILES) + ftab
    open(os.path.join(out_dir, 'tos_lib.c'), 'w').write('\n'.join(c) + '\n')
    return u

def data_file(src, nm):
    """A data file's bytes, from the original <nm>.Z under bin_root when it is stored uncompressed:
    the converted trees' StadiumBG.GR lost 20 bytes on the way out of TempleOS."""
    z = os.path.join(src.bin_root, nm + '.Z')
    if os.path.exists(z):
        d = open(z, 'rb').read()
        _, size, kind = struct.unpack_from('<qqB', d)
        if kind == 1:   # CT_NONE; shortcut: LZW-packed .Z files fall back to the converted copy, decode them if one turns up broken
            return d[17:17 + size]
    return open(os.path.join(src.code_root, nm), 'rb').read()

def file_table(src, names, table='__tos_files'):
    """TempleOS files the runtime's FileRead serves (no disk image on the host)."""
    rows = []
    for i, nm in enumerate(names):
        rows.append('static const uint8_t %s%d[] = %s;' % (table, i, byte_list(data_file(src, nm))))
    rows.append('const TosFile %s[] = {' % table)
    rows += ['  {"%s", %s%d, sizeof %s%d},' % (nm, table, i, table, i) for i, nm in enumerate(names)]
    return rows + ['  {0}};']

def thunk_default(u, p):
    """C for a param's default value, or 0 when there is none or it needs a function context."""
    if p.dflt is None or p.dflt.k == 'lastclass':
        return '0'
    try:
        em = FnEm(u, next(iter(u.prog.inits.values())))   # any Func: defaults are context-free expressions
        em.static_inits = {}
        c = em.ex(p.dflt)
        return '0' if em.tmps else c
    except Exception:
        return '0'

def fun_table(u, syms, table):
    """TosFun table: every function callable by name (see TosThunk in tos_rt.h)."""
    rows, ents = [], []
    for i, sym in enumerate(syms):
        ft = sym.ty
        if kind(ft.ret) == 's' or any(kind(p.ty) == 's' for p in ft.params):
            continue
        args = []
        for j, p in enumerate(ft.params):
            ty = ptr(p.ty.base) if p.ty.k == 'arr' else p.ty
            ct = cdecl(ty)
            k = kind(ty)
            val = '__tos_af(argv,%d,m)' % j if k == 'f' else '(%s)__tos_ai(argv,%d,m)' % (ct, j)
            args.append('__TOS_HAS(%d) ? %s : (%s)(%s)' % (j, val, ct, thunk_default(u, p)))
        n = len(ft.params)
        if ft.var:
            args += ['argc > %d ? argc - %d : 0' % (n, n), 'argv + %d' % n]
        call = '%s(%s)' % (cname(sym), ', '.join(args))
        k = kind(ft.ret)
        body = ('%s; return 0;' % call if k == 'v' else
                'return __f2i(%s);' % call if k == 'f' else 'return (int64_t)(%s);' % call)
        rows.append('static int64_t %s_th%d(int64_t argc, int64_t *argv, uint64_t skip, uint64_t m) '
                    '{ (void)argc; (void)argv; (void)skip; (void)m; %s }' % (table, i, body))
        ents.append('  {"%s", %s_th%d, (void *)%s},' % (sym.name, table, i, cname(sym)))
    return rows + ['const TosFun %s[] = {' % table] + ents + ['  {0}};']

def game_files(root, rel):
    """Data files beside a game that has its own folder (Stadium/Stadium.HC, Apps/X/Run.HC)."""
    d, base = os.path.split(rel.replace('\\', '/'))
    if not (base.lower().startswith('run.') or os.path.splitext(base)[0] == os.path.basename(d)):
        return []
    return sorted('%s/%s' % (d, f) for f in os.listdir(os.path.join(root, d))
                  if os.path.isfile(os.path.join(root, d, f)) and
                  os.path.splitext(f)[1].upper() not in ('.HC', '.HH', '.Z'))

game_name = hc_build.game_name

def holyc_text(toks):
    """Source text of a #define body, for the run-time symbol table."""
    out = []
    for t in toks:
        if t.k == 'str':
            out.append('"%s"' % ''.join(chr(c) if 32 <= c < 127 and c not in (34, 92) else '\\x%02X' % c for c in t.v))
        elif t.k == 'char':
            out.append('0x%X' % (t.v[0] & (2**64 - 1)))
        elif t.k == 'float':
            out.append(repr(t.v))
        else:
            out.append(t.v.decode('latin-1') if isinstance(t.v, bytes) else str(t.v))
    return ' '.join(out)

def define_table(p):
    """TempleOS keeps every #define in the hash table and parses them from strings at run time
    (menus, DolDoc); TosDefinesLoad (C runtime) loads this table into Adam's."""
    rows = ['const char *const __tos_defines[] = {']
    for name, toks in p.pp.defines.items():
        nm = name if isinstance(name, bytes) else name.encode('latin-1')
        rows.append('  %s, %s,' % (cstr_lit(nm), cstr_lit(holyc_text(toks).encode('latin-1'))))
    return rows + ['  0};']

def emit_game(p, rel, out_dir):
    prog, mk = p.prog, p.mark
    u = Unit(prog, binmap_of(p))
    u.skip = p.rt
    units = [x for x in prog.init_order if x != 'lib']
    classes = prog.class_order[mk['classes']:]
    for cd in prog.class_order[:mk['classes']]:
        u.members(cd)
    gfuncs = [sym for sym in prog.funcs[mk['funcs']:]]
    fdefs = [f for f in prog.fdefs if f.sym.unit in units]
    gdefs = [g for g in prog.gdefs if g[2] in units and g[0].name not in p.rt]
    c = [HEAD, '#include "tos_gen.h"', '']
    c += u.class_defs(classes) if classes else []
    for sym in gfuncs:
        c.append(u.proto(sym) + ';')
    inits = [prog.inits[x] for x in units]
    names = {f: f.sym.cname for f in inits}
    for f in inits:
        c.append('void %s(void);' % f.sym.cname)
    for sym, _, _ in gdefs:
        c.append(u.gdecl(sym, ext=True) + ';')
    body = emit_funcs(u, fdefs) + emit_funcs(u, inits, names)
    gl = u.global_defs(gdefs, set())
    stubs = []
    for sym in gfuncs:
        if sym.native or not getattr(sym, 'func', None):
            stubs += u.stub(sym)
    main = ['void __tos_game_main(void) {'] + ['  %s();' % f.sym.cname for f in inits] + ['}']
    ftab = fun_table(u, [f for f in gfuncs if prog.globals.get(f.name) is f], '__tos_game_funs')
    c += [''] + u.bins_out() + u.strs + [''] + gl + [''] + stubs + [''] + body + main + [''] + define_table(p)
    c += file_table(p.pp.src, game_files(p.pp.src.code_root, rel), '__tos_game_files') + ftab
    name = game_name(rel)
    open(os.path.join(out_dir, name + '.c'), 'w').write('\n'.join(c) + '\n')
    return u, name

def main():
    code_root, bin_root, out_dir = sys.argv[1:4]
    games = sys.argv[4:]
    os.makedirs(out_dir, exist_ok=True)
    p = hc_build.load(code_root, bin_root)
    u = emit_lib(p, out_dir)
    print('lib: %d functions failed' % len(u.failed))
    for nm, err in u.failed:
        print('  ', nm, err)
    for g in games:
        p = hc_build.load(code_root, bin_root, g)
        u, name = emit_game(p, g, out_dir)
        print('%s: %d functions failed' % (name, len(u.failed)))
        for nm, err in u.failed:
            print('  ', nm, err)

if __name__ == '__main__':
    main()
