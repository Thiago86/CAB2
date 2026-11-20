# Launcher (implemented in v15zzj)

- `launcher/src` (`launcher.c` Win32/GDI, `launcher_core.h` portable core tested on Linux, `launcher.rc`), `launcher/assets` (`.raw` artwork + `icon.ico`), `launcher/build.bat` → `dist/CrazyTaxi2_<version>/CrazyTaxi2.exe` (ready version).
- Flow: SELECT FILES (`.gdi` or `track03.bin`) → validates volume `CRAZY_TAXI_2` and the SHA-1 of `1ST_READ.BIN` (**US v1.004 only**) → extracts to `disc\` with a progress bar → generates the C from the disc and compiles the game (second progress bar, `build.log`) → unlocks PLAY.
- The ready version ships `launcher\run.bat` (a copy of `jogar.bat`) instead of `jogar.bat` at the root. PLAY runs it to start the game.
- `launcher.ini` remembers the already-extracted disc (opens directly with PLAY unlocked).
- Pending / ideas: button hover, language, choosing the destination folder, shipping a prebuilt `ct2.exe` (C generation from the disc still needs Python and a compiler).
