# Handoff: TempleOS games on macOS

This folder holds the whole project: the HolyC-to-C translator, the C runtime, the TempleOS
sources it reads, and the finished Windows build. Read [README.md](README.md) first: it
explains how everything fits together. This file covers only what is needed to finish the Mac
version.

## Where things stand (13 Nov 2026)

- **Windows: done.** All 37 games are in `out/*.exe` with their TempleOS icons. Every game
  passed two automated play tests (20 s and 40 s of random key presses) with no crash.
  `out/soak_sheet.png` shows what each game looks like after 40 s; use it as the reference.
- **Mac: not started.** Nothing has been compiled with clang yet. The Windows PC had no Mac
  compiler, so everything below about the Mac comes from reading the code. Expect surprises.

## For you, on the Mac

1. Copy this folder to the Mac. You can leave out `obj/` and `out/*.pdb` (about 490 MB of
   Windows-only build files), and `out/*.exe` too if you don't need the Windows games there.
2. Install Apple's command-line tools, which include the `clang` compiler and `python3`:
   ```bash
   xcode-select --install
   ```
   Nothing else is needed: the translator uses only Python's standard library.
3. Open the folder in Claude Code and say:
   "Read HANDOFF-MAC.md and do the Mac port. Build one game first, then all 37."
4. When games build, play a few for a minute each: keyboard, mouse and sound. The automated
   tests can't judge whether a game *feels* right.

The apps won't be signed with an Apple developer certificate. The first time you open one,
right-click it and choose **Open**. If macOS still refuses, run
`xattr -dr com.apple.quarantine <folder>` once on the folder.

## For Claude on the Mac

### What already carries over

- The translator (`tools/*.py`) and `gen.sh`, which picks `python3` on macOS.
- `gen/*.c`: the translated games and the TempleOS library (`tos_lib.c`), plain C11.
  Regenerate them on the Mac before building:
  ```bash
  sh gen.sh $(cat tools/test/games.txt)
  ```
  This takes about a minute.
- `runtime/rt_core.c` (except the debug allocator, see step 4), `rt_files.c` and `rt_gr.c`.
- `runtime/tos_rt.h`, which already has a non-MSVC branch (`TOS_TLS` is `__thread`).

### Rules learned on Windows

- Never edit `gen/` by hand. Fix `tools/` or `runtime/`, then re-run `gen.sh`.
- A function `h_Name(` defined at column 0 in `runtime/*.c` replaces the translated TempleOS
  function `Name`. Re-run `gen.sh` after adding one.
- Fixes for bugs in the original games go in `FIXES` or `BIN_FIXES` in `tools/hc_lex.py`.
  Four are already there (see README).
- TempleOS code tolerates reading freed memory and NULL plus an offset. Windows doesn't and
  macOS won't either.
- Keep the Windows build working. Shared changes to `tos_rt.h`, `hc_emit.py` and so on must
  still compile with MSVC. `rt_win32.c`, `rt_snd.c` and `build.cmd` stay for Windows.

### Work list, in order

1. **Compile the library alone first**:
   `clang -std=c11 -O2 -g -c -Iruntime -Igen gen/tos_lib.c`.
   `tos_rt.h` silences many MSVC warnings, so add matching `-Wno-...` flags for clang. Fix
   real errors in the translator.

2. **try/catch must not be optimized.** HolyC `try` is built on `setjmp`.
   `tools/hc_emit.py` wraps every function that has a `try` in `#pragma optimize("", off)`
   / `on` (search for `has_try`). That pragma is MSVC-only, and clang ignores it silently.
   Without it, locals changed after `setjmp` are undefined after `longjmp`. This gives rare
   wrong behavior, not build errors. About 60 library functions are affected.
   Fix: emit a macro such as `TOS_NOOPT` on those function definitions, defined as
   `__attribute__((optnone))` for clang. Keep the pragma for MSVC.

