# Handoff: merge with the Windows copy and publish on GitHub

For the next Claude session. Packed 13 Nov 2026 on the Mac, after the Mac OS X port was finished.

## Update, 13 Nov 2026 evening: merged on Windows; the Mac must rebuild

Step 1 below is done. The project is now a git repository (not pushed anywhere): `67cc045` is
the copy the Mac port started from, `04fe23d` the Windows work since then, `93ef5a8` (branch
`mac`) this Mac package, and the merge commit on `main` joins them. The Windows side had
changed shared files that the Mac build has not compiled yet:

- `tools/hc_emit.py`:
  - On 32-bit builds, pointers are stored in 8-byte slots (`TosPS` in `runtime/tos_rt.h`).
  - A function passed through a differently typed function pointer gets an adapter
    (`fnref`). The float/int bit reinterpretation applies on every architecture, Mac included.
  - Stubs are written as `TOS_WEAK <decl with TOS_STUB(n)>` followed by `TOS_ALTNAME(n)`.
    `TOS_ALTNAME` is empty on clang.
- `tools/hc_lex.py`: Spawn's `data` is `I64` (a `FIXES` entry). To match, `rt_mac.m`'s
  `TaskRt` now holds `void (*entry)(int64_t)` and `int64_t data`.
- `runtime/rt_core.c`:
  - The Mac's `RtlCaptureStackBackTrace` shim is now `rt_backtrace` (same code, declared in
    `rt.h`). `rt_win32.c` has the Windows version.
  - `cmos_read` uses `GetLocalTime` again under `_WIN32`, because the 32-bit build has no C
    library `time()`/`localtime()`. The Mac keeps `localtime`.
- `README.md`: retitled for both systems; adds System requirements, Tested on, and Old Windows.

On Windows: `gen/` regenerated, and the 64-bit and 32-bit builds of all 37 games built and
passed both `allrun.ps1` and `soak.ps1`.

**On the Mac:**

1. Run `sh gen.sh $(cat tools/test/games.txt)` and `build_mac.sh` for all 37 games.
2. Run `python3 tools/test/soak.py --ms 19000`, then the same with `--arch x86_64`.
3. Fix anything that breaks. Keep `build.cmd` working.
4. Remake `TempleOS-Games-macOS.zip` with `ditto` from the new apps.

The GitHub step waits until then, and until the user gives the go-ahead.

## What this folder is

The whole project: HolyC-to-C translator (`tools/`), C runtime (`runtime/`), generated C (`gen/`),
TempleOS sources (`tos_src/`), and builds for both systems. Read [README.md](README.md) first.

| Path | What | State |
|---|---|---|
| `out/*.exe`, `out/*.pdb` | Windows builds | **Stale**: built before the Mac port changed shared files (below) |
| `out/mac/<Game>.app` | Mac builds, all 37, universal (Intel 10.6+, Apple Silicon 11+) | Current, pass the soak tests |
| `TempleOS-Games-macOS.zip` | The same 37 `.app`s, zipped on the Mac with `ditto` | Upload this as the Mac release file (see below) |
| `out/mac/shots*`, `out/mac/soak*` | Mac test screenshots and `summary.txt` | Test output only |
| `HANDOFF-MAC.md` | The Mac port brief (done) | History |
| `HANDOFF-OS9.md` | Plan for a Mac OS 9 / PowerPC port | Not started; waiting on the user |

Left out of the package: `obj/` (build cache, rebuilt automatically) and `.DS_Store` files.

## 1. Merge with the Windows copy

The user has a Windows copy of this project. It may have changed since the Mac copy was made.
Compare `tools/`, `runtime/`, `build.cmd`, `gen.sh` and `README.md` between the two before
merging. The Mac port changed or added these, and nothing else:

- **Added:** `runtime/rt_mac.m`, `build_mac.sh`, `tools/test/soak.py`, `HANDOFF-OS9.md`, this file.
- **Changed (shared with Windows):**
  - `runtime/tos_rt.h`: `TOS_NOOPT`, `TOS_WEAK`, `TOS_STUB` macros (empty or `__stub_` on MSVC).
  - `runtime/rt.h`: non-Windows `strcasecmp`/`strtok_r` aliases.
  - `runtime/rt_core.c`: Windows includes moved under `#ifdef _WIN32`; mmap/backtrace shims for
    other systems; new `h_TaskValidate`.
  - `tools/hc_emit.py`: stubs use `TOS_WEAK`/`TOS_STUB` (the MSVC `/alternatename` pragma is
    kept); `try` functions get `TOS_NOOPT`; pointer-to-`Bool` casts compare with 0.
  - `tools/hc_build.py`: runtime file list includes `*.m` as well as `*.c`.
  - `README.md`: macOS sections.
- `gen/` is regenerated from those by `sh gen.sh $(cat tools/test/games.txt)`. Never edit it by hand.

**The Windows build has not been run since these changes.** On a Windows machine run
`sh gen.sh $(cat tools/test/games.txt)` then `build.cmd <Game>` for all 37, and the Windows
soak test (`tools/test/soak.ps1`), before publishing. `build.cmd` compiles `runtime\*.c` only, so
`rt_mac.m` is ignored there by design.

## 2. Publish on GitHub

- **Ask the user** before creating the repository, choosing public or private, and pushing.
  Pushing publishes the code.
- Binaries belong in a **GitHub Release**, not in git: `.gitignore` `obj/` and `out/`.
  - Windows: zip the 37 `.exe` files (the `.pdb` files are only for crash traces; leave them
    out or ship them as a separate zip).
  - Mac: upload `TempleOS-Games-macOS.zip` exactly as it is. **Do not unzip and re-zip it on
    Windows**: that drops the Unix "executable" bit, and the apps then refuse to open on a Mac.
- `gen/` is generated but lets people build without Python; committing it is a judgment call
  (9.6 MB of text).
- `tos_src/` (about 100 MB, no single file over 50 MB, so under GitHub's limits):
  `tos2/` is TempleOS 5.03 (public domain). Check the licence of the TinkerOS copy in
  `tos_src/m_tinkeros_TinkerOS/` before publishing it.
- Mac users must right-click an app and choose **Open** the first time (ad-hoc signed, not
  notarized). Put that in the release notes.

## Rules (from the earlier handoffs)

- Never edit `gen/`; change `tools/` or `runtime/` and re-run `gen.sh`.
- Keep both `build.cmd` (Windows) and `build_mac.sh` (Mac) working after shared changes.
- No new `.c` files for one system only: `build.cmd` compiles every `runtime\*.c`.
