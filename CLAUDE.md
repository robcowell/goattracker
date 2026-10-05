# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

GoatTracker v2 (2.77): a tracker-style Commodore 64 / SID music editor written in C (plus some C++ for the SID emulation). It is being ported to a GTK4/libadwaita UI (Linux only; the Win32 and MorphOS builds were dropped), with SDL2 used only for audio. Licensed under the GPL. This tree is not a git repository. Old Windows `.exe` binaries are still checked in under `win32/`.

`readme.txt` is the authoritative user and format documentation: keyboard commands, song data semantics, the `.SNG`/`.INS`/sound-effect file formats (section 6), and the version history. It is Latin-1 encoded, so read it with `iconv -f latin1 -t utf8 readme.txt`.

## Building

All builds run from `src/`. There is no test suite. Verify changes by building and then loading or playing the example songs in `examples/` (`.sng`, `.ins`).

```sh
cd src && make              # binaries go to ../linux/ (also builds the bme/ host tools)
make clean
make install                # copies the binaries to /usr/local/bin and the man page to /usr/local/man/man1
```

- Requires `libgtk-4-dev`, `libadwaita-1-dev` and `libsdl2-dev`. `makefile` sets the flags and includes `makefile.common`, which defines `ENGINE_OBJS` (shared by `goattrk2` and `gt2reloc`) and `GUI_OBJS` (the `gtk*.c` files, the only ones compiled with GTK flags).
- Testing GUI changes without touching the user's desktop: run on a nested display (`Xephyr :5`, plus `metacity --display=:5` if a window manager is needed) with `GDK_DEBUG=no-portals` so file dialogs open on that display instead of via the desktop portal, and point `HOME` at a scratch directory so `~/.goattrk/goattrk2.cfg` isn't overwritten on exit. Drive it with `DISPLAY=:5 xdotool` and capture with `xwd -root` + `ffmpeg`.

## Architecture

**Embedded data file.** Runtime assets are packed into `goattrk2.dat` according to the list in `goattrk2.seq`: the 6502 playroutines `player.s` and `altplayer.s`, plus the font, palette, cursors and icon. `dat2inc` then turns that into `goatdata.c` as the C array `datafile[]`. The `bme_io` layer (`io_open` and friends) reads files from this embedded blob. **If you edit `player.s`, `altplayer.s`, or any `.bin` asset, regenerate `goattrk2.dat` and then `goatdata.c`.** The makefile rules do this, but stale checked-in `.o` files can hide the change, so run `make clean` when in doubt.

**GTK user interface.** Everything runs on GTK's main thread (audio runs in SDL's callback thread, as it always did). `gtkui.c` holds `main()`: it calls `goattrk2_init()` (config, command line, SDL audio, initial song; returns an exit status for things like `-?`), builds a single docked window, and calls `goattrk2_shutdown()` (stop sound, save config) on quit. The UI keeps no copy of the editor state: it draws and edits the engine's globals (`epchn`, `eppos`, `einum`, `editmode`, ...).
- `gtkui.c`: window layout (orderlist | pattern | instrument column, tables and song info below), toolbar, menus, status bar, the 20 ms tick that drives follow-play and redraws during playback, and keyboard routing. Keys on the grids are translated to the engine's key codes and fed through `runkey()` → `docommand()`, so every classic key command still works. Keys stay with GTK while a text field, dialog or menu has focus (function keys excepted). Focusing a panel sets `editmode`, and `ui_focuseditmode()` moves focus when a command changes it.
- `gtkgrid.c`: the pattern, orderlist and table editors as custom-drawn `GtkDrawingArea`s (Pango monospace font, tracker colours, click and scroll to move the cursor). The orderlist is drawn vertically, so `orderlistcursor()` in `gtkui.c` rotates the arrow keys.
- `gtkpanels.c`: instrument list (with the Shift+X/C/V/S/Del commands) and the native instrument editor; song name/author/copyright entries. Song and instrument names are Latin-1; convert with `ui_toutf8()`/`ui_fromutf8()`.
- `gtkdialogs.c`: implements the hooks in `gui.h` that the engine's command handlers call (`ui_quit`, `ui_clear`, `ui_help`, `ui_editadsr`, `ui_relocator`, `ui_load`, `ui_save`) with non-blocking libadwaita dialogs and `GtkFileDialog`. The engine must never block waiting for input.
- Key codes (`KEY_xxx` in `bme/bme_main.h`) are the ASCII value of the unshifted character, or `BME_XKEY(keysym)` (256 + the low byte of the X11/GDK keysym). They must stay below `MAX_KEYS` (512).
- The packer (`greloc.c`) has a single batch path for both programs: options come from globals (`playerversion`, `playeradr`, `zeropageadr`, `fileformat`), the output file from `packedsongname`, and messages go to `STDOUT`/`STDERR`, which the editor points at a memory stream (`relocout`) and shows afterwards. `relocsuccess` reports the outcome.
- Help text lives in `ghelp.c` as a section table (`gethelpsections()`) rendered by `ui_help()`.

