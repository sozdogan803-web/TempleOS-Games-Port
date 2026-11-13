# Check runtime\rt_libc.c's 32-bit helpers against the 64-bit hardware: build x86helpers.c both ways, compare.
$root = Split-Path (Split-Path $PSScriptRoot)
Set-Location $root
$VCB = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build"
$env:PATH += ";C:\Program Files (x86)\Microsoft Visual Studio\Installer"   # vcvars needs vswhere
$t = "obj\x86helpers"
New-Item -ItemType Directory -Force $t | Out-Null
cmd /c "call `"$VCB\vcvars64.bat`" >nul && cl /nologo /O2 /MT /Fo$t\ /Fe$t\h64.exe tools\test\x86helpers.c" | Select-String "error"
cmd /c "call `"$VCB\vcvarsamd64_x86.bat`" >nul && cl /nologo /O2 /Oy- /arch:IA32 /GS- /Zl /DTOS_NOCRT /D_NO_CRT_STDIO_INLINE /Fo$t\ /Fe$t\h32.exe tools\test\x86helpers.c runtime\rt_libc.c /link /NODEFAULTLIB /ENTRY:rt_entry /SAFESEH:NO kernel32.lib" | Select-String "error|unresolved"
& "$t\h64.exe" > "$t\h64.txt"
& "$t\h32.exe" > "$t\h32.txt"
python -I tools\test\x86helpers_cmp.py "$t\h64.txt" "$t\h32.txt"
