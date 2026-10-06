#ifndef SM_H
#define SM_H

// SidMonkey: a beginner-friendly SID composer on the GoatTracker engine.
// The UI keeps no copy of the song: it draws and edits the engine's data
// (orderlists, patterns, instruments) directly.

#include <adwaita.h>
#include "goattrk2.h"
#include "gundo.h"
#include "ginfo.h"
#include "ghost.h"

// smui.c
extern GtkWindow *sm_window;
void sm_toast(const char *message);
void sm_setstatus(const char *text);
void sm_songchanged(void);
void sm_edited(void);
int sm_subtune(void);
int sm_subtunes(void);
const char *sm_toutf8(const char *latin1);

// smarrange.c: the voice lanes, one clip per orderlist pattern
GtkWidget *arrange_new(void);
void arrange_songchanged(void);
void arrange_refresh(void);
void arrange_tick(void);

#endif