3. **Addresses above 4 GB.** The Windows build is linked so that every address is below
   2 GB: `build.cmd` uses `/LARGEADDRESSAWARE:NO /BASE:0x10000000`. TempleOS assumes this.
   On Apple Silicon, macOS puts every address above 4 GB, and that can't be changed.
   - Known case: `TaskValidate` in `tos_src/tos2/Kernel/KTask.HC` requires
     `0<task<=I32_MAX`, so it would reject every task. Override it in the runtime as
     `h_TaskValidate`: keep the `task->addr==task` and signature checks, and drop the range
     check.
   - There may be other places that keep pointers in 32-bit fields. Symptoms: a crash at
     an address whose top bits were cut off, or something silently not happening.

4. **`runtime/rt_core.c`** includes `<windows.h>` for the `TOS_HEAPCHECK` debug allocator
   only (`VirtualAlloc`/`VirtualProtect`, around line 110). Port it to `mmap`/`mprotect`
   behind `#ifdef _WIN32`, or compile it out on the Mac.

5. **New `runtime/rt_mac.m`** (Objective-C, Cocoa) to replace `rt_win32.c` (444 lines).
   Read `rt_win32.c` in full. It provides the following:
   - **Clock:** `h_GetTSC`, `h_SysTimerRead`, `h_HPET`. Use `mach_absolute_time` or
     `clock_gettime`. `cnts.jiffies` is in milliseconds. `main` also sets the
     `cnts.time_stamp_*` frequency fields.
   - **Screen:** `h_GrUpdateVGAGraphics` shows the 640x480 8-bit frame through the
     palette in `rt_dac`, which holds 6-bit r,g,b per color. The window uses the largest
     whole-number zoom that fits 90% of the screen, is centered, and is letterboxed to 4:3
     with black bars.
   - **Input:** keys become PC set-1 scan codes through `rt_kbd_push`, with arrows and the
     like `E0`-prefixed and break codes `|0x80`. The mouse goes to `h_MsHardSet(x, y, z,
     left, right)` in 640x480 coordinates, mapped through the letterbox.
   - **Events:** `rt_tick` updates jiffies and pumps window events now and then. `rt_idle`
     waits about 1 ms when no task is ready. `h_SysHlt` calls `rt_idle`.
   - **Task switching:** `TaskRt`, `switch_to`, `end_task`, `pick`, `h_Yield`,
     `h_TaskEndNow` and `__tos_pause` run one Win32 fiber per TempleOS task, with 64 KB
     committed and 8 MB reserved. The scheduling logic is platform-independent; only the
     fiber calls change. On the Mac, use `ucontext` (`makecontext`/`swapcontext`): it is
     deprecated but works on Apple Silicon, and needs `#define _XOPEN_SOURCE 600` before the
     includes. Give each task an 8 MB `mmap`'d stack. Keep `tos_try_top` saved per task as
     it is now. Consider moving the shared logic into its own file so both platforms use it.
   - **Exit:** `h_TosGameMain` and `h_TosQuit`. Exit code 0 is normal; 5 means the game
     stopped on an exception.
   - **Crash reports:** `rt_print_stack` and the crash and heap-corruption handlers. Use
     `sigaction` on `SIGSEGV`/`SIGBUS` with `backtrace`, and print the faulting address as
     Windows does.
   - **Test hooks**, which the test script depends on:
     - `TOS_SHOT=<file.bmp>` saves the shown frame after `TOS_SHOT_MS` ms (default 4000),
       then exits 0;
     - `TOS_KEYS=ms:hex,ms:hex,...` injects scan codes at those times.
   - **`main`:** follow the same start-up order as `rt_win32.c`'s `main`. Cocoa wants the
     main thread, so run the TempleOS scheduler there and pump `NSApp` events from
     `rt_tick`/`rt_idle`, as Windows pumps messages.
   - **Stack size:** Windows links a 16 MB main stack (`/STACK:16777216`). On the Mac, use
     `-Wl,-stack_size,0x1000000`.

