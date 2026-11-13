"""HolyC parser + type checker producing a typed AST."""
import math, os, struct
from hc_lex import Tok

class CompileError(Exception):
    pass

# ---------------------------------------------------------------- types
INTS = {'I8': (1, True), 'U8': (1, False), 'I16': (2, True), 'U16': (2, False), 'I32': (4, True),
        'U32': (4, False), 'I64': (8, True), 'U64': (8, False), 'Bool': (1, True)}
PRIMS = set(INTS) | {'F64', 'U0'}
SUBINT = {'i8': ('I8', 8), 'u8': ('U8', 8), 'i16': ('I16', 4), 'u16': ('U16', 4), 'i32': ('I32', 2),
          'u32': ('U32', 2), 'i64': ('I64', 1), 'u64': ('U64', 1), 'f64': ('F64', 1)}

class Ty:
    __slots__ = ('k', 'name', 'base', 'n', 'ret', 'params', 'var', 'cdef')
    def __init__(s, k, name=None, base=None, n=None, ret=None, params=None, var=False, cdef=None):
        s.k, s.name, s.base, s.n, s.ret, s.params, s.var, s.cdef = k, name, base, n, ret, params, var, cdef
    def __repr__(s):
        if s.k == 'prim': return s.name
        if s.k == 'ptr': return '%r*' % s.base
        if s.k == 'arr': return '%r[%s]' % (s.base, s.n)
        if s.k == 'cls': return s.cdef.name
        return 'fn(%r)' % s.ret

def prim(n): return Ty('prim', name=n)
def ptr(t): return Ty('ptr', base=t)
I64, U64, F64, U0, U8, BOOL = prim('I64'), prim('U64'), prim('F64'), prim('U0'), prim('U8'), prim('Bool')

def is_f(t): return t.k == 'prim' and t.name == 'F64'
def int_base(t):
    """Integer prim name for ints and int-backed classes, else None."""
    if t.k == 'prim' and t.name in INTS: return t.name
    if t.k == 'cls' and t.cdef.ibase: return t.cdef.ibase
    return None
def is_i(t): return int_base(t) is not None
def is_p(t): return t.k in ('ptr', 'arr')
def is_fn(t): return t.k == 'func'
def is_arith(t): return is_f(t) or is_i(t)
def deref_t(t):
    if t.k in ('ptr', 'arr'): return t.base
    raise CompileError('deref of non-pointer %r' % t)

def same(a, b):
    if a is b: return True
    if a.k != b.k: return False
    if a.k == 'prim': return a.name == b.name or {a.name, b.name} <= {'I8', 'Bool'}
    if a.k in ('ptr', 'arr'): return same(a.base, b.base)
    if a.k == 'cls': return a.cdef is b.cdef
    if a.k == 'func':
        return same(a.ret, b.ret) and a.var == b.var and len(a.params) == len(b.params) and \
            all(same(p.ty, q.ty) for p, q in zip(a.params, b.params))
    return False

class Param:
    __slots__ = ('name', 'ty', 'dflt')
    def __init__(s, name, ty, dflt):
        s.name, s.ty, s.dflt = name, ty, dflt

class ClassDef:
    def __init__(s, name, is_union=False):
        s.name, s.is_union = name, is_union
        s.ibase = None         # int prim for 'I64 class Foo'
        s.items = []           # ('m', name, ty) | ('u'/'s', [items])  (anonymous union/struct)
        s.lookup = {}          # member -> ty
        s.order = []           # flat member order (for offset of U0 markers)
        s.defined = False
        s.cname = name
        s.base = None
        s.meta = {}            # member -> {key: expr}

class Sym:
    _n = 0
    def __init__(s, kind, name, ty, cname=None):
        s.kind, s.name, s.ty = kind, name, ty    # kind: var|func|lvar|param
        s.cname = cname or name
        s.defined = False          # function has body / global has storage here
        s.extern = False
        s.unit = None              # which source unit defined it
        s.native = False           # needs hand-written C
        s.dyn = None               # run-time array length expr (global arrays sized by DefinePrint)
        s.cexpr = None             # value expr (global init / DefinePrint), folded only for array dims
        s.static = False
        Sym._n += 1
        s.id = Sym._n

class N:
    """Expression node."""
    __slots__ = ('k', 'v', 'a', 't', 'lv')
    def __init__(s, k, t, v=None, a=None, lv=False):
        s.k, s.t, s.v, s.a, s.lv = k, t, v, a or [], lv
    def __repr__(s):
        return '(%s %r %r)' % (s.k, s.v, s.a)

# ---------------------------------------------------------------- parser
BIN_LEVELS = [
    ['`', '<<', '>>'],
    ['*', '/', '%'],
    ['&'],
    ['^'],
    ['|'],
    ['+', '-'],
    ['<', '>', '<=', '>='],
    ['==', '!='],
    ['&&'],
    ['^^'],
    ['||'],
]
ASSIGN_OPS = {'=', '+=', '-=', '*=', '/=', '%=', '&=', '|=', '^=', '<<=', '>>='}
FUNC_FLAGS = {'public', 'interrupt', 'haserrcode', 'argpop', 'noargpop', 'no_warn'}
REGS = {'RAX', 'RBX', 'RCX', 'RDX', 'RSI', 'RDI', 'RBP', 'RSP', 'R8', 'R9', 'R10', 'R11', 'R12', 'R13',
        'R14', 'R15'}

class Func:
    def __init__(s, sym):
        s.sym = sym
        s.locals = []        # Syms
        s.body = None
        s.has_try = False
        s.labels = set()
        s.tmps = 0

