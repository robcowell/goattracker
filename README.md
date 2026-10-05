# GoatTracker 2 · GTK Edition

A modern Linux front end for **GoatTracker 2**, Lasse Öörni's tracker-style music editor for the Commodore 64's SID chip. It keeps everything that makes GoatTracker GoatTracker: the playroutine, reSID emulation, packer/relocator, file formats and every keyboard command. The full-screen 1990s text interface is replaced by a docked, resizable GTK4/libadwaita window in the tradition of trackers from FastTracker 2 to Renoise.

![GoatTracker GTK Edition playing GhostTrackers](docs/screenshots/hero.png)

This is an unofficial port of GoatTracker v2.77. The engine and song data are unchanged, so songs, instruments and packed output work interchangeably with the original.

## Before and after

| GoatTracker 2.77 | GTK Edition |
| --- | --- |
| ![The original 100×37 text screen](docs/screenshots/classic.png) | ![The docked GTK window](docs/screenshots/hero.png) |
| One fixed 100×37 character screen drawn with an 8×16 bitmap font, scaled only in whole steps | A resizable window with movable dividers between panels and a scalable monospace font |
| Everything driven by the keyboard, with a few mouse hot spots | Every key command still works, plus native buttons, menus, sliders and fields; click, drag to select and right-click in the grids |
| No undo | Undo and redo for every edit |
| No record of unsaved changes: loading a song replaces your work without a warning | Unsaved changes are tracked, and you are asked before anything is lost |
| A home-made text-mode file selector | The desktop's file chooser |
| Packer options chosen with cursor keys on a text screen | A settings dialog with a report of the packed sizes |
| Help is a text screen scrolled with the cursor keys | A help window with headed sections, scrolled with the mouse or keyboard |
| SDL 1.2 (no longer maintained) | GTK4, libadwaita and SDL2 audio |

## What's new

### A tracker layout you can see all at once

Orderlists on the left, patterns in the middle, instruments down the right, and the wave, pulse, filter and speed tables along the bottom, all visible together and all live while the song plays. Panels are separated by draggable dividers.

All three grid editors work with the mouse:

- Click to place the cursor, drag to select rows, and Shift+click to extend the selection.
- Right-click for the editor's commands: cut, copy and paste, transpose, insert and delete rows, split and join patterns, set the play start and end positions, and more. These are the classic Shift+key commands, so they act on the selection when there is one.
- Double-click an orderlist entry to open that pattern.
- The scroll wheel moves the view without moving the cursor; any key brings the view back to the cursor.

#### Pattern editor

The edit row stays centred while the song scrolls past it, in the style of Renoise and Impulse Tracker. Notes, instruments, commands and their data each have their own colour, and empty fields can be shown as dots. The highlight step is adjustable, and the row each channel is playing is tinted green.

![Pattern editor](docs/screenshots/pattern.png)

Marked blocks (Shift+cursor keys) are shaded, and clicking a channel header mutes it.

![Pattern editor with a marked block](docs/screenshots/pattern-selection.png)

#### Vertical orderlists

The three per-channel orderlists now run top to bottom like a sequencer, so the cursor keys move the way they look: up and down between positions, left and right between digits and channels. Transposes and repeats are coloured, and green and red bars mark the F2 play start and end positions.

![Orderlist](docs/screenshots/orderlist.png)

#### All four tables side by side

Table commands and jumps are coloured, and a green bar marks where the selected instrument's pointer starts in each table.

![Tables and song information](docs/screenshots/tables.png)

### A native instrument editor

The instrument list sits above an editor for the selected instrument:

- Attack, decay, sustain and release are sliders, with a live drawing of the envelope.
- Each table pointer is a hex field with a button that jumps to that table data.
- The less obvious parameters (gate timer flags, first-frame waveform) have tooltips explaining them.
- **Test Note** plays the instrument for as long as you hold it.
- Buttons for cut, copy, paste, "paste and remap" and delete sit alongside the classic Shift+X/C/V/S/Del keys.

![Instrument list and editor](docs/screenshots/instrument-editor.png)

Song name, author and copyright are ordinary text fields. Accented characters are converted to and from the Latin-1 the song format stores.

### Undo and redo

GoatTracker 2 never had undo. Now every change can be undone, with **Ctrl+Z** and **Ctrl+Shift+Z** / **Ctrl+Y** or the arrows in the header bar. That covers note entry, orderlist and table edits, instrument edits, pasting, transposing, merging, loading an instrument, and even Optimize and Clear.

- Undo returns the cursor to where the edit happened.
- Typing a name or dragging a slider counts as one step.
- Up to 500 steps are kept.

### Never lose unsaved work

