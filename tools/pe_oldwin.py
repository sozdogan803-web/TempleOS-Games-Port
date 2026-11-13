"""pe_oldwin.py <exe>  -- make a 32-bit build loadable on Windows 95 / NT 4.0 and check its imports.

Today's linker writes 6.0 (Vista) as the required Windows version; older loaders refuse such
an exe, so the header's OS and subsystem versions are set to 4.0.  Every imported function
must be on OLD below (all present in Windows 95 and NT 4.0); anything else fails the build,
because a missing import stops the exe from loading at all.  Newer functions are looked up
with GetProcAddress at run time instead (see runtime/rt_win32.c)."""
import struct, sys

OLD = {
    'KERNEL32.DLL': '''ExitProcess GetModuleHandleA GetModuleFileNameA GetProcAddress LoadLibraryA GetStdHandle
        WriteFile CreateFileA CloseHandle GetEnvironmentVariableA HeapAlloc HeapFree HeapReAlloc GetProcessHeap
        VirtualAlloc VirtualFree VirtualProtect VirtualQuery Sleep QueryPerformanceCounter QueryPerformanceFrequency
        GetLocalTime SetUnhandledExceptionFilter CreateThread CreateEventA WaitForSingleObject GetLastError
        GetCurrentProcess InterlockedExchange''',
    'USER32.DLL': '''RegisterClassA CreateWindowExA DefWindowProcA PeekMessageA DispatchMessageA GetDC ReleaseDC
        BeginPaint EndPaint GetClientRect AdjustWindowRect SystemParametersInfoA LoadIconA SetCursor GetKeyState
        SetCapture ReleaseCapture GetSystemMetrics SetRect ShowWindow''',
    'GDI32.DLL': 'StretchDIBits PatBlt SetStretchBltMode',
    'WINMM.DLL': 'waveOutOpen waveOutPrepareHeader waveOutWrite timeBeginPeriod',
}
OLD = {dll: set(names.split()) for dll, names in OLD.items()}

def main(path):
    b = bytearray(open(path, 'rb').read())
    pe = struct.unpack_from('<I', b, 0x3C)[0]
    assert b[pe:pe + 4] == b'PE\0\0'
    nsec, opt_size = struct.unpack_from('<H', b, pe + 6)[0], struct.unpack_from('<H', b, pe + 20)[0]
    opt = pe + 24
    assert struct.unpack_from('<H', b, opt)[0] == 0x10B, 'not a 32-bit exe'
    struct.pack_into('<HH', b, opt + 40, 4, 0)   # MajorOperatingSystemVersion.Minor
    struct.pack_into('<HH', b, opt + 48, 4, 0)   # MajorSubsystemVersion.Minor
    secs = [struct.unpack_from('<8sIIII', b, opt + opt_size + 40 * i) for i in range(nsec)]

    def off(rva):
        for _, vsize, va, rawsize, raw in secs:
            if va <= rva < va + max(vsize, rawsize):
                return rva - va + raw
        raise ValueError('rva %#x' % rva)

    def cstr(o):
        return bytes(b[o:b.index(0, o)]).decode('ascii')

    bad = []
    imp = struct.unpack_from('<I', b, opt + 96 + 8)[0]
    d = off(imp)
    while True:
        oft, _, _, name, ft = struct.unpack_from('<IIIII', b, d)
        if not name:
            break
        dll = cstr(off(name)).upper()
        t = off(oft or ft)
        while True:
            e = struct.unpack_from('<I', b, t)[0]
            if not e:
                break
            fn = '#%d' % (e & 0xFFFF) if e & 0x80000000 else cstr(off(e) + 2)
            if fn not in OLD.get(dll, ()):
                bad.append('%s!%s' % (dll, fn))
            t += 4
        d += 20
    if bad:
        sys.exit('%s imports functions older Windows may lack: %s' % (path, ' '.join(bad)))
    open(path, 'wb').write(b)

if __name__ == '__main__':
    main(sys.argv[1])
