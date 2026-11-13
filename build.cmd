@echo off
rem build.cmd <Game> [compile-only files...]   -- builds out\<Game>.exe from gen\ + runtime\ with MSVC
setlocal
set "PATH=%PATH%;C:\Program Files (x86)\Microsoft Visual Studio\Installer"
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0"
if not exist obj mkdir obj
if not exist out mkdir out
if not defined TOS_OPT set TOS_OPT=/O2
set CF=/nologo /std:c11 %TOS_OPT% /Zi /MT /W1 /utf-8 /Iruntime /Igen /Foobj\ /Fdobj\ /c
if "%~1"=="lib" (cl %CF% gen\tos_lib.c & exit /b %errorlevel%)
if not exist obj\tos_lib.obj cl %CF% gen\tos_lib.c || exit /b 1
cl %CF% gen\%1.c runtime\*.c || exit /b 1
rem the game's TempleOS icon (tools\icons.ps1 makes out\icons\<Game>.ico)
set RES=
if exist out\icons\%1.ico (
  echo 1 ICON "out/icons/%1.ico"> obj\%1.rc
  rc /nologo /fo obj\%1.res obj\%1.rc || exit /b 1
  set RES=obj\%1.res
)
link /nologo /DEBUG /INCREMENTAL:NO /STACK:16777216 /LARGEADDRESSAWARE:NO /DYNAMICBASE:NO /BASE:0x10000000 /OUT:out\%1.exe obj\tos_lib.obj obj\%1.obj obj\rt_*.obj %RES% user32.lib gdi32.lib winmm.lib dbghelp.lib || exit /b 1