class Parser:
    def __init__(s, pp, prog=None):
        s.pp = pp
        s.buf = []
        s.prog = prog or Program()
        s.fn = None          # current Func
        s.scope = None       # dict of locals
        s.unit = None
        s.skip_bodies = False
        s.native_files = ()  # path prefixes whose bodies are hardware/asm code: declarations only, C runtime supplies them
        s.dollar = 0
        s.dyn_dim = None     # run-time size of the global array being declared
        s.replace_file = None  # tools/ports/<Game>.HC: its functions replace the game's (same C name)

    # -- token helpers
    def peek(s, k=0):
        while len(s.buf) <= k:
            s.buf.append(s.pp.next())
        return s.buf[k]
    def next(s):
        t = s.peek()
        s.buf.pop(0)
        return t
    def isop(s, v, k=0):
        t = s.peek(k)
        return t.k == 'op' and t.v == v
    def isid(s, v, k=0):
        t = s.peek(k)
        return t.k == 'id' and t.v == v
    def accept(s, v):
        if s.isop(v):
            s.next(); return True
        return False
    def expect(s, v):
        t = s.next()
        if not (t.k == 'op' and t.v == v):
            s.err('expected %r got %r' % (v, t.v), t)
        return t
    def ident(s):
        t = s.next()
        if t.k != 'id':
            s.err('expected identifier got %r' % (t.v,), t)
        return t.v
    def err(s, msg, t=None):
        t = t or s.peek()
        raise CompileError('%s:%d: %s' % (t.file, t.line, msg))

    # -- symbol lookup
    def lookup(s, name):
        if s.scope is not None and name in s.scope:
            return s.scope[name]
        return s.prog.globals.get(name)
    def is_type_start(s, k=0):
        t = s.peek(k)
        if t.k != 'id': return False
        v = t.v
        if v in PRIMS or (v.endswith('i') and v[:-1] in PRIMS): return True
        if v in s.prog.classes:
            return not (s.scope is not None and v in s.scope)
        return v in ('class', 'union')

    # ------------------------------------------------------------ types
    def base_type(s):
        t = s.next()
        v = t.v
        if v.endswith('i') and v[:-1] in PRIMS and v not in s.prog.classes:
            v = v[:-1]
        if v in PRIMS:
            return prim(v)
        if v in s.prog.classes:
            return Ty('cls', cdef=s.prog.classes[v])
        s.err('unknown type %r' % v, t)

    def declarator(s, bt, allow_abstract=False):
        """Parse '*'* ( name | (*name)(params) ) [dims]. Returns (name, ty)."""
        t = bt
        while (s.isid('reg') or s.isid('noreg')) and (s.peek(1).k == 'id' or s.isop('*', 1)):
            # per-declarator register hint (`I64 reg RSI *dst`): ignored. `I8 reg,pad;` is a name.
            s.next()
            if s.peek().k == 'id' and s.peek().v in REGS:
                s.next()
        while s.accept('*'):
            t = ptr(t)
        if s.isop('(') and s.isop('*', 1):
            s.next(); s.next()
            nptr = 1
            while s.accept('*'):
                nptr += 1
            name = None
            if s.peek().k == 'id':
                name = s.ident()
            dims = s.dims()
            s.expect(')')
            params, var = s.param_list()
            ft = Ty('func', ret=t, params=params, var=var)
            for _ in range(nptr):
                ft = ptr(ft)
            for d in reversed(dims):
                ft = Ty('arr', base=ft, n=d)
            return name, ft
        name = None
        if s.peek().k == 'id' and not s.is_type_start():
            name = s.ident()
        elif not allow_abstract:
            s.err('expected declarator name')
        for d in reversed(s.dims()):
            t = Ty('arr', base=t, n=d)
        return name, t

    def dims(s):
        ds = []
        while s.isop('['):
            s.next()
            if s.isop(']'):
                ds.append(None)
            else:
                e = s.expr()
                v = const_val(e)
                if v is None and s.fn is None:
                    v = const_val(e, True)   # TempleOS sizes it at JIT time, after earlier globals ran
                if v is None and e.k == 'var' and e.v.name == 'mp_cnt':
                    v = 128   # shortcut: MP_PROCESSORS_NUM bound; the runtime's mp_cnt never exceeds it
                elif v is None and not ds and s.fn is None:
                    s.dyn_dim = e   # global sized at run time (TreeCheckers u[UNITS_NUM]): heap-allocated
                    ds.append(None); s.expect(']'); continue
                if v is None:
                    s.err('non-constant array size')
                ds.append(int(v))
            s.expect(']')
        return ds

    def param_list(s):
        s.expect('(')
        params, var = [], False
        if s.accept(')'):
            return params, var
        while True:
            if s.accept('...'):
                var = True
            else:
                while s.peek().k == 'id' and s.peek().v in ('reg', 'noreg'):
                    s.next()
                    if s.peek().k == 'id' and s.peek().v in REGS:
                        s.next()
                bt = s.base_type()
                name, ty = s.declarator(bt, allow_abstract=True)
                if ty.k == 'arr':
                    ty = ptr(ty.base)
                elif ty.k == 'prim' and ty.name == 'U0':
                    ty = I64   # "Bool Plot(U0,I64 x,...)": an unused 8-byte arg slot
                dflt = None
                if s.accept('='):
                    if s.isid('lastclass'):
                        s.next(); dflt = N('lastclass', ptr(U8))
                    else:
                        dflt = s.assign_expr()
                params.append(Param(name, ty, dflt))
            if s.accept(')'):
                return params, var
            if not s.accept(';'):   # KernelC.HH StrPrintHex has a ';' separator; HolyC accepts it
                s.expect(',')

    def type_name(s):
        """Type in a cast or sizeof: base '*'* or base (*)(params)."""
        bt = s.base_type()
        name, ty = s.declarator(bt, allow_abstract=True)
        return ty

    # ------------------------------------------------------------ classes
    def class_def(s, is_union, ibase=None, public=False):
        name = None
        if s.peek().k == 'id' and not s.isop('{', 0):
            name = s.ident()
        if name is None:
            s.prog.anon += 1
            name = '__anon%d' % s.prog.anon
        cd = s.prog.classes.get(name)
        if cd is None or cd.defined and s.isop('{'):
            cd = ClassDef(name, is_union)
            if name in s.prog.classes:   # redefinition (shadows)
                cd.cname = '%s__%d' % (name, s.prog.anon + 1); s.prog.anon += 1
            s.prog.classes[name] = cd
            s.prog.class_order.append(cd)
        cd.is_union = is_union
        if ibase:
            cd.ibase = ibase if ibase in INTS else None
            if ibase == 'F64':
                cd.ibase = 'F64'
        if s.accept(':'):
            base = s.prog.classes[s.ident()]
            cd.base = base
            cd.items = list(base.items)
            cd.lookup = dict(base.lookup)
            cd.order = list(base.order)
            cd.meta = dict(base.meta)
        if not s.isop('{'):
            return cd          # forward declaration
        s.next()
        cd.items += s.member_list(cd, '}')
        cd.defined = True
        return cd

    def member_list(s, cd, end):
        items = []
        while not s.accept(end):
            if s.accept(';'):
                continue
            if s.isid('$'):   # $=($+15)&-16; alignment directive: ignored
                while not s.accept(';'):
                    s.next()
                continue
            if s.isid('union') or s.isid('class'):
                kw = s.ident()
                if s.isop('{'):
                    s.next()
                    sub = s.member_list(cd, '}')
                    items.append(('u' if kw == 'union' else 's', sub))
                    if s.peek().k == 'id' and s.isop(';', 1):
                        s.err('named nested aggregate not supported')
                    s.accept(';')
                    continue
                s.buf.insert(0, Tok('id', kw, '', 0))
            s.dollar = items_size(cd.items + items, False)   # `$` = current class offset
            bt = s.base_type() if not (s.isid('class') or s.isid('union')) else s.inline_class()
            while True:
                name, ty = s.declarator(bt)
                items.append(('m', name, ty))
                cd.lookup[name] = ty
                cd.order.append((name, ty))
                # member meta data: `format "..."`, `data ...` etc.
                meta = {}
                while s.peek().k == 'id':
                    key = s.ident()
                    if s.isop(',') or s.isop(';'):
                        meta[key] = None
                        continue
                    meta[key] = s.assign_expr()
                cd.meta[name] = meta
                if s.accept(','):
                    continue
                s.expect(';')
                break
        return items

    def inline_class(s):
        kw = s.ident()
        cd = s.class_def(kw == 'union')
        return Ty('cls', cdef=cd)

    # ------------------------------------------------------------ expressions
    def expr(s):
        return s.assign_expr()

    def assign_expr(s):
        l = s.binary(len(BIN_LEVELS) - 1)
        t = s.peek()
        if t.k == 'op' and t.v in ASSIGN_OPS:
            s.next()
            r = s.assign_expr()
            if not l.lv:
                s.err('assignment to non-lvalue')
            return N('asg', l.t, t.v, [l, r])
        return l

    def binary(s, lvl):
        if lvl < 0:
            return s.unary()
        ops = BIN_LEVELS[lvl]
        l = s.binary(lvl - 1)
        if ops[0] == '<':    # chained comparisons
            parts = [l]
            while s.peek().k == 'op' and s.peek().v in ops:
                parts.append(s.next().v)
                parts.append(s.binary(lvl - 1))
            if len(parts) == 1:
                return l
            if len(parts) == 3:
                return s.mkbin(parts[1], parts[0], parts[2])
            return N('chain', I64, None, parts)
        while s.peek().k == 'op' and s.peek().v in ops:
            op = s.next().v
            r = s.binary(lvl - 1)
            l = s.mkbin(op, l, r)
        return l

    def mkbin(s, op, l, r):
        lt, rt = l.t, r.t
        if op in ('&&', '||', '^^'):
            return N('bin', I64, op, [l, r])
        if op in ('<', '>', '<=', '>=', '==', '!='):
            return N('bin', I64, op, [l, r])
        if op in ('+', '-'):
            if is_p(lt) and is_i(rt):
                return N('bin', ptr(lt.base), op, [l, r])
            if is_p(rt) and is_i(lt) and op == '+':
                return N('bin', ptr(rt.base), op, [l, r])
            if is_p(lt) and is_p(rt) and op == '-':
                return N('bin', I64, op, [l, r])
        if is_f(lt) or is_f(rt):
            if op in ('&', '|', '^', '<<', '>>'):
                return N('bin', I64, op, [l, r])
            return N('bin', F64, op, [l, r])
        if (int_base(lt) == 'U64' or int_base(rt) == 'U64'):
            return N('bin', U64, op, [l, r])
        return N('bin', I64, op, [l, r])

    def unary(s):
        t = s.peek()
        if t.k == 'op':
            v = t.v
            if v in ('-', '+', '!', '~'):
                s.next()
                x = s.unary()
                if v == '+': return x
                if v == '!': return N('un', I64, '!', [x])
                if v == '-' and x.k in ('int', 'flt'):
                    return N(x.k, x.t if x.k == 'flt' else I64, -x.v)
                rt = F64 if is_f(x.t) else (U64 if int_base(x.t) == 'U64' else I64)
                return N('un', rt, v, [x])
            if v == '*':
                s.next()
                x = s.unary()
                if x.t.k == 'cls' and not x.t.cdef.ibase and x.lv:
                    return N('addr', ptr(x.t), None, [x])   # struct value is its address (TTFFire.HC: *indirect_head)
                if is_i(x.t):
                    x = N('cast', ptr(I64), 'conv', [x])   # HolyC derefs an integer as I64* (KTask.HC Spawn: *task->rsp=data)
                bt = deref_t(x.t)
                return N('deref', bt, None, [x], lv=True)
            if v == '&':
                s.next()
                x = s.postfix(addr=True)
                if x.k == 'fref' or x.k == 'int':
                    return x  # HolyC ignores & on a constant (SpriteSideBar.HC: &SPf_SEL)
                if not x.lv:
                    s.err('& of non-lvalue')
                return N('addr', ptr(x.t), None, [x])
            if v in ('++', '--'):
                s.next()
                x = s.unary()
                return N('pre', x.t, v, [x])
        if t.k == 'id' and t.v == 'sizeof':
            s.next()
            par = s.accept('(')
            if s.is_type_start() and not (s.isop('.', 1)):
                ty = s.type_name()
                e = N('sizeof', I64, ty)
            elif s.peek().k == 'id' and s.peek().v in s.prog.classes and s.isop('.', 1):
                cname = s.ident(); s.next(); m = s.ident()
                e = N('sizeofm', I64, (s.prog.classes[cname], m))
            else:
                x = s.unary() if not par else s.expr()
                e = N('sizeof', I64, x.t)
            if par: s.expect(')')
            return e
        if t.k == 'id' and t.v == 'offset' and s.isop('(', 1):
            s.next(); s.expect('(')
            cname = s.ident(); cd = s.prog.classes[cname]
            path = []
            while s.accept('.'):
                path.append(s.ident())
            s.expect(')')
            return N('offset', I64, (cd, path))
        return s.postfix()

    def postfix(s, addr=False):
        e = s.primary(addr)
        while True:
            if e.k == 'fref' and not s.isop('(') and (not addr or s.isop('->') or s.isop('.') or s.isop('[')):
                e = s.call(e, noparen=True)   # bare function name = call
            t = s.peek()
            if t.k != 'op':
                break
            v = t.v
            if v == '(':
                if s.is_type_start(1) and not s.isid('class', 1) and not s.isid('union', 1):
                    s.next()
                    ty = s.type_name()
                    s.expect(')')
                    e = s.cast(e, ty)
                    continue
                if e.k == 'fref' and not addr:
                    e = s.call(e)
                    continue
                if is_fn(e.t) or (e.t.k == 'ptr' and is_fn(e.t.base)):
                    e = s.call(e)
                    continue
                if e.k == 'fref':
                    e = s.call(e)
                    continue
                s.err('call of non-function')
            elif v == '[':
                s.next()
                i = s.expr()
                s.expect(']')
                bt = deref_t(e.t)
                e = N('idx', bt, None, [e, i], lv=True)
            elif v in ('.', '->'):
                s.next()
                m = s.ident()
                e = s.member(e, m, v == '->')
            elif v in ('++', '--'):
                s.next()
                e = N('post', e.t, v, [e])
            else:
                break
        if e.k == 'fref' and not addr:
            e = s.call(e, noparen=True)
        return e

    def member(s, e, m, arrow):
        t = e.t
        if arrow:
            if not is_p(t):
                s.err('-> on non-pointer %r' % t)
            t = t.base
            e = N('deref', t, None, [e], lv=True)
        if t.k == 'cls' and m in t.cdef.lookup:
            return N('mem', t.cdef.lookup[m], m, [e], lv=True)
        if (t.k == 'prim' or (t.k == 'cls' and t.cdef.ibase)) and m in SUBINT:
            et, cnt = SUBINT[m]
            return N('sub', Ty('arr', base=prim(et), n=cnt), m, [e], lv=e.lv)
        s.err('no member %r in %r' % (m, t))

    def cast(s, e, ty):
        st = e.t
        if is_f(ty) and (is_i(st) or is_p(st)):
            if e.k == 'int':
                return N('flt', F64, struct.unpack('<d', struct.pack('<Q', e.v & (2**64 - 1)))[0])
            return N('cast', F64, 'i2f', [e], lv=e.lv)
        if is_f(st) and (is_i(ty) or is_p(ty)):
            if e.k == 'flt':
                v = struct.unpack('<q', struct.pack('<d', e.v))[0]
                return N('cast', ty, 'conv', [N('int', I64, v)])
            return N('cast', ty, 'f2i', [e], lv=e.lv)
        # HolyC: a cast of an lvalue is an lvalue reinterpreting its storage (`ptr(I64)+=s`)
        return N('cast', ty, 'conv', [e], lv=e.lv)

    def primary(s, addr=False):
        t = s.next()
        if t.k == 'int':
            return N('int', U64 if t.v >= 2**63 else I64, t.v)
        if t.k == 'float':
            return N('flt', F64, t.v)
        if t.k == 'char':
            v = t.v[0]
            return N('int', U64 if v >= 2**63 else I64, v)
        if t.k == 'str':
            b = t.v
            while s.peek().k == 'str':
                b += s.next().v
            return N('str', ptr(U8), b)
        if t.k == 'op' and t.v == '(':
            e = s.expr()
            s.expect(')')
            return e
        if t.k == 'id':
            name = t.v
            if name.startswith('__bin_'):
                s.prog.bin_used.add(name)
                return N('bin', ptr(U8), name)
            if name in ('__DIR__', '__FILE__', '__DATE__', '__TIME__', '__CMD_LINE__'):
                v = {'__DIR__': '/' + t.file.rsplit('/', 1)[0] if '/' in t.file else '/',
                     '__FILE__': '/' + t.file}.get(name, '')
                return N('str', ptr(U8), v.encode())
            if name == '__LINE__':
                return N('int', I64, t.line)
            if name == '$':
                return N('int', I64, s.dollar)
            sym = s.lookup(name)
            if sym is None:
                s.err('undefined %r' % name, t)
            s.prog.refs.add(sym)
            if sym.kind == 'func':
                return N('fref', sym.ty, sym)
            return N('var', sym.ty, sym, lv=True)
        s.err('unexpected %r' % (t.v,), t)

    def call(s, f, noparen=False):
        args = []
        if not noparen:
            s.expect('(')
            if not s.accept(')'):
                while True:
                    if s.isop(',') or s.isop(')'):
                        args.append(None)
                    else:
                        args.append(s.assign_expr())
                    if s.accept(')'):
                        break
                    s.expect(',')
        ft = f.t
        if ft.k == 'ptr':
            ft = ft.base
        if ft.k != 'func':
            s.err('call of non-function type %r' % ft)
        params = ft.params
        out = []
        for i, p in enumerate(params):
            a = args[i] if i < len(args) else None
            if a is None:
                if p.dflt is None:
                    s.err('missing arg %d (%s) in call to %s' % (i + 1, p.name,
                          f.v.name if f.k == 'fref' else 'fptr'))
                if p.dflt.k == 'lastclass':
                    prev = out[-1].t if out else None
                    nm = prev.base.cdef.name if prev is not None and prev.k == 'ptr' and \
                        prev.base.k == 'cls' else ''
                    a = N('str', ptr(U8), nm.encode())
                else:
                    a = p.dflt
            out.append(a)
        extra = args[len(params):]
        if extra and not ft.var:
            s.err('too many args to %s' % (f.v.name if f.k == 'fref' else 'fptr'))
        if any(a is None for a in extra):
            s.err('empty variadic arg')
        if f.k == 'fref' and f.v.name == 'DefinePrint' and len(extra) == 1 and \
                out[0].k == 'str' and out[1].k == 'str' and s.unit != 'lib':
            # game-time DefinePrint("NAME","%d",x): later code uses NAME as a constant whose value
            # is only known at run time -> make NAME a global variable and this an assignment.
            name = out[0].v.decode('latin-1')
            sym = s.prog.globals.get(name)
            if sym is None:
                sym = Sym('var', name, F64 if b'f' in out[1].v else I64, 'h_' + name)
                sym.defined, sym.unit = True, s.unit
                s.prog.globals[name] = sym
                s.prog.gvars.append(sym)
                s.prog.gdefs.append((sym, None, s.unit))
                sym.cexpr = extra[0]
            return N('asg', sym.ty, '=', [N('var', sym.ty, sym, lv=True), extra[0]])
        if f.k == 'fref':
            s.prog.refs.add(f.v)
        return N('call', ft.ret, ft, [f] + out + extra)

    # ------------------------------------------------------------ statements
    def stmt(s):
        t = s.peek()
        if t.k == 'op':
            if t.v == '{':
                return s.block()
            if t.v == ';':
                s.next(); return ('nop',)
        if t.k == 'str' or t.k == 'char':
            return s.print_stmt()
        if t.k == 'id':
            v = t.v
            if s.isop(':', 1) and v not in s.prog.classes and not s.is_type_start():
                s.next(); s.next()
                if s.fn: s.fn.labels.add(v)
                return ('label', v)
            if v.isupper() and s.lookup(v) is None and not s.is_type_start():
                # inline asm instruction line (e.g. PUSHFD / CLI): function needs a native version
                s.next()
                if v in ('PUSHFD', 'POPFD', 'CLI', 'STI', 'PAUSE') and (s.peek().nl or s.peek().k == 'eof'):
                    return ('asm', v)   # interrupt masking is moot: tasks run one at a time under a lock
                while not s.peek().nl and s.peek().k != 'eof':
                    s.next()
                if s.fn: s.fn.sym.native = True
                return ('asm',)
            h = getattr(s, 'st_' + v, None)
            if h and (s.lookup(v) is None):
                s.next()
                return h()
            if s.is_decl_start():
                return s.local_decl()
        es = s.expr_list(';')  # comma operator in statements: `a=1, b=2;`
        s.expect(';')
        return ('expr', es[0]) if len(es) == 1 else ('block', [('expr', e) for e in es])

    def is_decl_start(s):
        t = s.peek()
        if t.k != 'id': return False
        if t.v in ('public', 'extern', 'static', 'reg', 'noreg', 'no_warn', '_extern', '_intern',
                   'import', 'interrupt', 'haserrcode', 'argpop', 'noargpop', '_import'):
            return True
        if s.is_type_start():
            return True
        # `I64 class Foo`
        return False

    def print_stmt(s):
        t = s.next()
        if t.k == 'char':
            if t.v[1] == 0 and not s.isop(';'):
                e = s.expr()
            else:
                e = N('int', I64, t.v[0])
            s.expect(';')
            return ('expr', s.mkcall('PutChars', [e]))
        b = t.v
        while s.peek().k == 'str':
            b += s.next().v
        if b == b'' and not s.isop(';') and not s.isop(','):
            fmt = s.expr()
        else:
            fmt = N('str', ptr(U8), b)
        args = []
        while s.accept(','):
            args.append(s.assign_expr())
        s.expect(';')
        return ('expr', s.mkcall('Print', [fmt] + args))

    def mkcall(s, name, args):
        sym = s.prog.globals[name]
        s.prog.refs.add(sym)
        f = N('fref', sym.ty, sym)
        ft = sym.ty
        out = list(args)
        for i in range(len(args), len(ft.params)):
            out.append(ft.params[i].dflt)
        return N('call', ft.ret, ft, [f] + out)

    def block(s):
        s.expect('{')
        out = []
        while not s.accept('}'):
            out.append(s.stmt())
        return ('block', out)

    def cond(s):
        s.expect('(')
        e = s.expr()
        s.expect(')')
        return e

    def st_if(s):
        c = s.cond()
        a = s.stmt()
        b = None
        if s.isid('else'):
            s.next(); b = s.stmt()
        return ('if', c, a, b)

    def st_while(s):
        c = s.cond()
        return ('while', c, s.stmt())

    def st_do(s):
        body = s.stmt()
        if not s.isid('while'): s.err('expected while')
        s.next()
        c = s.cond()
        s.expect(';')
        return ('do', body, c)

    def st_for(s):
        s.expect('(')
        init = None if s.isop(';') else s.expr_list(';')
        s.expect(';')
        c = None if s.isop(';') else s.expr()
        s.expect(';')
        step = None if s.isop(')') else s.expr_list(')')
        s.expect(')')
        return ('for', init, c, step, s.stmt())

    def expr_list(s, end):
        es = [s.expr()]
        while s.accept(','):
            es.append(s.expr())
        return es

    def st_break(s):
        s.expect(';'); return ('break',)

    def st_return(s):
        e = None if s.isop(';') else s.expr()
        s.expect(';')
        return ('return', e)

    def st_goto(s):
        l = s.ident(); s.expect(';')
        return ('goto', l)

    def st_lock(s):
        return s.stmt()

    def st_no_warn(s):
        while not s.accept(';'):
            s.next()
        return ('nop',)

    def st_try(s):
        if s.fn: s.fn.has_try = True
        body = s.stmt()
        if not s.isid('catch'): s.err('expected catch')
        s.next()
        return ('try', body, s.stmt())

    def st_asm(s):
        depth = 0
        s.expect('{')
        depth = 1
        while depth:
            t = s.next()
            if t.k == 'eof': s.err('eof in asm')
            if t.k == 'op' and t.v == '{': depth += 1
            if t.k == 'op' and t.v == '}': depth -= 1
        if s.fn:
            s.fn.sym.native = True
        return ('asm',)

    def st_switch(s):
        nobound = False
        if s.accept('['):
            e = s.expr(); s.expect(']')
        else:
            e = s.cond()
        s.expect('{')
        items = []
        while not s.accept('}'):
            if s.isid('case'):
                s.next()
                if s.accept(':'):
                    items.append(('case', None, None))
                    continue
                a = s.expr()
                b = None
                if s.accept('...'):
                    b = s.expr()
                s.expect(':')
                items.append(('case', a, b))
            elif s.isid('default') and s.isop(':', 1):
                s.next(); s.next()
                items.append(('default',))
            elif (s.isid('start') or s.isid('end')) and s.isop(':', 1):
                items.append((s.next().v,))
                s.next()
            else:
                items.append(s.stmt())
        return ('switch', e, items)

    # ------------------------------------------------------------ declarations
    def modifiers(s):
        mods = set()
        while True:
            t = s.peek()
            if t.k != 'id': break
            v = t.v
            if v in ('public', 'extern', 'static', 'interrupt', 'haserrcode', 'argpop', 'noargpop',
                     'no_warn', 'import', 'noreg'):
                s.next(); mods.add(v)
            elif v in ('_extern', '_intern', '_import'):
                s.next(); s.next(); mods.add('_extern')
            elif v == 'reg':
                s.next(); mods.add('reg')
                if s.peek().k == 'id' and s.peek().v in REGS:
                    s.next()
            else:
                break
        return mods

    def local_decl(s):
        mods = s.modifiers()
        if s.isid('class') or s.isid('union'):
            kw = s.ident()
            cd = s.class_def(kw == 'union')
            if s.accept(';'):
                return ('nop',)
            bt = Ty('cls', cdef=cd)
        elif s.isid('class', 1) or s.isid('union', 1):
            ib = s.ident()
            kw = s.ident()
            cd = s.class_def(kw == 'union', ibase=ib)
            if s.accept(';'):
                return ('nop',)
            bt = Ty('cls', cdef=cd)
        else:
            bt = s.base_type()
        out = []
        while True:
            name, ty = s.declarator(bt)
            sym = Sym('lvar', name, ty)
            sym.static = 'static' in mods
            init = None
            if s.accept('='):
                init = s.initializer(ty)
            if ty.k == 'arr' and ty.n is None and init is not None and init[0] == 'list':
                ty.n = len(init[1])
            elif ty.k == 'arr' and ty.n is None and init is not None and init[1].k == 'str':
                ty.n = len(init[1].v) + 1
            s.fn.locals.append(sym)
            s.scope[name] = sym
            out.append(('decl', sym, init))
            if s.accept(','):
                continue
            s.expect(';')
            break
        return out[0] if len(out) == 1 else ('block', out, 'decls')

    def initializer(s, ty=None):
        sub = ty.base if ty is not None and ty.k == 'arr' else None
        if s.accept('{'):
            items = []
            while not s.accept('}'):
                items.append(s.initializer(sub))
                if not s.accept(','):
                    s.expect('}')
                    break
            return ('list', items)
        if ty is not None and ty.k == 'arr' and ty.n and not s.isop('"') and s.peek().k != 'str':
            # HolyC (PrsVarInit2): '{' is optional for fixed-size arrays, '}' then closes it
            items = [s.initializer(sub)]
            while len(items) < ty.n and s.accept(','):
                items.append(s.initializer(sub))
            s.accept('}')
            return ('list', items)
        return ('expr', s.assign_expr())

    def top(s, unit, skip_bodies=False):
        """Parse a whole translation unit; top-level statements go to prog.init."""
        s.unit = unit
        s.skip_bodies = skip_bodies
        while s.peek().k != 'eof':
            s.top_item()
        s.buf.pop(0)   # consume eof so another unit can be pushed and parsed after this one

    def top_item(s):
        t = s.peek()
        if t.k == 'op' and t.v == ';':
            s.next(); return
        if t.k == 'id' and t.v == 'asm' and s.isop('{', 1):
            s.next(); s.st_asm(); return
        save = s.peek()
        mods = s.modifiers()
        t = s.peek()
        if t.k == 'id' and (t.v in ('class', 'union') or
                            ((s.isid('class', 1) or s.isid('union', 1)) and s.is_type_start())):
            ib = None
            if t.v not in ('class', 'union'):
                ib = s.ident()
            kw = s.ident()
            if ib and ib.endswith('i') and ib[:-1] in PRIMS:
                # intrinsic union e.g. `I64i union I64 {...}` : builtin, skip
                s.ident()
                s.skip_braces()
                s.expect(';')
                return
            cd = s.class_def(kw == 'union', ibase=ib)
            if s.accept(';'):
                return
            bt = Ty('cls', cdef=cd)
            s.global_decls(bt, mods)
            return
        if not s.is_type_start():
            if mods:
                s.err('expected declaration')
            s.top_stmt()
            return
        bt = s.base_type()
        s.global_decls(bt, mods)

    def skip_braces(s):
        s.expect('{')
        d = 1
        while d:
            t = s.next()
            if t.k == 'op' and t.v == '{': d += 1
            elif t.k == 'op' and t.v == '}': d -= 1

    def top_stmt(s):
        fs = s.prog.init_fn(s.unit)
        s.fn, s.scope = fs, fs.scope
        native = s.peek().file.startswith(s.native_files) if s.native_files else False
        st = s.stmt()
        if not s.skip_bodies and not native:
            fs.body.append(st)
        s.fn, s.scope = None, None
        s.registry_hook(st)

    def registry_hook(s, st):
        """RegDft(path,"decls"); RegExe(path); compiles decls at compile time in TempleOS.
        Here the decls become ordinary globals; prog.reg[path] lists them for the runtime's save/load."""
        if st[0] != 'expr' or st[1].k != 'call' or st[1].a[0].k != 'fref':
            return
        name, args = st[1].a[0].v.name, st[1].a[1:]
        if not args or args[0].k != 'str':
            return
        path = args[0].v
        if name == 'RegDft' and args[1].k == 'str':
            s.prog.reg_text[path] = args[1].v
        elif name == 'RegExe' and path in s.prog.reg_text:
            assert not s.buf, 'lookahead past RegExe'
            n0 = len(s.prog.gvars)
            fname = 'reg:' + path.decode('latin-1')
            s.pp.push_text(s.prog.reg_text[path].decode('latin-1'), fname)
            while s.peek().file == fname:
                s.top_item()
            s.prog.reg[path] = s.prog.gvars[n0:]

    def global_decls(s, bt, mods):
        first = True
        while True:
            name, ty = s.declarator(bt)
            if s.isop('(') and ty.k != 'func':
                # function definition or prototype
                params, var = s.param_list()
                if ty.k == 'cls' and not ty.cdef.ibase:
                    ty = ptr(ty)   # HolyC returns only RAX: "MySpring F()" hands back the pointer it returns
                fty = Ty('func', ret=ty, params=params, var=var)
                s.function(name, fty, mods)
                return
            s.global_var(name, ty, mods)
            if s.accept(','):
                continue
            s.expect(';')
            return

    def global_var(s, name, ty, mods):
        dyn, s.dyn_dim = s.dyn_dim, None
        native = bool(s.native_files) and s.peek().file.startswith(s.native_files)
        old = s.prog.globals.get(name)
        if old is not None and old.kind == 'var' and same(old.ty, ty) and \
                (not old.defined or 'extern' in mods or '_extern' in mods):
            sym = old
            if ty.k == 'arr' and ty.n is not None: sym.ty = ty
        else:
            cname = 'h_' + name if old is None or old.unit == s.unit else 'h_%s__%d' % (name, Sym._n)
            sym = Sym('var', name, ty, cname)
            s.prog.globals[name] = sym
            s.prog.gvars.append(sym)
        sym.dyn = dyn
        init = None
        if s.accept('='):
            fs = s.prog.init_fn(s.unit)
            s.fn, s.scope = fs, fs.scope
            init = s.initializer(ty)
            s.fn, s.scope = None, None
            if init[0] == 'expr':
                sym.cexpr = init[1]
            if ty.k == 'arr' and ty.n is None and init[0] == 'list':
                ty.n = len(init[1])
            elif ty.k == 'arr' and ty.n is None and init[0] == 'expr' and init[1].k == 'str':
                ty.n = len(init[1].v) + 1
        if 'extern' in mods or '_extern' in mods or 'import' in mods:
            sym.extern = True
            if '_extern' in mods:
                sym.native = True
            return
        if sym.defined and sym.unit == s.unit:
            return
        sym.defined = True
        sym.unit = s.unit
        sym.extern = False
        if not s.skip_bodies:
            s.prog.gdefs.append((sym, init, s.unit))
            if (init is not None or dyn is not None) and not native:
                # run-time part of the initializer, ordered with the unit's top-level statements
                s.prog.init_fn(s.unit).body.append(('ginit', sym, init))

    def function(s, name, fty, mods):
        old = s.prog.globals.get(name)
        body_follows = s.isop('{')
        if (old is not None and old.kind == 'func' and old.defined and body_follows and
                s.replace_file and s.peek().file == s.replace_file):
            s.prog.fdefs = [f for f in s.prog.fdefs if f.sym is not old]
            old.defined = False
        if old is not None and old.kind == 'func' and (not old.defined or not body_follows):
            sym = old
            # merge default args from later declaration
            if len(old.ty.params) == len(fty.params):
                for p, q in zip(old.ty.params, fty.params):
                    if p.dflt is None and q.dflt is not None:
                        p.dflt = q.dflt
                    if q.name and body_follows:
                        p.name = q.name
            elif body_follows or not old.defined:   # a later bare prototype keeps the defined signature
                sym.ty = fty
        else:
            cname = 'h_' + name
            if old is not None:
                cname = 'h_%s__%d' % (name, Sym._n)
            sym = Sym('func', name, fty, cname)
            s.prog.globals[name] = sym
            s.prog.funcs.append(sym)
        if '_extern' in mods:
            sym.native = True
        if not body_follows:
            s.accept(';')   # KernelC.HH omits it after B(); HolyC tolerates that
            return
        sym.defined = True
        sym.unit = s.unit
        if s.native_files and s.peek().file.startswith(s.native_files):
            sym.native = True
        if s.skip_bodies or sym.native and s.unit == 'lib':
            s.skip_braces()
            return
        fn = Func(sym)
        fn.ty = sym.ty
        s.fn, s.scope = fn, {}
        fn.params = []
        for p in sym.ty.params:
            ps = Sym('param', p.name, p.ty)
            fn.params.append(ps)
            if p.name:
                s.scope[p.name] = ps
        if sym.ty.var:
            for nm, ty in (('argc', I64), ('argv', ptr(I64))):
                ps = Sym('param', nm, ty)
                s.scope[nm] = ps
                fn.params.append(ps)
        # buffer the whole body so a library body that fails can be dropped cleanly
        k, d = 1, 1
        while d:
            t = s.peek(k); k += 1
            if t.k == 'eof': break
            if t.k == 'op' and t.v == '{': d += 1
            elif t.k == 'op' and t.v == '}': d -= 1
        close = s.buf[k - 1]
        try:
            fn.body = s.block()
        except CompileError as e:
            if s.unit != 'lib' or not any(t is close for t in s.buf):
                raise
            while s.buf.pop(0) is not close:   # drop the rest of the failed body
                pass
            s.prog.broken[name] = str(e)
            sym.native = True
            return
        finally:
            s.fn, s.scope = None, None
        if not sym.native:
            s.prog.fdefs.append(fn)
        sym.func = fn