**Editor (`goattrk2`).** `goattrk2.c` holds `goattrk2_init`/`goattrk2_shutdown`, the config, the global editor state (declared `extern` in `goattrk2.h` under `#ifndef GOATTRK2_C`) and `docommand()`, which dispatches a keypress on `editmode` (`EDIT_PATTERN`, `EDIT_ORDERLIST`, `EDIT_INSTRUMENT`, `EDIT_TABLES`, `EDIT_NAMES`). Each subsystem lives in its own `g*.c` file:
- `gsong.c`: song data model (orderlists, patterns, instruments, the wave/pulse/filter/speed tables), plus `.sng`/`.ins` load and save and v1 import.
- `gpattern.c`, `gorder.c`, `ginstr.c`, `gtable.c`: the editors for each data type.
- `gplay.c`: the in-editor playroutine. It is a C reimplementation that must stay behaviourally in sync with the 6502 `player.s`/`altplayer.s`.
- `greloc.c`: packer/relocator. It emits the song data as assembler source, prepends the chosen playroutine, assembles it with the built-in 6510 cross-assembler in `asm/` (taken from Exomizer), and writes PRG/BIN/SID output.
- `gsound.c` / `gsid.cpp`: audio output. `gsid.cpp` picks between reSID (`resid/`) and reSID-fp (`resid-fp/`) at runtime, and also handles the HardSID and Catweasel hardware paths (Windows-specific code still sits behind `#ifdef __WIN32__` but is no longer built). Hardware SID output is driven by an SDL2 timer (`SDL_AddTimer`); emulated output by the SDL2 audio callback in `bme/bme_snd.c`.
- `gdisplay.c` (note names, song timer, `followplayupdate()`), `gfile.c` (paths, `editstring()`), `ghelp.c` (help text). File loading works on a bare file name in the current directory, so the UI `chdir`s to the chosen folder first.
- `gcommon.h`: shared constants, including pattern command numbers, data limits (`MAX_PATT`, `MAX_INSTR`, ...), orderlist and pattern byte encodings, and the `INSTR` struct.

**`bme/`** is what remains of a small Covert BitOps library: SDL2 audio (`bme_snd.c`), the embedded data file reader (`bme_io.c`), endianness helpers, and the key code definitions.

**`gt2reloc`** is the command-line packer. `gt2reloc.c` `#define`s `GT2RELOC` and `#include`s `greloc.c` directly, and links `ENGINE_OBJS` (no GTK). It defines its own copies of the editor globals the shared modules reference.

**Standalone utilities** (`ins2snd2.c`, `sngspli2.c`, `mod2sng.c`) are single-file tools that link only `bme_end.o`, which handles endianness.

When changing song format semantics, keep `gsong.c` (load/save), `gplay.c` (editor playback), `player.s`/`altplayer.s` (C64 playback), `greloc.c` (packing), and readme section 6 consistent.
