#ifndef GHOST_H
#define GHOST_H

// Engine host layer: path-based wrappers around the engine's calls, which
// otherwise take file names, the current subtune and play positions from
// editor globals. For front ends other than the tracker (SidMonkey); no GTK.

// Results of host_loadsong() besides the engine's LOAD_OK, LOAD_DAMAGED and
// LOAD_MULTICHANNEL
#define HOST_LOAD_BADPATH -1   // name too long, or the folder can't be entered
#define HOST_LOAD_NOTSONG -2   // not a GoatTracker v2 song, or unreadable

// Load a song (the current one is replaced only if the file is valid) and
// reset the undo history. Changes the current directory to the song's.
int host_loadsong(const char *path);

// Save the song to path and remember it as the song's file. Returns 1 on
// success.
int host_savesong(const char *path);

// Restart sound output with the current settings (songs can carry their own)
int host_restartsound(void);

// Play a subtune from the beginning, or from where channel chnum starts the
// orderlist entry at songpos. Returns 0 if that position is never reached.
void host_play(int subtune);
int host_playfrom(int subtune, int chnum, int songpos);
void host_stop(void);

// Sound a note (FIRSTNOTE-based, as in patterns) with an instrument on a
// channel without the song playing, and release it
void host_preview(int note, int ins, int chnum);
void host_release(int chnum);

#endif