The title bar shows **•** while the song has unsaved changes. The bullet disappears again if you undo back to the saved state.

![Header bar with unsaved changes](docs/screenshots/header-unsaved.png)

Closing the window, quitting or opening another song asks first, and **Save…** saves and then carries on with what you were doing.

![Save changes prompt](docs/screenshots/unsaved-prompt.png)

### Menus, toolbar and status bar

- **Header bar:** transport controls, follow-play, and undo/redo.
- **Toolbar:** edit/jam mode, octave, row highlight step, note-entry layout (Protracker, DMC or Janko), SID model, speed multiplier and the hard-restart ADSR. These used to be hidden behind Shift+F-key combinations.
- **Status bar:** playback time and each channel's position.

| Main menu | View menu |
| --- | --- |
| ![Main menu](docs/screenshots/menu.png) | ![View menu](docs/screenshots/view-menu.png) |

The main menu has separate items for opening, merging and saving songs, loading and saving instruments, and exporting, so none of them depend on which editor you are in.

### Pack, relocate and export in a real dialog

The playroutine options are switches that respect their dependencies (sound effects, zeropage ghost registers and full buffering switch on buffered writes for you). The player and zeropage addresses are hex fields, and the output format is a drop-down. After packing you get the size report the command-line packer prints.

| Options | Result |
| --- | --- |
| ![Export options](docs/screenshots/export.png) | ![Export report](docs/screenshots/export-report.png) |

The packer is shared with `gt2reloc`, which produces byte-identical output to the original.

### Dialogs instead of y/n prompts

**Shift+Esc** used to ask six yes/no questions in a row. It now opens one dialog where you tick the parts to clear and set the new pattern length.

![Optimize or clear dialog](docs/screenshots/clear.png)

The keyboard reference (**F12**) is a help window; **Shift+F12** puts the current editor's keys first.

![Keyboard help](docs/screenshots/help.png)

### Bigger text when you want it

The grids use a scalable monospace font. **View → Larger Text** (or `-w2`…`-w4` on the command line) makes everything bigger, and the window can be any size. The screenshot below is at the third size, the same song as above.

![Larger text in a larger window](docs/screenshots/large-text.png)

## What stays the same

- **Keyboard:** every key command from the original works, in the editor it belongs to. The full reference is in [readme.txt](readme.txt) section 2.3, or press **F12**. The few differences:
  - Ctrl+Z and Ctrl+Y are undo and redo. Ctrl otherwise still doubles as Shift; Shift+Z still cycles auto-advance.
  - Tab cycles between the editors.
  - Text fields keep their own keys, except the function keys.
- **Files:** `.sng` and `.ins` files are untouched in format, and so is the settings file `~/.goattrk/goattrk2.cfg`.
- **Command line:** the same options as before (`goattrk2 song.sng -s1 -e1` and so on). `-w` now sets the text size, and `-??` opens the help window at startup.
- **Sound:** the same reSID and reSID-fp emulation, and the same playroutine, timing and packer.
- **Tools:** `gt2reloc`, `ins2snd2`, `sngspli2` and `mod2sng` are built alongside the editor and behave as before.

## Building

You need a C/C++ compiler, GTK 4.10 or later, libadwaita 1.5 or later, and SDL2. On Debian or Ubuntu (24.04 or later):

```sh
sudo apt install build-essential pkg-config libgtk-4-dev libadwaita-1-dev libsdl2-dev
```

On Fedora:

```sh
sudo dnf install gcc gcc-c++ make pkgconf gtk4-devel libadwaita-devel SDL2-devel
```

Then build and run:

```sh
cd src
make -j
../linux/goattrk2 ../examples/dojo.sng
```

`make install` copies the programs to `/usr/local/bin` and the man page to `/usr/local/man/man1`.

## Not included

- **Other platforms:** the Windows and MorphOS builds have been dropped, and this edition is for Linux.
- **The classic interface:** the original full-screen text screen is gone.
- **Hardware output:** HardSID and Catweasel support is still in the code but hasn't been tested with this edition.

## Credits and licence

GoatTracker 2 is by **Lasse Öörni** (Cadaver, Covert BitOps), with HardSID 4U support by Téli Sándor, the reSID engine by Dag Lem, reSID distortion and nonlinearity by Antti S. Lankila, the 6510 cross-assembler from Exomizer2 by Magnus Lind, and patches by Stefan A. Haubenthal, Valerio Cannone, Raine M. Ekman, Tero Lindeman, Henrik Paulini, Groepaz and Birgit Jauernig. See [readme.txt](readme.txt) for the original documentation and version history.

Distributed under the GNU General Public License; see [copying](copying). The example songs in `examples/` are by their respective authors, as credited in each song.
