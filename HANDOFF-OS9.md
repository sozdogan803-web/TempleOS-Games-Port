# Plan: TempleOS games on Mac OS 9 (PowerPC G3)

Target: the user's slot-loading iMac G3 ("Dimension blue"), running Mac OS 9.2.
Status (13 Nov 2026): **plan only, nothing built.** The Mac OS X version (Intel 10.6+, Apple
Silicon 11+) is finished; see [README.md](README.md) and [HANDOFF-MAC.md](HANDOFF-MAC.md).

Read this whole file before starting. The two hard problems (byte order and 32-bit pointers)
are in the translator, not the runtime, and are where most of the time will go.

## Still needed from the user

1. The iMac's **memory and processor speed** (Apple menu > About This Computer). Slot-loading
   iMac G3s have 350-500 MHz and shipped with 64-128 MB; they take up to 1 GB.
2. A **Mac OS 9.2 install CD or image** of their own, to run Mac OS 9 in QEMU on the
   development Mac. Without it every test means copying to the iMac by hand.
3. A way to copy files to the iMac (see Phase 1).

## Why it is hard

| Problem | Size | Where it bites |
|---|---|---|
| **Byte order.** PowerPC is big-endian; TempleOS assumes little-endian everywhere | 466 narrow-pointer puns in `gen/` (e.g. `Line` reads `((int32_t*)&x)[1]` as the high half); 176 embedded data blobs (sprites, fonts, `.GR`/`.DD` files) stored little-endian | Wrong coordinates, garbled sprites, broken text |
| **32-bit pointers.** G3 is 32-bit; TempleOS structs have 8-byte pointer fields and code treats memory slots as 8 bytes | ~665 pointer/`int64_t` casts; every `#pragma pack(1)` struct layout; `TosFun` arg slots; `Spawn`'s `rsp` frame; `MAlloc` headers | Struct offsets differ from what TempleOS code computes; pointers read from the wrong half |
| **64-bit integers on a 32-bit CPU.** All HolyC integers are `I64` | everywhere | Speed: each `I64` op is 2+ instructions; division and float<->int conversion are libgcc calls |
| **Mac OS 9 has no Unix.** No `mmap`, `ucontext`, signals, `backtrace`, Cocoa, CoreAudio | `runtime/rt_mac.m` | A new `rt_os9.c` |
| **No memory protection.** A wild write can freeze the whole Mac | debugging | Do all bring-up in QEMU |

## The approach

### Byte order: keep TempleOS memory little-endian

Have the translator emit every multi-byte load and store of TempleOS memory through macros:

```c
#define TOS_LD32(p)    /* LE host: *(uint32_t*)(p);  BE host: __builtin_bswap32(*(uint32_t*)(p)) */
#define TOS_ST32(p, v)
/* ... 16 and 64 bits, F64 through a 64-bit integer */
```

Memory then always holds little-endian data, as on TempleOS, so **the puns and the embedded
blobs both work unchanged**. PowerPC has byte-reversed load/store instructions (`lwbrx`,
`stwbrx`, `lhbrx`), and GCC emits them for these patterns, so 32-bit accesses cost nothing
extra. A 64-bit access is two of them.

- Locals whose address is never taken can stay native (the translator knows which).
- `runtime/*.c` must use the same macros for every TempleOS struct field it touches
  (`h_gr.m_dc2->m_body`, `h_cnts.m_jiffies`, task fields in the scheduler, and so on).
- Alternative considered: fix each pun and each blob parser one by one. Faster at run time
  but impossible to know when you have found them all. Rejected.

### 32-bit: keep pointers 8 bytes wide in memory

Emit pointer fields (data and function pointers, in structs, arrays and globals) as 8-byte
little-endian storage, read and written through `TOS_LDP`/`TOS_STP`, which convert to and from
a native 32-bit pointer. Struct layouts and `sizeof` then match TempleOS exactly, and every
"8-byte slot" assumption still holds. Pointer-typed locals stay native.