class Program:
    def __init__(s):
        s.globals = {}
        s.classes = {}
        s.class_order = []
        s.funcs = []
        s.gvars = []
        s.gdefs = []        # (sym, init, unit)
        s.fdefs = []        # Func
        s.inits = {}        # unit -> Func for top-level statements
        s.init_order = []
        s.anon = 0
        s.bin_used = set()
        s.refs = set()
        s.reg_text, s.reg = {}, {}   # registry path -> decl text / [global Sym]
        s.broken = {}       # lib func name -> parse error (hardware/boot code): emitted as an aborting stub

    def init_fn(s, unit):
        f = s.inits.get(unit)
        if f is None:
            # port glue files (tools/ports/...) come as absolute paths; name them by tools/-relative path
            name = os.path.relpath(unit, os.path.dirname(os.path.abspath(__file__))) if os.path.isabs(unit) else unit
            sym = Sym('func', '__init', Ty('func', ret=U0, params=[], var=False),
                      '__init_' + ''.join(c if c.isalnum() else '_' for c in name))
            f = Func(sym)
            f.body = []
            f.scope = {}
            f.params = []
            f.ty = sym.ty
            s.inits[unit] = f
            s.init_order.append(unit)
        return f

# ---------------------------------------------------------------- layout (packed, like HolyC)
def sizeof_t(t):
    if t.k == 'prim':
        return 8 if t.name == 'F64' else 0 if t.name == 'U0' else INTS[t.name][0]
    if t.k == 'ptr' or t.k == 'func':
        return 8
    if t.k == 'arr':
        return (t.n or 0) * sizeof_t(t.base)
    cd = t.cdef
    if cd.ibase:
        return 8 if cd.ibase == 'F64' else INTS[cd.ibase][0]
    return items_size(cd.items, cd.is_union)

