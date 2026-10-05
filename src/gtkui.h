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

// Latin-1 (song data) <-> UTF-8 (GTK) conversion; returns a static buffer
const char *ui_toutf8(const char *latin1);
void ui_fromutf8(char *dest, const char *utf8, int maxlen);

#endif