**Host-side check, before touching PowerPC:** on the 64-bit Mac build, make `TOS_STP` store
`ptr ^ 0x5A5A...` and `TOS_LDP` undo it. Any access that bypasses the macros then reads a
garbage pointer and crashes at once. Run the full soak test this way until it is clean. The
same trick can't check integer byte order, because the embedded blobs are raw little-endian;
those misses show up in QEMU.

### Mac OS 9 runtime: `runtime/rt_os9.c` (Carbon, PowerPC CFM)

Build a **Carbon** app (CarbonLib). The same binary then runs on Mac OS 9 and natively on
Mac OS X 10.0-10.4 PowerPC, so G4/G5 Macs come along for free. Fallback if Retro68's Carbon
support is not good enough: a classic Toolbox app (InterfaceLib), Mac OS 9 only.

| Piece | Mac OS X (`rt_mac.m`) | Mac OS 9 (`rt_os9.c`) |
|---|---|---|
| Clock | `mach_absolute_time` | `Microseconds()` or `UpTime()` |
| Screen | CGImage on a CALayer | 8-bit `GWorld` whose color table is set from `rt_dac`; `CopyBits` to the window (it can scale) |
| Keyboard | `NSEvent` key codes | poll `GetKeys()` each tick and diff against the last `KeyMap`: gives make and break for every key, modifiers included. Classic key codes match modern Mac key codes, so `rt_mac.m`'s `sc_of[]` table carries over |
| Mouse | `NSEvent` | `GetMouse`, `Button()`; Control-click is the right button (one-button mouse) |
| Tasks | `ucontext`, 8 MB `mmap`'d stacks | **Thread Manager** cooperative threads: `NewThread`, `YieldToThread`. Same scheduler logic as `rt_mac.m` |
| Sound | AudioQueue callback | Sound Manager `SndPlayDoubleBuffer`, the same square-wave fill as `snd_fill` (it runs at interrupt time: no allocation) |
| Events | `nextEventMatchingMask` | `WaitNextEvent` with a 1-tick sleep in `rt_idle` |
| Crash report | signals + `backtrace` | none; log to a file and debug in QEMU |
| Exit | `exit` | `ExitToShell` |
| Test hooks | `TOS_SHOT`, `TOS_KEYS` env vars | no environment: read a `TOS Test` text file next to the app (`shot=`, `keys=` lines); write `TOS Shot.bmp` and `TOS Log` |

Memory: the app's partition is fixed in its `SIZE` resource. Allocate with `NewPtr` or
newlib `malloc` from the app heap.

### Toolchain

- **Retro68** (github.com/autc04/Retro68): a modern GCC cross-compiler for classic Mac OS,
  68k and PowerPC. Builds from source on macOS (about an hour; Homebrew deps such as `cmake`,
  `boost`, `bison`, `texinfo`). It handles the C11 the translator emits. It produces
  MacBinary `.bin` files and HFS `.dsk` disk images, keeping the resource fork.
- **QEMU** (`brew install qemu`): `qemu-system-ppc -M mac99,via=pmu` boots Mac OS 9.2 with no
  ROM file. Sound in mainline QEMU is unreliable for Mac OS 9, so check tones with the
  `TOS Log` trace and test sound by ear on the iMac.
- **hfsutils** (`brew install hfsutils`): `hmount`/`hcopy -m` put apps into the guest disk
  image while QEMU is off.
- Automation: put the game's alias in the guest's `Startup Items` folder, boot QEMU, inject
  keys with the monitor's `sendkey`, grab the screen with `screendump`, then power off.

## Phases (in order, with go/no-go checks)

**Phase 1: toolchain and transfer (about 1 session).**
- Build Retro68 and install QEMU and hfsutils.
- Install Mac OS 9.2 in QEMU from the user's image.
- Build a Carbon "hello window" that draws an 8-bit GWorld and plays a tone. Run it in QEMU,
  then on the iMac. That proves the copy path. Options: a USB stick (FAT16 is safest; decode
  `.bin` with StuffIt Expander), a burned HFS CD, or FTP.
- **Check:** if Carbon apps misbehave, switch to the classic Toolbox target now.

