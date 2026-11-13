"""sym.py <exe> <hexaddr...>: symbol + offset for addresses in an exe linked at its preferred base."""
import ctypes, sys
from ctypes import wintypes

dbg = ctypes.WinDLL('dbghelp')
k32 = ctypes.WinDLL('kernel32')

class SYMBOL_INFO(ctypes.Structure):
    _fields_ = [('SizeOfStruct', wintypes.ULONG), ('TypeIndex', wintypes.ULONG), ('Reserved', ctypes.c_uint64 * 2),
                ('Index', wintypes.ULONG), ('Size', wintypes.ULONG), ('ModBase', ctypes.c_uint64),
                ('Flags', wintypes.ULONG), ('Value', ctypes.c_uint64), ('Address', ctypes.c_uint64),
                ('Register', wintypes.ULONG), ('Scope', wintypes.ULONG), ('Tag', wintypes.ULONG),
                ('NameLen', wintypes.ULONG), ('MaxNameLen', wintypes.ULONG), ('Name', ctypes.c_char * 512)]

h = ctypes.c_void_p(1234)
dbg.SymInitialize.argtypes = [ctypes.c_void_p, ctypes.c_char_p, wintypes.BOOL]
dbg.SymLoadModuleEx.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint64,
                                wintypes.DWORD, ctypes.c_void_p, wintypes.DWORD]
dbg.SymLoadModuleEx.restype = ctypes.c_uint64
dbg.SymFromAddr.argtypes = [ctypes.c_void_p, ctypes.c_uint64, ctypes.POINTER(ctypes.c_uint64), ctypes.POINTER(SYMBOL_INFO)]
dbg.SymInitialize(h, None, False)
base = dbg.SymLoadModuleEx(h, None, sys.argv[1].encode(), None, 0x10000000, 0, None, 0)
print('module base', hex(base))
for a in sys.argv[2:]:
    si = SYMBOL_INFO(); si.SizeOfStruct = 88; si.MaxNameLen = 500
    disp = ctypes.c_uint64()
    if dbg.SymFromAddr(h, int(a, 16), ctypes.byref(disp), ctypes.byref(si)):
        print(a, si.Name.decode(), '+%d' % disp.value, 'size', si.Size)
    else:
        print(a, 'no symbol', ctypes.GetLastError())
