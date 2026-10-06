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
#include "garrange.h"
#include "ginput.h"

// smui.c
extern GtkWindow *sm_window;
void sm_toast(const char *message);
void sm_setstatus(const char *text);
void sm_songchanged(void);
void sm_edited(void);
void sm_newsong(void);
void sm_opensong(const char *path);
void sm_togglepause(void);
int sm_subtune(void);
int sm_subtunes(void);
const char *sm_toutf8(const char *latin1);

// smarrange.c: the voice lanes, one clip per orderlist pattern
GtkWidget *arrange_new(void);
void arrange_songchanged(void);
void arrange_refresh(void);
void arrange_tick(void);
extern const double sm_palette[8][3];

// smroll.c: the piano roll for the selected clip
GtkWidget *roll_new(void);
void roll_show(int chnum, int patt, int trans);
void roll_refresh(void);
void roll_refreshinstruments(void);
void roll_tick(void);
void roll_noteon(unsigned id, int note);
void roll_noteoff(unsigned id);
void roll_releaseall(void);
void roll_setinstrument(int instrnum);
int roll_busy(void);

// smexport.c
void export_audio(void);
void export_c64(int format);

// smstart.c: the welcome dialog, the starter song and "Learn the SID"
void start_welcome(void);
void start_newfromtemplate(void);
void start_learn(void);

// smsound.c: the instruments sidebar
GtkWidget *sound_new(void);
void sound_refresh(void);
void sound_select(int instrnum);

#endif
