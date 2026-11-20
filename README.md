# Crazy Taxi 2 — native Windows port (static recompilation)

A native Windows port of **Crazy Taxi 2 (Dreamcast)** built by **static recompilation**: the SH-4 code in the
`1ST_READ.BIN` of YOUR disc is translated to C (`tools/sh4c.py`) and compiled together with a custom runtime
(`recomp/`: CPU/memory, PowerVR TA → Direct3D 11, AICA → audio, input, VMU, options menu, widescreen, etc.).

> **This repository contains no game data whatsoever.** No disc files, textures, generated code or binaries.
> You need your own disc dump. Everything derived from the game is generated locally on your machine and is
> listed in `.gitignore`.

## ⚠️ Required game version
**Only the American (NTSC-U / USA) release of Crazy Taxi 2 is supported** — `1ST_READ.BIN` version v1.004,
SHA-1 `bcff06e9defcd454d99ada84b9d26757e8ab4221`. The launcher checks the disc volume name (`CRAZY_TAXI_2`) and this
hash; dumps of other regions/versions (PAL, Japanese, etc.) are rejected and will not work, because the translated
code addresses are specific to this build.

## Disclaimer
- Preservation / study project. Crazy Taxi 2 © SEGA. No license is granted for the code (all rights reserved by the
  author); the launcher artwork is the author's own composition based on Sega material.
- **Status:** the logic was developed and tested on Linux. **Everything Windows-specific (D3D11, `win.c`, the launcher
  GUI, `build.bat`, icon/windres) is only lightly tested** — expect rough edges.

## Requirements
1. Windows 10/11, 64-bit.
2. Internet access on the first run only: if Python 3 or a C compiler (gcc/clang) is not found, the build downloads a portable
   Python (python.org embeddable) and a portable compiler (w64devkit) into `toolchain\` — nothing is installed on your system.
   (If you already have them in `PATH`, they are used instead.)
3. A dump of your own **US** disc: a folder with the `.gdi` and `track03.bin` (the data-track file must be named
   `track03.bin`).

## How to play (ready-made build with launcher)
1. Run `launcher\build.bat` — it creates `dist\CrazyTaxi2_<version>\` (the "ready version"). By default it bundles a portable C compiler (w64devkit, in `toolchain\`). If the C generated from your own disc exists locally (`recomp\ct2_funcs_*.c`), it is bundled too, making the folder fully self-contained (no Python, no downloads) — **that folder contains code derived from the game: personal use only, never share it.** Use `launcher\build.bat public` for a shareable build without it (it downloads a portable Python on first run and generates the C from the player's own disc). Use `launcher\build.bat nobundle` for a small build that downloads them on first run instead.
2. Open `dist\CrazyTaxi2_<version>\CrazyTaxi2.exe`.
3. Click **SELECT FILES** and pick the `.gdi` / `track03.bin`. The launcher validates the disc and extracts the files
   (progress bar), then generates the C code from your disc and compiles the game (second progress bar, a few minutes,
   log in `build.log`). Only then is **PLAY** unlocked.
4. **PLAY** starts the game.

## Developer mode
`jogar.bat "C:\path\to\dump"` — extracts, analyzes, generates C, compiles and runs directly.
When updating to a new version, delete `recomp\obj` and `ct2.exe` (`jogar.bat` only rebuilds when `VERSION.txt` changes).

## Layout
- `tools/` — GD-ROM extractor, analysis, classification, SH-4→C translator (`sh4c.py`)
- `recomp/` — runtime (`boot.c`, `sh4rt.*`, `ta.c`/`gpu11.inc` D3D11, `aica.*`, `win.c`, `menux.inc` options menu)
- `launcher/` — Win32 launcher (source, assets, `build.bat`)
- `ghidra/`, `names_manual.csv`, `extra_entries.txt` — analysis support
- `docs/CHANGELOG.md` — per-version history (in Portuguese); `docs/LAUNCHER_PLAN.md`

## Features
Widescreen (with edge-culling fix), keyboard/XInput controls with turbo dash, options menu, file-based VMU saves,
anisotropic filtering, and more — see the changelog.
