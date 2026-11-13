"""Shared loader: parses the TempleOS library (Kernel + Adam) and one game into a Program."""
import os, re, glob
from hc_lex import Source, Preproc
from hc_parse import Parser, Sym, N

HERE = os.path.dirname(os.path.abspath(__file__))

# Kernel.PRJ order (boot files KStart16/32/64 and KMain left out), then StartOS.HC's order for Adam.
LIB = ['Kernel/KernelA.HH', 'Kernel/KMathA', 'Kernel/KUtils', 'Compiler/CompilerA.HH', 'Compiler/CompilerB.HH',
       'Kernel/KernelB.HH', 'Kernel/KExts', 'Kernel/StrA', 'Kernel/KGlbls', 'Kernel/KMathB', 'Kernel/Sched',
       'Kernel/Mem/MakeMem', 'Kernel/FontStd', 'Kernel/FontCyrillic', 'Kernel/StrB', 'Kernel/KHashA', 'Kernel/KInts',
       'Kernel/KDataTypes', 'Kernel/Compress', 'Kernel/KHashB', 'Kernel/KLoad', 'Kernel/KDate', 'Kernel/StrPrint',
       'Kernel/StrScan', 'Kernel/KDefine', 'Kernel/Display', 'Kernel/KMisc', 'Kernel/KDbg', 'Kernel/KeyDev',
       'Kernel/KExcept', 'TosKbdTables', 'Kernel/SerialDev/MakeSerialDev', 'Kernel/QSort', 'Kernel/KTask', 'Kernel/Job',
       'Kernel/PCIBIOS', 'Kernel/MultiProc', 'Kernel/EdLite', 'Kernel/BlkDev/MakeBlkDev', 'Kernel/FunSeg',
       'Kernel/KernelC.HH',
       'Compiler/CExts', 'Compiler/CInit', 'Compiler/CExcept', 'Compiler/LexLib', 'Compiler/Lex', 'TosLexExpr',   # the lexer: DolDoc parses with it
       'TosBoot', 'Adam/MakeAdam', 'Adam/Opt/Utils/Find', 'TosStart']
# TosBoot/TosStart live in tools/: the parts of KMain and StartOS.HC that apply without real hardware.

# Hardware, memory, interrupts, disk, loader, debugger: declarations only.
# (Functions with inline asm elsewhere are made native by the parser automatically.)
EMBED_FILES = ['Doc/EdPullDown.DD', 'Compiler/OpCodes.DD', 'Adam/God/Vocab.DD']   # read at boot; served by the runtime's FileRead
NATIVE_FILES = ('Kernel/KMathA', 'Kernel/KUtils', 'Kernel/Mem/', 'Kernel/KInts', 'Kernel/KLoad',
                'Kernel/KDbg', 'Kernel/PCIBIOS',
                'Kernel/EdLite', 'Kernel/BlkDev/', 'Kernel/FunSeg')

RT_DIR = os.path.join(HERE, '..', 'runtime')

def runtime_natives(rt_dir=RT_DIR):
    """Names the C runtime defines itself (`... h_Name(` or a global `... h_Name;`/`=`/`[`):
    they override any translated body or definition."""
    names = set()
    for f in glob.glob(os.path.join(rt_dir, '*.c')):
        txt = open(f).read()
        names |= set(re.findall(r'^\w[\w \*]*?\bh_(\w+)\(', txt, re.M))
        names |= set(re.findall(r'^(?!static|extern|typedef)\w[\w \*]*?\bh_(\w+)\s*(?:=|;|\[)', txt, re.M))
    return names

def game_name(rel):
    parts = rel.replace('\\', '/').split('/')
    return parts[-2] if parts[-1].lower().startswith('run.') else os.path.splitext(parts[-1])[0]

def load(code_root, bin_root, game=None, bodies=True):
    pp = Preproc(Source(code_root, bin_root))
    pp.pinned = {'SYS_FIXED_AREA'}   # prelude.HC moves it off 0x100000, which Windows maps at start-up
    pp.push_text(''.join('#include "/%s"\n' % f for f in LIB), 'lib.HC')
    pp.push_text(open(os.path.join(HERE, 'prelude.HC')).read(), 'prelude')
    Sym._n = 0   # suffixed C names must match between the lib-only parse and each game parse
    p = Parser(pp)
    p.native_files = NATIVE_FILES
    p.top('lib', skip_bodies=not bodies)
    pr = p.prog
    p.mark = dict(classes=len(pr.class_order), funcs=len(pr.funcs), gvars=len(pr.gvars),
                  fdefs=len(pr.fdefs), bins=set(pr.bin_used))
    if game:
        pp.push_file(game)
        p.top(game)
        port = os.path.join(HERE, 'ports', game_name(game) + '.HC')
        if os.path.exists(port):   # hand-written stand-ins for what the game compiles at run time
            p.replace_file = port
            pp.push_file(port)
            p.top(port)
            pr.init_fn(port)
    p.rt = runtime_natives()
    for name in p.rt:
        sym = pr.globals.get(name)
        if sym is not None and sym.kind == 'func':
            sym.native = True
    pr.fdefs = [f for f in pr.fdefs if not f.sym.native]
    return p

def syms(x, out):
    """Collect every Sym referenced from an AST fragment."""
    if isinstance(x, N):
        if x.k in ('var', 'fref'): out.add(x.v)
        for a in x.a: syms(a, out)
        if isinstance(x.v, (list, tuple)): syms(x.v, out)
    elif isinstance(x, (list, tuple)):
        for a in x: syms(a, out)
    elif isinstance(x, Sym): out.add(x)