6. **Sound.** `runtime/rt_snd.c` is a 62-line waveOut square wave; port it to Core Audio
   (`AudioQueue` or an `AudioUnit` render callback):
   - `rt_spk(hz)` sets the PC-speaker tone, and 0 means silence;
   - `rt_snd_init()` starts output once;
   - `TOS_SND_TRACE=1` logs each tone change.

7. **`build_mac.sh <Game>`**: compile `gen/tos_lib.c` once (cache the `.o`), then
   `gen/<Game>.c`, the portable runtime files, `rt_mac.m` and the Mac sound file. Link
   with `-framework Cocoa -framework AudioToolbox`, and produce
   `out/mac/<Game>.app`:
   - `Contents/MacOS/<Game>`;
   - `Contents/Info.plist`;
   - `Contents/Resources/<Game>.icns`. Make the icon from `out/icons/<Game>.ico`: `sips`
     can read `.ico` and write PNG, and `iconutil` or `sips` makes `.icns`.

   The game name is the file name, or the folder name for `Apps/<Game>/Run.HC`.
   `tools/test/games.txt` lists all 37 source paths. Build for the Mac's own architecture
   first. A universal build (`-arch arm64 -arch x86_64`) can come later.

8. **Test.** Port `tools/test/soak.ps1` to a shell or Python script that uses the same
   `TOS_KEYS`/`TOS_SHOT` variables:
   - Play every game for 20 s and then 40 s with random keys.
   - Expect exit 0 and a screenshot from every game except Collision and Whap, which quit
     on the first key press. That is how the originals behave.
   - Compare against `out/soak_sheet.png`.
   - Then the user plays each game.

### Apple Silicon differences to watch for

- Float-to-integer conversion that overflows: x86 gives `0x8000000000000000`, ARM
  saturates. Rare, but HolyC code may have relied on the x86 result.
- This code has never run on ARM before. If a game misbehaves only on the Mac, suspect step
  2 (optimized `setjmp`) or step 3 (pointer range) first.

### Tips from the Windows port (what saved time)

**Order of work**
- **Get one small game working end to end first.** FlapBat is the best first target: small,
  2D, keyboard only. Then pick games that each exercise something different:
  - Talons and Varoom: 3D and TempleOS's multicore code;
  - ToTheFront: menus, pop-up windows and the seed prompt;
  - CircleTrace: the mouse;
  - X-Caliber and Titanium: lots of sprites, sound, and many tasks at once.
  Only then build all 37.
- **Build the test hooks before anything else.** With `TOS_SHOT` (screenshot, then exit)
  and `TOS_KEYS` (scripted keys), every game can be tested without anyone at the keyboard.
  On Windows these turned hours of manual checking into minutes.
- **Then test in this order:**
  1. a 4-second launch-and-screenshot of all 37 (`tools/test/allrun.ps1`);
  2. a 20-second play with random keys (`soak.ps1`);
  3. a 40-second play with the game order reversed, which changes each game's key sequence.

  Each pass found bugs the earlier ones missed. ToTheFront's seed-prompt bug only appeared
  in the reversed pass, and X-Caliber's crash only on a repeat of the 20-second pass, so
  run each pass more than once.
- **Look at all the screenshots at once.** A single contact sheet (`sheet.ps1` makes one on
  Windows) shows a black or garbled game straight away.

**Making the edit-build-test loop fast**
- `gen/tos_lib.c` is huge (about 67,000 lines). Compile it once and reuse the object.
  `gen.sh` rewrites it identically every time, whichever games you pass, so rebuild it only
  when its content changes (compare a hash, not the timestamp).
- A change in `runtime/` needs no `gen.sh`; just rebuild. A change in `tools/` needs
  `gen.sh`, and usually leaves `tos_lib.c` unchanged.
- The games build independently: build them in parallel (`xargs -P`). Windows built them
  one after another, about 6 minutes for all 37.
