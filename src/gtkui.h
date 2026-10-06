//
// GOATTRACKER v2 GTK user interface, shared declarations
//
// The editor state lives in the engine's globals (epchn, eppos, einum,
// editmode, ...). The GTK widgets draw that state and feed keypresses to the
// engine's command handlers on the main thread; nothing here keeps a copy.
//

#ifndef GTKUI_H
#define GTKUI_H

#include <adwaita.h>
#include "goattrk2.h"
#include "gui.h"
#include "gundo.h"

// gtkui.c: window, toolbar, menus and keyboard routing
extern GtkWindow *mainwindow;
void ui_refresh(void);
void ui_focuseditmode(void);
void ui_runkey(unsigned raw, unsigned ascii, int shift);
void ui_quitnow(void);
void ui_settitle(void);
void ui_toast(const char *message);
void ui_edited(void);
void ui_undo(void);
void ui_redo(void);
GtkWidget *hexspin_new(int max, int digits);

// gtkgrid.c: the custom-drawn pattern, orderlist and table editors
extern GtkWidget *patterngrid;
extern GtkWidget *ordergrid;
extern GtkWidget *tablegrid;
GtkWidget *grid_pattern_new(void);
GtkWidget *grid_orderlist_new(void);
GtkWidget *grid_tables_new(void);
void grid_setfontscale(int scale);
void grid_redraw(void);
void grid_followcursor(void);
void grid_relayout(void);

// gtkpanels.c: instrument list/editor and song information
extern GtkWidget *instrlist;
extern GtkWidget *songnameentry;
GtkWidget *panel_instruments_new(void);
GtkWidget *panel_songinfo_new(void);
void panels_sync(void);
void panels_syncall(void);

// gtkdialogs.c
void ui_loadsong(int merge);
void ui_savesong(void);
void ui_loadinstrument(void);
void ui_saveinstrument(void);
void ui_showsoundfailure(void);
void ui_confirmdiscard(void (*proceed)(void));
void ui_opensongpath(const char *path, int merge);
void ui_afterstartupload(void);
void ui_loadinstrumentpath(const char *path);
void ui_quicksave(void);
void ui_backup(void);
void ui_preferences(void);

// gtkui.c: settings of this editor (kept in ~/.goattrk/gtkedition.ini)
extern int settings_backupinterval;
extern int settings_decodetables;
extern int settings_showpiano;
extern int settings_showsidstate;

// gtkmonitor.c: piano keyboard and SID register views
GtkWidget *monitor_piano_new(void);
GtkWidget *monitor_sidview_new(void);
void monitor_update(void);
void monitor_setvisible(int showpiano, int showsidstate);
void ui_backupschanged(void);
void ui_restartsound(void);

// File chooser (gtkdialogs.c): done is called with the chosen path
typedef void (*FILEDONE)(const char *path, gpointer data);
void ui_choosefile(const char *title, const char *dir, const char *name, const char *pattern,
  const char *filtername, int save, FILEDONE done, gpointer data);
void ui_exportagain(void);

// gtkwav.c
void ui_wavexport(void);

// gtkinfo.c: plain-language descriptions of song data
void info_describe(char *buf, int size);
void table_describe(int table, int pos, char *buf, int size, int brief);
int table_isreachable(int table, int pos);

// Waveform toggles for a wavetable row (wavetable = 1) or an instrument's
// first frame waveform, in a popover pointing at where (NULL = whole widget)
void ui_waveformeditor(GtkWidget *parent, const GdkRectangle *where, unsigned char *value, int wavetable, int coalesce);

// Jumps to related data (ENTER on a table pointer or command, the instrument
// editor's table buttons) remember where they came from; Alt+Left goes back
void ui_pushplace(void);
void ui_goback(void);
void ui_playfromhere(void);
void ui_setdetune(int cents);

// Latin-1 (song data) <-> UTF-8 (GTK) conversion; returns a static buffer
const char *ui_toutf8(const char *latin1);
void ui_fromutf8(char *dest, const char *utf8, int maxlen);

#endif