**Phase 2: memory and stack budget, on the modern Mac (about 1 session).**
- Measure each game's TempleOS heap peak, excluding Cocoa: count bytes in `h_MAlloc`/`h_Free`.
- Measure the deepest stack each task uses: check how many pages of each `mmap`'d stack were
  touched (`mincore`).
- Fix the known leak: `h_HeapCtrlInit`'s shortcut never frees a dead task's blocks. Track
  blocks per heap and free them in `h_HeapCtrlDel`.
- **Go/no-go:** if the heap plus stacks (Thread Manager stacks are real memory, so size them
  from the measurements, likely 128-512 KB each) don't fit in the iMac's RAM minus about
  20 MB for Mac OS 9, stop or add RAM.

**Phase 3: translator, byte order and 64-bit pointer storage (2-4 sessions).**
- Implement `TOS_LD*`/`TOS_ST*` and `TOS_LDP`/`TOS_STP` in `tools/hc_emit.py` and
  `runtime/tos_rt.h`. Port `runtime/*.c` to the macros.
- With no-op macros on the modern Mac, all 37 games must still pass the soak tests unchanged.
- Run the pointer-XOR check build until the soak is clean.
- Keep the Windows build compiling (MSVC; the macros must be plain C).

**Phase 4: Mac OS 9 runtime (1-2 sessions).** Write `rt_os9.c` as in the table above, plus
`build_os9.sh` (Retro68 toolchain, `SIZE` resource, icon from `out/icons/*.ico` converted to
`icl8`/`ics8`, MacBinary output). `__tos_unimpl`, `rt_print_stack` and the like log to
`TOS Log`.

**Phase 5: bring-up in QEMU (2-4 sessions, the bulk of the debugging).**
- Start with **FlapBat**, then TicTacToe and Digits (simple 2D).
- Then ToTheFront (pop-ups, `Line`), X-Caliber (sprites and sound), Talons and Varoom (3D).
- Expect byte-order misses: garbled sprites or text, or a crash at an address with the bytes
  reversed. Compare against `out/mac/soak/*.png`.
- **Go/no-go on the real iMac with FlapBat:** if it is unplayably slow, try lowering the
  window manager's frame rate (`WinMgrTask` refresh) and drawing at 1x without scaling.
  If it is still too slow, limit the release to the 2D games.

**Phase 6: all 37 and the user's play test (1-2 sessions plus the user's time).**

**Total: roughly 8-14 working sessions**, likely spread over weeks. The uncertainty is mostly
in Phase 5 (how many byte-order misses) and in whether a 350-500 MHz G3 is fast enough.

## Risks

- **Speed.** The G3 is about 10-20 times slower per core than the development Mac, and all
  64-bit integer math is emulated in pairs of 32-bit instructions. Expect the 2D games to be
  playable and the 3D ones (Talons, Varoom) not to be.
- **Memory.** On Mac OS X the games peak at 65-155 MB, much of it Cocoa and the dead-task leak.
  The Mac OS 9 budget is decided in Phase 2.
- **Misaligned accesses.** `#pragma pack(1)` puts fields at odd offsets. The G3 handles
  misaligned integer loads in hardware, slowly when they cross cache lines. Byte-reversed and
  floating-point misaligned accesses can trap to a slow software handler. If profiling shows
  this, align the hot structs.
- **Retro68 Carbon maturity.** It is less used than the classic target. Phase 1 decides.
- **Toolbox limits.** No preemptive threads (fine: TempleOS here is cooperative anyway) and
  no crash protection (use QEMU).

## Rules carried over from the Mac OS X port

- Never edit `gen/` by hand: change `tools/` or `runtime/` and re-run `gen.sh`.
- Keep Windows (`build.cmd`) and Mac OS X (`build_mac.sh`) building after shared changes.
  Re-run `python3 tools/test/soak.py` (both `--arch` values) after every translator change.
- `-ftrivial-auto-var-init=zero` mattered with clang. With GCC (Retro68), use the same flag
  (GCC 12 and later have it) or add `-O1` and check `Line` in ToTheFront first.
- Functions with `try` need GCC's `__attribute__((optimize("O0")))`. Map `TOS_NOOPT` to it
  for GCC (clang uses `optnone`).
