# TempleOS games on Windows

The 37 games that ship with TempleOS 5.03 (Terry A. Davis, public domain), translated from
HolyC to C and built as ordinary Windows programs. Each game is one `.exe` in `out/` with
its TempleOS icon, and needs nothing else installed. `out/x86/` has 32-bit builds of the same
games for old Windows, down to Windows 95 and NT 4.0 (see [Old Windows](#old-windows)).

The games run on the TempleOS library itself: the window manager, DolDoc, the graphics
library, menus and pop-ups are TempleOS's own code, translated. A small C runtime replaces
only what was hardware or assembly language (memory, task switching, keyboard, mouse,
screen, PC speaker, clock).

## System requirements

| | Minimum | Most it will use |
|---|---|---|
| Windows | Windows 95 or NT 4.0 (the 32-bit games in `out/x86/`) | Windows 11. Both builds were tested there; `out/` is the 64-bit one |
| macOS | Mac OS X 10.6 Snow Leopard on an Intel Mac | Apple Silicon Macs as well as Intel |
| Processor | 486DX, or any x86 with floating point | One core: the games run TempleOS's tasks on a single core, so more cores don't help |
| Memory | About 20 MB per game (Talons about 100 MB) | About 100 MB (Talons) |
| Screen | 640x480: the window opens maximized and the picture shrinks a little | Any size: the window grows in whole steps (3x on a 4K screen) and can be maximized |
| Disk | About 1.3 MB per game (32-bit), 2 MB (64-bit) | The same |
| Sound | Optional: TempleOS's PC-speaker beeps play through any sound card | The same |

### Tested on

| | Windows PC | MacBook Pro 14-inch |
|---|---|---|
| Processor | AMD Ryzen 7 7800X3D | Apple M5 (10-core CPU) |
| Graphics | NVIDIA GeForce RTX 5070 Ti | Apple M5 (10-core GPU) |
| Memory | 32 GB | 16 GB |
| Storage | 2 TB SSD | 512 GB SSD |
| System | Windows 11 Pro | macOS 27.2 |

Both Windows builds, 64-bit and 32-bit, ran on the Windows PC. The minimum systems in the
table above (Windows 95 to XP, Mac OS X 10.6) are what the builds are made for; they have not
been run on real machines that old.

## Playing

Double-click `out/<Game>.exe` (on Windows XP or older, or any 32-bit Windows,
`out/x86/<Game>.exe`). Controls are the same as in TempleOS:

- the keys each game names in its help pop-up;
- the mouse;
- `Esc` (in some games `Shift+Esc`) to quit, or close the window.

The window opens at the largest whole-number size of 640x480 that fits your screen. You can
resize or maximize it; the picture keeps its 4:3 shape.

Games: BattleLines, BigGuns, BlackDiamond, BomberGolf, CastleFrankenstein, CharDemo,
CircleTrace, Collision, Digits, DunGen, ElephantWalk, FlapBat, FlatTops, Halogen, KeepAway,
MassSpring, Maze, RainDrops, RawHide, Rocket, RocketScience, Span, Squirt, Stadium, Strut,
Talons, TheDead, TicTacToe, Titanium, ToTheFront, TreeCheckers, Varoom, Wenceslas, Whap,
X-Caliber, Zing, ZoneOut.

## What is different from TempleOS

- **No disk.** Games read the few files they need from inside the `.exe`. Anything they write
  (high scores, `RegWrite` settings) is dropped, so scores reset each run.
- **One CPU core.** Varoom and Talons use TempleOS's multicore job queue; here it runs those
  jobs on the one core.
- **Icons.** 29 icons are the sprites TempleOS shows for the game in its menu, or the first
  sprite in the game's source. The other 8 games have no sprite in TempleOS, so their icon is
  a picture of the game's screen: CharDemo, CircleTrace, Halogen, MassSpring, Maze, RainDrops,
  TicTacToe and Whap. Titanium's menu icon is lost (see `tools/mkicons.py`), so it uses the
  first sprite in `Titanium.HC`.
- **Four fixes to the originals.** These are bugs or corrupt data that TempleOS tolerates (it
  lets a program read address 0 and memory past the end of a buffer) but Windows does not:
  - KeepAway: a NULL check is added where it compares against a team-mate who may not exist
    yet (the `FIXES` table in `tools/hc_lex.py`).
  - X-Caliber: `ShotNew` forgets to return the shot it made, which worked on TempleOS by
    luck; `return tmps;` is added (also in `FIXES`).
  - Titanium: in TinkerOS's `Titanium.HC`, sprite 9's end marker is a corrupt byte, so it
    is set back to 0 (`BIN_FIXES` in `tools/hc_lex.py`).
  - Stadium: the converted `tos2` copy of `StadiumBG.GR` is 20 bytes short, so data files
    come from TinkerOS's uncompressed `.Z` originals when there is one (`data_file` in
    `tools/hc_emit.py`).
- **macOS is not done.** The translated C is portable. Only `runtime/rt_win32.c` (window,
  input, task switching on fibers) and `runtime/rt_snd.c` (waveOut audio) are Windows-only,
  plus the `TOS_HEAPCHECK` allocator in `runtime/rt_core.c` (`VirtualAlloc`). A Mac version
  needs those rewritten with Cocoa, Core Audio and `mmap`, and then testing on a Mac.
  [HANDOFF-MAC.md](HANDOFF-MAC.md) is the step-by-step plan.

## Rebuilding

Needs:

- Visual Studio Build Tools with the C++ workload (the path is in `build.cmd`);
- Python 3;
- Git Bash, for `gen.sh`.

The TempleOS sources are in `tos_src/`:

- `tos2/`: the TempleOS 5.03 code;
- `m_tinkeros_TinkerOS/src/`: TinkerOS's copy, read only for the sprite data embedded in
  source files and for the original `.Z` files.

Commands, from this folder:

```bash
sh gen.sh Demo/Games/FlapBat.HC Apps/X-Caliber/Run.HC   # HolyC -> gen/*.c (library + those games)
```

```bat
build.cmd FlapBat   :: from Command Prompt or PowerShell (.\build.cmd); gen/ + runtime/ -> out/FlapBat.exe
```

```bash
powershell -File tools/icons.ps1 Demo/Games/FlapBat.HC   # -> out/icons/FlapBat.ico (build.cmd links it in)
```

`gen.sh` takes source paths; `build.cmd` takes the game name, which is the file name, or the
folder name for `Apps/<Game>/Run.HC`. Re-run `build.cmd` after `icons.ps1` so the icon is
linked in.

For the 32-bit build, set `TOS_ARCH=x86` first (`set TOS_ARCH=x86` in Command Prompt,
`$env:TOS_ARCH="x86"` in PowerShell); `build.cmd` then writes `out/x86/<Game>.exe`.

## Old Windows

The games in `out/x86/` are built to run on Windows 95, 98, ME, NT 4.0, 2000 and XP, as well
as every later Windows. Windows 3.1 cannot run them: Win32s has no threads or console.

- **No Microsoft C runtime.** Today's runtime needs Vista. `runtime/rt_libc.c` supplies the
  few C library functions the games use (strings, `printf`-style formatting, files on
  standard output, heap through `HeapAlloc`, x87 maths), plus the helpers the compiler calls
  for 64-bit integer arithmetic on a 32-bit CPU. `tools/test/x86helpers.ps1` checks these
  against the 64-bit build's results.
- **Only functions Windows 95 has.** `tools/pe_oldwin.py` runs after each link. It sets the
  Windows version the exe asks for to 4.0, and fails the build if the exe imports a function
  outside its list of Windows 95 / NT 4.0 functions. Newer functions are looked up when the
  game starts, and used only if present:
  - fibers (task switching): Windows 98 and NT 4.0 have them; on Windows 95 the runtime
    switches stacks itself (`ctx_switch` in `runtime/rt_win32.c`; `TOS_NOFIBERS=1` forces
    this on any Windows, for testing);
  - first-chance crash reports (`AddVectoredExceptionHandler`): XP and later. Before XP the
    report comes from the unhandled-exception filter, except on Windows 95 inside a game
    task, where Windows shows its own error box;
  - source lines in crash reports: `dbghelp.dll`, if it is there.
- **Any x86 PC that runs Windows 95** with a floating-point unit (486DX or later): the code
  uses x87 floating point and no MMX or SSE. A game uses about 20 MB of memory (Talons about
  100 MB). On a 640x480 screen the window opens maximized and the picture shrinks a little.
- **16-colour icons.** The PNG icons need Vista, so `build.cmd` converts each one to 16
  colours (`mkicons.py classic`). TempleOS draws in 16 colours, so nothing is lost.
- **HolyC pointers stay 8 bytes.** TempleOS's structures hold 8-byte pointers, and games
  depend on that layout. On 32-bit, the translator stores each pointer in an 8-byte slot
  (`TosPS` in `runtime/tos_rt.h`).
- **Calls through function pointers.** HolyC passes every argument as 8 bytes, so it freely
  hands a function taking `Obj *` to a pointer typed `I64 (*)(I64,I64)` (KeepAway's sort,
  ToTheFront's line-of-sight plots, task functions). On 32-bit, C passes pointers as 4
  bytes, so the translator puts a small adapter function between them (`fnref` in
  `tools/hc_emit.py`). For the same reason Spawn's `data` is an `I64` (a `FIXES` entry in
  `tools/hc_lex.py`): Strut and CastleFrankenstein pass an F64 through it.

Tested by running all 37 games on Windows 11 in 32-bit mode, with and without
`TOS_NOFIBERS=1`. They have not been run on a real Windows 95 or XP machine.

## How it works

- `tools/hc_lex.py`, `hc_parse.py`, `hc_emit.py`: HolyC lexer (including DolDoc and its
  embedded sprites), parser with type checking, and C emitter. `hc_build.py` lists the library
  files, in TempleOS's own boot order.
- `tools/TosBoot.HC`, `TosStart.HC`, `TosLexExpr.HC`, `prelude.HC`: HolyC stand-ins for the
  parts of boot and the compiler that only make sense on real hardware. This includes a small
  interpreter for the `Fun(args);` strings that menus and pop-ups run, and for the numbers
  typed into number prompts such as ToTheFront's game seed.
- `tools/ports/ToTheFront.HC`: ToTheFront compiles its AI players at run time; this file
  supplies them precompiled.
- `runtime/`: the C runtime. Any `h_Name(` defined there replaces the translated TempleOS
  function `Name`. Run `gen.sh` again after adding one.

Testing aids, all environment variables:

| Variable | Effect |
|---|---|
| `TOS_SHOT=file.bmp` | Save the screen after `TOS_SHOT_MS` ms (default 4000), then exit |
| `TOS_KEYS=1500:39,1600:B9` | Press keys: scan codes in hex, at those ms |
| `TOS_OPT=/Od` | Compiler flags for `build.cmd` (default `/O2`) |
| `TOS_HEAPCHECK=1` | Give every allocation guard pages, so overruns crash at the bad write. Uses up address space after about 10 s of play, so use it on short runs only |
| `TOS_SND_TRACE=1` | Log each speaker tone change |

A crash prints a stack trace, with source lines, to stderr. The trace needs the `.pdb` file
next to each `.exe` (about 12 MB each). You can delete the `.pdb` files if you only want to
play.