def items_size(items, is_union):
    sizes = [sizeof_t(it[2]) if it[0] == 'm' else items_size(it[1], it[0] == 'u') for it in items]
    return (max(sizes) if sizes else 0) if is_union else sum(sizes)

def member_offset(cd, name):
    """Byte offset of member `name` in class cd (None if absent)."""
    def walk(items, base, is_union):
        off = base
        for it in items:
            if it[0] == 'm':
                if it[1] == name:
                    return off, True
                sz = sizeof_t(it[2])
            else:
                r, found = walk(it[1], off, it[0] == 'u')
                if found:
                    return r, True
                sz = items_size(it[1], it[0] == 'u')
            if not is_union:
                off += sz
        return off, False
    r, found = walk(cd.items, 0, cd.is_union)
    return r if found else None

# ---------------------------------------------------------------- const eval
def const_val(e, follow=False):
    """Constant value of e or None. follow=True also folds globals with a known value expr and
    pure math calls (array dims: ToTheFront terrain[map_rows][map_cols])."""
    k = e.k
    if k in ('int', 'flt'):
        return e.v
    if follow and k == 'var' and e.v.cexpr is not None:
        x = const_val(e.v.cexpr, True)
        return None if x is None else int(x) if is_i(e.t) else float(x)   # F64->I64 truncates (FISTTP)
    if follow and k == 'call' and e.a[0].k == 'fref':
        f = {'Cos': math.cos, 'Sin': math.sin, 'Sqrt': math.sqrt, 'ToI64': int}.get(e.a[0].v.name)
        xs = [const_val(a, True) for a in e.a[1:]]
        return None if f is None or None in xs else f(*xs)
    if k == 'sizeof':
        return sizeof_t(e.v)
    if k == 'sizeofm':
        cd, m = e.v
        return sizeof_t(cd.lookup[m])
    if k == 'offset':
        cd, path = e.v
        off = 0
        for m in path:
            off += member_offset(cd, m)
            t = cd.lookup[m]
            cd = t.cdef if t.k == 'cls' else None
        return off
    if k == 'un':
        x = const_val(e.a[0], follow)
        if x is None: return None
        if e.v == '-': return -x
        if e.v == '~': return ~int(x)
        if e.v == '!': return int(not x)
    if k == 'cast' and e.v == 'conv':
        x = const_val(e.a[0], follow)
        if x is None or not is_i(e.t) or isinstance(x, float): return None if x is None or isinstance(x, float) else x
        sz, sg = INTS[int_base(e.t)]
        x &= (1 << (8 * sz)) - 1
        if sg and x >= 1 << (8 * sz - 1): x -= 1 << (8 * sz)
        return x
    if k == 'bin':
        a, b = const_val(e.a[0], follow), const_val(e.a[1], follow)
        if a is None or b is None: return None
        op = e.v
        try:
            if isinstance(a, float) or isinstance(b, float):
                return {'+': a + b, '-': a - b, '*': a * b, '/': a / b if b else None}.get(op)
            f = {'+': lambda: a + b, '-': lambda: a - b, '*': lambda: a * b, '&': lambda: a & b,
                 '|': lambda: a | b, '^': lambda: a ^ b,
                 '<<': lambda: a << b if 0 <= b < 64 else 0, '>>': lambda: a >> b if 0 <= b < 64 else 0,
                 '==': lambda: int(a == b), '!=': lambda: int(a != b), '<': lambda: int(a < b),
                 '>': lambda: int(a > b), '<=': lambda: int(a <= b), '>=': lambda: int(a >= b),
                 '&&': lambda: int(bool(a and b)), '||': lambda: int(bool(a or b)),
                 '`': lambda: pow(a, b, 1 << 64) if b >= 0 else 0}.get(op)   # lazily: only the op asked for
            r = f() if f else None
            if op == '/' and b: r = int(a / b)
            if op == '%' and b: r = a - b * int(a / b)
            if r is not None and isinstance(r, int):
                r &= (1 << 64) - 1
                if r >= 1 << 63 and e.t.name != 'U64': r -= 1 << 64
            return r
        except Exception:
            return None
    return None