- Run long batches in the background, and don't edit `runtime/` or `gen/` while a batch is
  building: the builds pick up half-finished files.
- Don't run two copies of the same game in the same test batch: they overwrite each other's
  screenshot and log files.

**Finding bugs quickly**
- **Crash reports first.** A report with the stack and the faulting address solved most
  crashes in one look. Windows had no debugger attached; on the Mac, `lldb` gives you one
  for free: `lldb <app>/Contents/MacOS/<Game>`, then `run`, then `bt` after a crash.
- **Reading the faulting address:**
  - a small address such as `0x50` means NULL plus a field offset, so find which struct
    field sits at that offset;
  - a garbage address means the code walked off the end of data.
- **Watch stderr for `X not implemented`.** That is how missing pieces such as `Call` and
  `ExePrint2` turned up. Also check `gen.sh`'s `N functions failed` lines: failed functions
  become stubs that print that message.
- **Temporary debug prints are fine,** directly in `gen/*.c`, as long as you re-run `gen.sh`
  afterwards to wipe them.
- **Overruns:** `TOS_HEAPCHECK=1` catches buffer overruns at the bad write, on short runs
  only. On the Mac, clang's AddressSanitizer (`-fsanitize=address`) does the same job
  better. Expect two kinds of false alarm:
  - TempleOS deliberately reads freed memory (`ScanMsg` after `JobDel`);
  - ASan can misreport around `ucontext` task switches.
- **Silent exits:** a game vanishing with no report was heap corruption, which Windows ends
  without calling the crash handler. On the Mac, also catch `SIGABRT`, which is how
  `malloc` reports corruption.

**TempleOS quirks that cost the most time**
- **Bugs in the original games:**
  - code that reads through NULL, which works on TempleOS because it maps address 0;
  - functions that forget their `return`, where TempleOS returns whatever is in a register
    and the port returns 0.
  Fix these in `FIXES`, not in the runtime. All 37 games were already scanned for missing
  returns; only X-Caliber's mattered.
- **Corrupt or missing data in the TempleOS sources.** The `tos2` copy lost some embedded
  data, and TinkerOS's copy has one corrupt sprite byte. When a sprite crash appears,
  compare the sprite with its neighbouring animation frames: they should have the same
  layout.
- **TempleOS kernel functions written in assembly** show up as "not implemented" and need a
  small C version in `runtime/`.

**Don't redo what's done**
- **Icons:** don't redo them. Making them needed TempleOS itself to draw its sprites, which
  took a while on Windows. `out/icons/*.ico` are finished: just convert them to `.icns`.
- **Fixes:** all fixes to the games, and the PC-speaker emulation in `rt_core.c`, are
  platform-independent. Only the sound output in `rt_snd.c` changes, and
  `TOS_SND_TRACE=1` lets you check the tones without listening.

**Editing**
- Shell edits (`sed`, heredocs, `re.sub` replacement strings) repeatedly turned `\n` and
  `\t` into real newlines and tabs. Edit any text with backslashes (C strings, paths) with
  an editor tool.

## Folder map

| Path | What |
|---|---|
| `README.md` | How the project works, the fixes made to the originals, testing aids |
| `tools/` | Translator (`hc_lex.py`, `hc_parse.py`, `hc_emit.py`, `hc_build.py`), HolyC boot stand-ins, icon tools |
| `tools/test/` | `games.txt` (the 37 games), Windows test scripts (`allrun.ps1`, `soak.ps1`, `sheet.ps1`), `sym.py` (Windows symbol lookup) |
| `runtime/` | C runtime. `rt_win32.c` and `rt_snd.c` are Windows-only |
| `gen/` | Translator output (regenerate with `gen.sh`) |
| `tos_src/` | TempleOS 5.03 sources (`tos2/`) and TinkerOS's copy for embedded sprites and original data files |
| `out/` | Windows `.exe` files, `icons/*.ico`, test screenshots |
| `build.cmd` | Windows build (Visual Studio Build Tools) |
