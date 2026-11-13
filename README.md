# TempleOS games on Windows

The 37 games that ship with TempleOS 5.03 (Terry A. Davis, public domain), translated from
HolyC to C and built as ordinary Windows programs. Each game is one `.exe` in `out/` with
its TempleOS icon, and needs nothing else installed.

The games run on the TempleOS library itself: the window manager, DolDoc, the graphics
library, menus and pop-ups are TempleOS's own code, translated. A small C runtime replaces
only what was hardware or assembly language (memory, task switching, keyboard, mouse,
screen, PC speaker, clock).

## Playing

Double-click `out/<Game>.exe`. Controls are the same as in TempleOS:

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
