@echo off
rem build.cmd <Game> [compile-only files...]   -- builds out\<Game>.exe from gen\ + runtime\ with MSVC
rem set TOS_ARCH=x86 first for the 32-bit build (out\x86\<Game>.exe), which runs on Windows 95 and later:
rem   no Microsoft C runtime (runtime\rt_libc.c instead), x87 floating point, Windows 4.0 in the exe header
setlocal
set "PATH=%PATH%;C:\Program Files (x86)\Microsoft Visual Studio\Installer"
set "VCB=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build"
cd /d "%~dp0"
if /i "%TOS_ARCH%"=="x86" (
  call "%VCB%\vcvarsamd64_x86.bat" >nul || exit /b 1
  set O=obj\x86
  set OUT=out\x86
  set ACF=/Oy- /arch:IA32 /GS- /Zl /DTOS_NOCRT /D_NO_CRT_STDIO_INLINE
  set LF=/STACK:16777216 /DYNAMICBASE:NO /NODEFAULTLIB /ENTRY:rt_entry /SAFESEH:NO /SUBSYSTEM:CONSOLE kernel32.lib
) else (
  call "%VCB%\vcvars64.bat" >nul || exit /b 1
  set O=obj
  set OUT=out
  set ACF=
  set LF=/STACK:16777216 /LARGEADDRESSAWARE:NO /DYNAMICBASE:NO /BASE:0x10000000
)
if not exist %O% mkdir %O%
if not exist %OUT% mkdir %OUT%
if not defined TOS_OPT set TOS_OPT=/O2
set CF=/nologo /std:c11 %TOS_OPT% %ACF% /Zi /MT /W1 /utf-8 /Iruntime /Igen /Fo%O%\ /Fd%O%\ /c
if "%~1"=="lib" (cl %CF% gen\tos_lib.c & exit /b %errorlevel%)
if not exist %O%\tos_lib.obj cl %CF% gen\tos_lib.c || exit /b 1
cl %CF% gen\%1.c runtime\*.c || exit /b 1
rem the game's TempleOS icon (tools\icons.ps1 makes out\icons\<Game>.ico); 16 colours for x86, as
rem Windows before Vista can't read PNG icons
set RES=
set ICO=out\icons\%1.ico
if /i "%TOS_ARCH%"=="x86" if exist %ICO% (
  python -I tools\mkicons.py classic %ICO% %O%\%1.ico || exit /b 1
  set ICO=%O%\%1.ico
)
if exist %ICO% (
  echo 1 ICON "%ICO:\=/%"> %O%\%1.rc
  rc /nologo /fo %O%\%1.res %O%\%1.rc || exit /b 1
  set RES=%O%\%1.res
)
link /nologo /DEBUG /INCREMENTAL:NO %LF% /OUT:%OUT%\%1.exe %O%\tos_lib.obj %O%\%1.obj %O%\rt_*.obj %RES% user32.lib gdi32.lib winmm.lib || exit /b 1
if /i "%TOS_ARCH%"=="x86" python -I tools\pe_oldwin.py %OUT%\%1.exe || exit /b 1
