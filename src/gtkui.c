//
// GOATTRACKER v2 GTK user interface
//
// A single docked window in the style of modern trackers: orderlists on the
// left, the pattern editor in the middle, instruments on the right, and the
// tables and song information along the bottom.
//
// Keyboard input on the editors is translated into the engine's key codes
// and run through docommand(), so every classic key command keeps working.
//

#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <glib-unix.h>
#include "gtkui.h"

#define APPID "net.sourceforge.goattracker2"

GtkWindow *mainwindow = NULL;

static AdwApplication *app;
static GtkWidget *toolbarview;
static GtkWidget *windowtitle;
static GtkWidget *toastoverlay;
static GtkWidget *instrpanel;
static GtkWidget *songpanel;
static GtkWidget *recordbutton, *followbutton, *octavespin, *stepspin;
static GtkWidget *keymodedrop, *siddrop, *speeddrop, *hrbutton, *subtunespin;
static GtkWidget *statusplay, *statuspos, *statusinfo, *statusmode;
static int syncing = 0;
static int wasplaying = 0;

// Song length, measured by a silent offline render while stopped
static double songlength = -2;          // -2 = not known yet, -1 = doesn't end
static unsigned lengthversion = (unsigned)-1;
static int lengthsubtune = -1;
static gint64 lengthchanged = 0;
static int quitting = 0;

// Settings of this editor, kept apart from goattrk2.cfg so that file stays
// in the stock format
int settings_backupinterval = 30;
int settings_decodetables = 1;
static guint backuptimer = 0;

//
// Latin-1 conversion (song and instrument names are stored as Latin-1)
//

const char *ui_toutf8(const char *latin1)
{
  static char buf[256];
  char *utf8 = g_convert(latin1, -1, "UTF-8", "ISO-8859-1", NULL, NULL, NULL);

  g_strlcpy(buf, utf8 ? utf8 : "", sizeof buf);
  g_free(utf8);
  return buf;
}

void ui_fromutf8(char *dest, const char *utf8, int maxlen)
{
  char *latin1 = g_convert_with_fallback(utf8, -1, "ISO-8859-1", "UTF-8", "?", NULL, NULL, NULL);

  memset(dest, 0, maxlen);
  if (latin1) strncpy(dest, latin1, maxlen - 1);
  g_free(latin1);
}

//
// Keyboard translation: GDK key events to the engine's raw key codes
// (see BME_XKEY in bme/bme_main.h) and characters
//

static unsigned keyindex(guint keyval)
{
  switch (keyval)
  {
    case GDK_KEY_BackSpace: return KEY_BACKSPACE;
    case GDK_KEY_Tab:
    case GDK_KEY_ISO_Left_Tab: return KEY_TAB;
    case GDK_KEY_Return: return KEY_ENTER;
    case GDK_KEY_Escape: return KEY_ESC;
    case GDK_KEY_Delete: return KEY_DEL;
    case GDK_KEY_ISO_Level3_Shift: return KEY_RIGHTALT;

    // Keypad without numlock reports the digit keys
    case GDK_KEY_KP_Insert: return KEY_KP0;
    case GDK_KEY_KP_End: return KEY_KP1;
    case GDK_KEY_KP_Down: return KEY_KP2;
    case GDK_KEY_KP_Next: return KEY_KP3;
    case GDK_KEY_KP_Left: return KEY_KP4;
    case GDK_KEY_KP_Begin: return KEY_KP5;
    case GDK_KEY_KP_Right: return KEY_KP6;
    case GDK_KEY_KP_Home: return KEY_KP7;
    case GDK_KEY_KP_Up: return KEY_KP8;
    case GDK_KEY_KP_Prior: return KEY_KP9;
    case GDK_KEY_KP_Delete: return KEY_KPPERIOD;
  }
  if ((keyval >= 0x20) && (keyval < 0x100)) return gdk_keyval_to_lower(keyval);
  if ((keyval & 0xff00) == 0xff00) return BME_XKEY(keyval);
  return 0;
}

// Raw key codes identify the physical key: use the unshifted keyval
static unsigned rawkeyof(GtkEventControllerKey *controller, guint keyval, guint keycode)
{
  GdkEvent *event = gtk_event_controller_get_current_event(GTK_EVENT_CONTROLLER(controller));
  GdkKeymapKey *keys;
  guint *keyvals;
  int n, c;
  guint layout = event ? gdk_key_event_get_layout(event) : 0;

  if (gdk_display_map_keycode(gtk_widget_get_display(GTK_WIDGET(mainwindow)), keycode, &keys, &keyvals, &n))
  {
    for (c = 0; c < n; c++)
    {
      if (((guint)keys[c].group == layout) && (keys[c].level == 0))
      {
        keyval = keyvals[c];
        break;
      }
    }
    g_free(keys);
    g_free(keyvals);
  }
  return keyindex(keyval);
}

// The character a keypress types, as the classic version expected it
static unsigned keyascii(guint keyval, GdkModifierType state)
{
  gunichar ch;

  switch (keyval)
  {
    case GDK_KEY_Return:
    case GDK_KEY_KP_Enter: return 13;
    case GDK_KEY_BackSpace: return 8;
    case GDK_KEY_Tab:
    case GDK_KEY_ISO_Left_Tab: return 9;
    case GDK_KEY_Escape: return 27;
    case GDK_KEY_Delete: return 127;
  }
  if (state & GDK_CONTROL_MASK)
  {
    guint lower = gdk_keyval_to_lower(keyval);
    if ((lower >= 'a') && (lower <= 'z')) return lower - 'a' + 1;
    return 0;
  }
  ch = gdk_keyval_to_unicode(keyval);
  if ((ch >= 32) && (ch < 256) && (ch != 127)) return ch;
  return 0;
}

static int isfunctionkey(unsigned raw)
{
  return (raw >= KEY_F1) && (raw <= KEY_F12);
}

//
// Running engine commands
//

// The orderlist is shown vertically, so the cursor keys are turned to match:
// up/down move between positions, left/right between digits and channels
static int orderlistcursor(void)
{
  switch (rawkey)
  {
    case KEY_UP:
    orderleft();
    orderleft();
    return 1;

    case KEY_DOWN:
    orderright();
    orderright();
    return 1;

    case KEY_LEFT:
    if (escolumn) escolumn = 0;
    else
    {
      rawkey = KEY_UP;
      orderlistcommands();
      escolumn = 1;
    }
    return 1;

    case KEY_RIGHT:
    if (!escolumn) escolumn = 1;
    else
    {
      rawkey = KEY_DOWN;
      orderlistcommands();
      escolumn = 0;
    }
    return 1;
  }
  return 0;
}

//
// Going back after a jump to related data
//

typedef struct
{
  int mode;
  int chn, patt, pos, column;     // pattern editor
  int subtune, ochn, opos;        // orderlist
  int instr, ipos;                // instrument
  int table, tpos, tcolumn;       // tables
} PLACE;

#define MAX_PLACES 32

static PLACE places[MAX_PLACES];
static int numplaces = 0;

static void syncbackaction(void)
{
  g_simple_action_set_enabled(G_SIMPLE_ACTION(g_action_map_lookup_action(G_ACTION_MAP(app), "back")), numplaces > 0);
}

static void getplace(PLACE *p)
{
  p->mode = editmode;
  p->chn = epchn;
  p->patt = epnum[epchn];
  p->pos = eppos;
  p->column = epcolumn;
  p->subtune = esnum;
  p->ochn = eschn;
  p->opos = eseditpos;
  p->instr = einum;
  p->ipos = eipos;
  p->table = etnum;
  p->tpos = etpos;
  p->tcolumn = etcolumn;
}

static void pushplace(const PLACE *p)
{
  if (numplaces == MAX_PLACES)
  {
    memmove(&places[0], &places[1], (MAX_PLACES - 1) * sizeof(PLACE));
    numplaces--;
  }
  places[numplaces++] = *p;
  syncbackaction();
}

void ui_pushplace(void)
{
  PLACE p;

  getplace(&p);
  pushplace(&p);
}

void ui_goback(void)
{
  PLACE *p;

  if (!numplaces)
  {
    ui_toast("Nothing to go back to");
    return;
  }
  p = &places[--numplaces];
  // The song may have changed since; keep everything in range
  if (p->subtune != esnum)
  {
    esnum = p->subtune;
    songchange();
  }
  epchn = p->chn;
  if (!isplaying()) epnum[epchn] = p->patt;
  eppos = MIN(p->pos, pattlen[epnum[epchn]]);
  epcolumn = p->column;
  eschn = p->ochn;
  eseditpos = MIN(p->opos, songlen[esnum][eschn] + 1);
  einum = p->instr;
  eipos = p->ipos;
  etnum = p->table;
  etpos = p->tpos;
  etcolumn = p->tcolumn;
  editmode = p->mode;
  syncbackaction();
  grid_followcursor();
  ui_focuseditmode();
  ui_refresh();
}

static void runkey(unsigned raw, unsigned ascii, int shift, int allowhex)
{
  int oldmode = editmode;
  int oldrecord = recordmode;
  PLACE from;

  key = ascii;
  rawkey = raw;
  shiftpressed = shift;
  if (rawkey == KEY_KPENTER)
  {
    key = KEY_ENTER;
    rawkey = KEY_ENTER;
  }
  if ((rawkey >= KEY_KP0) && (rawkey <= KEY_KP9)) key = '0' + (rawkey - KEY_KP0);
  converthex();
  if (!allowhex) hexnybble = -1;

  getplace(&from);
  undo_markcursor();
  if ((editmode != EDIT_ORDERLIST) || (!orderlistcursor()))
    docommand();
  undo_checkpoint(0);

  // ENTER jumps from a reference to what it refers to: remember the way back
  if ((rawkey == KEY_ENTER) && (editmode != oldmode)) pushplace(&from);

  if (editmode != oldmode) ui_focuseditmode();
  if (recordmode != oldrecord) jam_releaseall();
  ui_refresh();
}

void ui_runkey(unsigned raw, unsigned ascii, int shift)
{
  runkey(raw, ascii, shift, 1);
}

static void onkeyreleased(GtkEventControllerKey *controller, guint keyval, guint keycode,
  GdkModifierType state, gpointer data)
{
  jam_noteoff(rawkeyof(controller, keyval, keycode));
}

static void onactivechanged(GtkWindow *window, GParamSpec *pspec, gpointer data)
{
  // Key releases don't arrive while another window has the focus
  if (!gtk_window_is_active(window)) jam_releaseall();
}

static gboolean onkeypressed(GtkEventControllerKey *controller, guint keyval, guint keycode,
  GdkModifierType state, gpointer data)
{
  GtkWidget *focus = gtk_window_get_focus(mainwindow);
  unsigned raw, ascii;
  int shift = (state & (GDK_SHIFT_MASK | GDK_CONTROL_MASK)) != 0;

  // Dialogs and menus handle their own keys
  if (adw_application_window_get_visible_dialog(ADW_APPLICATION_WINDOW(mainwindow))) return FALSE;
  if (focus)
  {
    GtkWidget *popup = gtk_widget_get_ancestor(focus, GTK_TYPE_POPOVER);
    if ((popup) && (gtk_widget_get_mapped(popup))) return FALSE;
  }

  if (((keyval == GDK_KEY_Return) || (keyval == GDK_KEY_KP_Enter)) && (state & GDK_ALT_MASK))
  {
    if (gtk_window_is_fullscreen(mainwindow)) gtk_window_unfullscreen(mainwindow);
    else gtk_window_fullscreen(mainwindow);
    return TRUE;
  }

  raw = rawkeyof(controller, keyval, keycode);
  ascii = keyascii(keyval, state);
  if ((!raw) && (!ascii)) return FALSE;

  // Keyboard movement brings the views back to the cursor
  grid_followcursor();

  // Alt+Left returns from a jump to table or instrument data
  if ((keyval == GDK_KEY_Left) && ((state & (GDK_ALT_MASK | GDK_CONTROL_MASK | GDK_SHIFT_MASK)) == GDK_ALT_MASK))
  {
    ui_goback();
    return TRUE;
  }

  // Ctrl+Enter in the orderlist plays from that position (Shift+Enter keeps
  // its classic meaning)
  if ((editmode == EDIT_ORDERLIST) && (focus == ordergrid) && (raw == KEY_ENTER) &&
    ((state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK | GDK_ALT_MASK)) == GDK_CONTROL_MASK))
  {
    ui_playfromhere();
    return TRUE;
  }

  // Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y undo and redo everywhere, text fields
  // included (their own undo is off). Shift+Z still cycles auto-advance.
  if ((state & GDK_CONTROL_MASK) && ((raw == KEY_Z) || (raw == KEY_Y)))
  {
    if ((raw == KEY_Y) || (state & GDK_SHIFT_MASK)) ui_redo();
    else ui_undo();
    return TRUE;
  }

  // Shift+F11 exports a WAV and Ctrl+F9 repeats the last export (plain F11
  // and F9 keep their classic meanings)
  if ((raw == KEY_F11) && (state & GDK_SHIFT_MASK))
  {
    ui_wavexport();
    return TRUE;
  }
  if ((raw == KEY_F9) && (state & GDK_CONTROL_MASK))
  {
    ui_exportagain();
    return TRUE;
  }

  // Ctrl+S saves and Ctrl+, opens Preferences from anywhere; Shift+S and
  // Shift+, keep their classic meanings
  if ((state & GDK_CONTROL_MASK) && (!(state & GDK_SHIFT_MASK)) && ((raw == KEY_S) || (raw == KEY_COMMA)))
  {
    if (raw == KEY_S) ui_quicksave();
    else ui_preferences();
    return TRUE;
  }

  // Tab cycles between the editors, as in the classic version
  if (raw == KEY_TAB)
  {
    runkey(raw, ascii, shift, 0);
    return TRUE;
  }

  // Text fields keep their keys; only the function keys stay global
  if ((focus) && (GTK_IS_EDITABLE(focus)))
  {
    if (!isfunctionkey(raw)) return FALSE;
    runkey(raw, 0, shift, 0);
    return TRUE;
  }

  if ((focus) && (gtk_widget_is_ancestor(focus, instrpanel)))
  {
    // The list moves with the cursor keys; everything else is the classic
    // instrument command set (Shift+X/C/V/S/Del, Space, + and -, ...)
    switch (raw)
    {
      case KEY_UP:
      case KEY_DOWN:
      case KEY_PGUP:
      case KEY_PGDN:
      case KEY_HOME:
      case KEY_END:
      if (!shift) return FALSE;
    }
    editmode = EDIT_INSTRUMENT;
    eipos = 0;
    runkey(raw, ascii, shift, 0);
    return TRUE;
  }

  // Jam mode: note keys on the pattern editor play on any free channel and
  // stop when they are released
  if ((!recordmode) && (focus == patterngrid) && (editmode == EDIT_PATTERN) && (!epcolumn) && (ascii) &&
    (!(state & (GDK_SHIFT_MASK | GDK_CONTROL_MASK | GDK_ALT_MASK))))
  {
    int note = pattern_notekey(raw);

    if (note >= 0)
    {
      jam_noteon(raw, note);
      return TRUE;
    }
  }

  if ((focus == patterngrid) || (focus == ordergrid) || (focus == tablegrid) || (!focus))
  {
    runkey(raw, ascii, shift, 1);
    return TRUE;
  }

  // Other widgets (toolbar controls): only the global keys
  if ((isfunctionkey(raw)) || (raw == KEY_ESC))
  {
    runkey(raw, 0, shift, 0);
    return TRUE;
  }
  return FALSE;
}

void ui_focuseditmode(void)
{
  switch (editmode)
  {
    case EDIT_PATTERN: gtk_widget_grab_focus(patterngrid); break;
    case EDIT_ORDERLIST: gtk_widget_grab_focus(ordergrid); break;
    case EDIT_INSTRUMENT: gtk_widget_grab_focus(GTK_WIDGET(gtk_list_box_get_row_at_index(GTK_LIST_BOX(instrlist), einum))); break;
    case EDIT_TABLES: gtk_widget_grab_focus(tablegrid); break;
    case EDIT_NAMES: gtk_widget_grab_focus(songnameentry); break;
  }
}

//
// Status and toolbar synchronization
//

static void updatestatus(void)
{
  char buf[128];
  int c, len = 0;

  len = sprintf(buf, "%s  %02d:%02d", isplaying() ? "Playing" : "Stopped", timemin, timesec);
  if (songlength >= 0) sprintf(buf + len, " / %d:%02d", (int)songlength / 60, (int)songlength % 60);
  else if (songlength == -1) sprintf(buf + len, " / no end");
  gtk_label_set_text(GTK_LABEL(statusplay), buf);
  len = 0;

  for (c = 0; c < MAX_CHN; c++)
  {
    int chnpos = MAX(chn[c].songptr - 1, 0);
    int chnrow = chn[c].pattptr / 4;
    if (chnrow > pattlen[chn[c].pattnum]) chnrow = pattlen[chn[c].pattnum];
    len += sprintf(buf + len, "%sCh%d %02X/%02d", c ? "   " : "", c + 1, chnpos, chnrow);
  }
  gtk_label_set_text(GTK_LABEL(statuspos), buf);

  {
    char info[256];
    info_describe(info, sizeof info);
    gtk_label_set_text(GTK_LABEL(statusinfo), info);
  }

  {
    static const char *advance[] = {"", "  ·  Auto-advance", "  ·  Auto-advance (all)"};
    sprintf(buf, "%s%s", recordmode ? "Edit mode" : "Jam mode", advance[autoadvance % 3]);
    gtk_label_set_text(GTK_LABEL(statusmode), buf);
  }
}

static GtkWidget *volumescale;

static void onvolumechanged(GtkRange *range, gpointer data)
{
  if (syncing) return;
  mastervolume = (int)gtk_range_get_value(range);
}

void ui_setdetune(int cents)
{
  SDL_LockAudio();
  sid_setdetune(cents);
  SDL_UnlockAudio();
}

static void synctoolbar(void)
{
  char buf[32];

  syncing = 1;
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(recordbutton), recordmode);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(followbutton), followplay);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(octavespin), epoctave);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(stepspin), stepsize);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(subtunespin), esnum);
  gtk_drop_down_set_selected(GTK_DROP_DOWN(keymodedrop), keypreset);
  gtk_drop_down_set_selected(GTK_DROP_DOWN(siddrop), sidmodel);
  gtk_drop_down_set_selected(GTK_DROP_DOWN(speeddrop), multiplier);
  sprintf(buf, "HR %04X", adparam);
  gtk_button_set_label(GTK_BUTTON(hrbutton), buf);
  gtk_range_set_value(GTK_RANGE(volumescale), mastervolume);
  syncing = 0;
}

void ui_settitle(void)
{
  char buf[MAX_PATHNAME + 16];

  // GNOME style: a bullet marks unsaved changes
  snprintf(buf, sizeof buf, "%s%s", undo_isdirty() ? "• " : "",
    strlen(loadedsongfilename) ? ui_toutf8(loadedsongfilename) : "Untitled");
  adw_window_title_set_subtitle(ADW_WINDOW_TITLE(windowtitle), buf);
}

// View → Larger Text: the grids draw with their own font sizes, and
// everything else (header bar, toolbar, panels, status bar, menus, popovers
// and dialogs) inherits a CSS font size set on the window, by the same
// factor the grids use (12/10, 14/10, 16/10)
static int textscale = 1;

void ui_settextscale(int scale)
{
  static GtkCssProvider *provider = NULL;
  static const char *classes[] = {NULL, NULL, "gt-textsize-2", "gt-textsize-3", "gt-textsize-4"};
  int c;

  textscale = CLAMP(scale, 1, 4);
  if (!mainwindow) return;
  if (!provider)
  {
    // libadwaita gives some styles fixed sizes, so restate them relative to
    // the scaled text
    static const char *css =
      ".gt-textsize-2 { font-size: 120%; } .gt-textsize-3 { font-size: 140%; } "
      ".gt-textsize-4 { font-size: 160%; } "
      ".gt-textsize-2 popover > contents { font-size: 120%; } "
      ".gt-textsize-3 popover > contents { font-size: 140%; } "
      ".gt-textsize-4 popover > contents { font-size: 160%; } "
      ".gt-textsize-2 .heading, .gt-textsize-3 .heading, .gt-textsize-4 .heading { font-size: 100%; } "
      ".gt-textsize-2 .caption, .gt-textsize-3 .caption, .gt-textsize-4 .caption { font-size: 82%; } "
      ".gt-textsize-2 .title-1, .gt-textsize-3 .title-1, .gt-textsize-4 .title-1 { font-size: 181%; } "
      ".gt-textsize-2 .title-2, .gt-textsize-3 .title-2, .gt-textsize-4 .title-2 { font-size: 136%; } "
      ".gt-textsize-2 .title-4, .gt-textsize-3 .title-4, .gt-textsize-4 .title-4 { font-size: 118%; }";

    provider = gtk_css_provider_new();
#if GTK_CHECK_VERSION(4, 12, 0)
    gtk_css_provider_load_from_string(provider, css);
#else
    gtk_css_provider_load_from_data(provider, css, -1);
#endif
    gtk_style_context_add_provider_for_display(gtk_widget_get_display(GTK_WIDGET(mainwindow)),
      GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  }
  for (c = 2; c <= 4; c++) gtk_widget_remove_css_class(GTK_WIDGET(mainwindow), classes[c]);
  if (textscale > 1) gtk_widget_add_css_class(GTK_WIDGET(mainwindow), classes[textscale]);
}

static void syncundoactions(void)
{
  g_simple_action_set_enabled(G_SIMPLE_ACTION(g_action_map_lookup_action(G_ACTION_MAP(app), "undo")), undo_canundo());
  g_simple_action_set_enabled(G_SIMPLE_ACTION(g_action_map_lookup_action(G_ACTION_MAP(app), "redo")), undo_canredo());
  syncbackaction();
}

// Called after a native widget changed the song: record it and show it
void ui_edited(void)
{
  ui_settitle();
  syncundoactions();
}

static void afterundo(void)
{
  grid_followcursor();
  panels_syncall();
  ui_focuseditmode();
  ui_refresh();
}

void ui_undo(void)
{
  if (undo_undo()) afterundo();
  else ui_toast("Nothing to undo");
}

void ui_redo(void)
{
  if (undo_redo()) afterundo();
  else ui_toast("Nothing to redo");
}

void ui_refresh(void)
{
  if (!mainwindow) return;
  grid_redraw();
  panels_sync();
  synctoolbar();
  updatestatus();
  ui_settitle();
  syncundoactions();
}

void ui_toast(const char *message)
{
  adw_toast_overlay_add_toast(ADW_TOAST_OVERLAY(toastoverlay), adw_toast_new(message));
}

// Measure the song again a moment after it stops changing (not while
// playing: the measurement uses the playroutine)
static void updatesonglength(void)
{
  if ((undo_version() == lengthversion) && (esnum == lengthsubtune)) return;
  if (!lengthchanged)
  {
    lengthchanged = g_get_monotonic_time();
    return;
  }
  if (g_get_monotonic_time() - lengthchanged < 800000) return;
  lengthchanged = 0;
  lengthversion = undo_version();
  lengthsubtune = esnum;
  songlength = render_songlength(esnum, 1800);
  if (songlength < 0) songlength = -1;
  updatestatus();
}

static gboolean tick(gpointer data)
{
  int playing = isplaying();

  if (!playing) updatesonglength();

  if ((playing) || (wasplaying))
  {
    if ((playing) && (followplay)) grid_followcursor();
    followplayupdate();
    grid_redraw();
    updatestatus();
  }
  wasplaying = playing;
  monitor_update();
  return G_SOURCE_CONTINUE;
}

//
// Toolbar handlers
//

void ui_restartsound(void)
{
  if (!sound_init(b, mr, writer, hardsid, sidmodel, ntsc, multiplier, catweasel, interpolate, customclockrate))
    ui_toast("Sound output could not be restarted");
}

static void onrecordtoggled(GtkToggleButton *button, gpointer data)
{
  if (syncing) return;
  recordmode = gtk_toggle_button_get_active(button);
  jam_releaseall();
  ui_refresh();
}

static void onfollowtoggled(GtkToggleButton *button, gpointer data)
{
  if (syncing) return;
  followplay = gtk_toggle_button_get_active(button);
}

static void onoctavechanged(GtkSpinButton *spin, gpointer data)
{
  if (syncing) return;
  epoctave = (int)gtk_spin_button_get_value(spin);
}

static void onstepchanged(GtkSpinButton *spin, gpointer data)
{
  if (syncing) return;
  stepsize = (int)gtk_spin_button_get_value(spin);
  grid_redraw();
}

static void onsubtunechanged(GtkSpinButton *spin, gpointer data)
{
  int target = (int)gtk_spin_button_get_value(spin);

  if (syncing) return;
  while (esnum < target) nextsong();
  while (esnum > target) prevsong();
  undo_checkpoint(0);
  ui_refresh();
}

static void onkeymodechanged(GObject *drop, GParamSpec *pspec, gpointer data)
{
  if (syncing) return;
  keypreset = gtk_drop_down_get_selected(GTK_DROP_DOWN(drop));
}

static void onsidchanged(GObject *drop, GParamSpec *pspec, gpointer data)
{
  if (syncing) return;
  sidmodel = gtk_drop_down_get_selected(GTK_DROP_DOWN(drop));
  ui_restartsound();
}

static void onspeedchanged(GObject *drop, GParamSpec *pspec, gpointer data)
{
  if (syncing) return;
  multiplier = gtk_drop_down_get_selected(GTK_DROP_DOWN(drop));
  ui_restartsound();
}

static void onhrclicked(GtkButton *button, gpointer data)
{
  ui_editadsr();
}

//
// Actions
//

static void onplay(GSimpleAction *action, GVariant *parameter, gpointer data)
{
  initsong(esnum, GPOINTER_TO_INT(data));
  ui_refresh();
}

// Play from the orderlist entry under the cursor, with the song in the state
// it would be in had it played from the start
void ui_playfromhere(void)
{
  if (eseditpos >= songlen[esnum][eschn])
  {
    ui_toast("Choose a pattern in the orderlist to play from");
    return;
  }
  if (!render_seek(esnum, eschn, eseditpos, 1800))
    ui_toast("The song doesn't reach this position when played from the start");
  ui_refresh();
}

static void onloop(GSimpleAction *action, GVariant *parameter, gpointer data)
{
  loopplay = !loopplay;
  if (!loopplay) looprowend = -1;
  g_simple_action_set_state(action, g_variant_new_boolean(loopplay));
}

static void onstop(GSimpleAction *action, GVariant *parameter, gpointer data)
{
  stopsong();
  ui_refresh();
}

static void onsimpleaction(GSimpleAction *action, GVariant *parameter, gpointer data)
{
  const char *name = g_action_get_name(G_ACTION(action));

  if (!strcmp(name, "undo")) ui_undo();
  else if (!strcmp(name, "redo")) ui_redo();
  else if (!strcmp(name, "new")) ui_clear();
  else if (!strcmp(name, "open")) ui_loadsong(0);
  else if (!strcmp(name, "merge")) ui_loadsong(1);
  else if (!strcmp(name, "save")) ui_quicksave();
  else if (!strcmp(name, "saveas")) ui_savesong();
  else if (!strcmp(name, "prefs")) ui_preferences();
  else if (!strcmp(name, "back")) ui_goback();
  else if (!strcmp(name, "exportagain")) ui_exportagain();
  else if (!strcmp(name, "wav")) ui_wavexport();
  else if (!strcmp(name, "loadinstr")) ui_loadinstrument();
  else if (!strcmp(name, "saveinstr")) ui_saveinstrument();
  else if (!strcmp(name, "export")) ui_relocator();
  else if (!strcmp(name, "help")) ui_help(0);
  else if (!strcmp(name, "quit")) ui_quit();
  else if (!strcmp(name, "mute"))
  {
    mutechannel(epchn);
    ui_refresh();
  }
  else if (!strcmp(name, "fullscreen"))
  {
    if (gtk_window_is_fullscreen(mainwindow)) gtk_window_unfullscreen(mainwindow);
    else gtk_window_fullscreen(mainwindow);
  }
  else if (!strcmp(name, "zoomin")) grid_setfontscale(++bigwindow > 4 ? (bigwindow = 4) : bigwindow);
  else if (!strcmp(name, "zoomout")) grid_setfontscale(--bigwindow < 1 ? (bigwindow = 1) : bigwindow);
}

static void ontoggleaction(GSimpleAction *action, GVariant *parameter, gpointer data)
{
  int bit = GPOINTER_TO_INT(data);
  GVariant *state = g_action_get_state(G_ACTION(action));
  int on = !g_variant_get_boolean(state);

  g_variant_unref(state);
  g_simple_action_set_state(action, g_variant_new_boolean(on));
  if (on) patterndispmode |= bit;
  else patterndispmode &= ~bit;
  grid_redraw();
}

static void savesettings(void);

static void ondecodetables(GSimpleAction *action, GVariant *parameter, gpointer data)
{
  settings_decodetables = !settings_decodetables;
  g_simple_action_set_state(action, g_variant_new_boolean(settings_decodetables));
  grid_relayout();
  savesettings();
}

static void onshowmonitor(GSimpleAction *action, GVariant *parameter, gpointer data)
{
  int piano = settings_showpiano, sidstate = settings_showsidstate;

  if (GPOINTER_TO_INT(data)) sidstate = !sidstate;
  else piano = !piano;
  monitor_setvisible(piano, sidstate);
  g_simple_action_set_state(action, g_variant_new_boolean(GPOINTER_TO_INT(data) ? sidstate : piano));
  savesettings();
}

static void onabout(GSimpleAction *action, GVariant *parameter, gpointer data)
{
  static const char *developers[] = {
    "Lasse Öörni (editor)",
    "Téli Sándor (HardSID 4U support)",
    "Stefan A. Haubenthal",
    "Valerio Cannone",
    "Raine M. Ekman",
    "Tero Lindeman",
    "Henrik Paulini",
    "Groepaz",
    "Birgit Jauernig (microtonal support)",
    NULL
  };
  static const char *credits[] = {
    "Dag Lem (reSID engine)",
    "Antti S. Lankila (reSID distortion / nonlinearity)",
    "Magnus Lind (6510 cross-assembler from Exomizer2)",
    "Antonio Vera (icon)",
    "Simon Bennett (command quick reference)",
    NULL
  };
  AdwDialog *about = adw_about_dialog_new();
  const char *version = strrchr(programname, 'v');

  adw_about_dialog_set_application_name(ADW_ABOUT_DIALOG(about), "GoatTracker");
  adw_about_dialog_set_version(ADW_ABOUT_DIALOG(about), version ? version + 1 : "");
  adw_about_dialog_set_comments(ADW_ABOUT_DIALOG(about), "Tracker-style music editor for the Commodore 64 SID chip");
  adw_about_dialog_set_website(ADW_ABOUT_DIALOG(about), "http://sourceforge.net/projects/goattracker2");
  adw_about_dialog_set_developers(ADW_ABOUT_DIALOG(about), developers);
  adw_about_dialog_add_credit_section(ADW_ABOUT_DIALOG(about), "Uses work by", credits);
  adw_about_dialog_set_license_type(ADW_ABOUT_DIALOG(about), GTK_LICENSE_CUSTOM);
  adw_about_dialog_set_license(ADW_ABOUT_DIALOG(about),
    "Distributed under the GNU General Public License (see the file COPYING).");
  adw_dialog_present(about, GTK_WIDGET(mainwindow));
}

typedef struct
{
  const char *label;
  const char *action;
  const char *accel;     // shown in the menu; the key itself is handled by the editor
} MENUITEM;

static GMenu *menusection(const MENUITEM *items)
{
  GMenu *menu = g_menu_new();

  for (; items->label; items++)
  {
    GMenuItem *item = g_menu_item_new(items->label, items->action);
    if (items->accel) g_menu_item_set_attribute(item, "accel", "s", items->accel);
    g_menu_append_item(menu, item);
    g_object_unref(item);
  }
  return menu;
}

static void appendsection(GMenu *menu, const char *submenu, const MENUITEM *items)
{
  GMenu *section = menusection(items);

  if (submenu) g_menu_append_submenu(menu, submenu, G_MENU_MODEL(section));
  else g_menu_append_section(menu, NULL, G_MENU_MODEL(section));
  g_object_unref(section);
}

static GMenuModel *buildmenu(void)
{
  static const MENUITEM edit[] = {
    {"Undo", "app.undo", "<Control>z"},
    {"Redo", "app.redo", "<Control><Shift>z"},
    {"Go Back", "app.back", "<Alt>Left"},
    {NULL}
  };
  static const MENUITEM file[] = {
    {"New Song…", "app.new", "<Shift>Escape"},
    {"Open Song…", "app.open", "F10"},
    {"Merge Song…", "app.merge", "<Shift>F10"},
    {"Save Song", "app.save", "<Control>s"},
    {"Save Song As…", "app.saveas", "F11"},
    {NULL}
  };
  static const MENUITEM instrument[] = {
    {"Load Instrument…", "app.loadinstr", NULL},
    {"Save Instrument…", "app.saveinstr", NULL},
    {"Pack, Relocate & Export…", "app.export", "F9"},
    {"Export Again", "app.exportagain", "<Control>F9"},
    {"Export WAV…", "app.wav", "<Shift>F11"},
    {NULL}
  };
  static const MENUITEM playback[] = {
    {"Play from Beginning", "app.play", "F1"},
    {"Play from Position", "app.playpos", "F2"},
    {"Play Pattern", "app.playpattern", "F3"},
    {"Stop", "app.stop", "F4"},
    {"Mute Channel", "app.mute", "<Shift>F4"},
    {"Loop", "app.loop", NULL},
    {NULL}
  };
  static const MENUITEM view[] = {
    {"Larger Text", "app.zoomin", NULL},
    {"Smaller Text", "app.zoomout", NULL},
    {"Hexadecimal Row Numbers", "app.hexrows", NULL},
    {"Dots for Empty Fields", "app.dots", NULL},
    {"Describe Table Rows", "app.decodetables", NULL},
    {"Piano Keyboard", "app.showpiano", NULL},
    {"SID Registers", "app.showsidstate", NULL},
    {"Fullscreen", "app.fullscreen", "<Alt>Return"},
    {NULL}
  };
  static const MENUITEM help[] = {
    {"Preferences", "app.prefs", "<Control>comma"},
    {"Keyboard Help", "app.help", "F12"},
    {"About GoatTracker", "app.about", NULL},
    {"Quit", "app.quit", "Escape"},
    {NULL}
  };
  GMenu *menu = g_menu_new();

  appendsection(menu, NULL, file);
  appendsection(menu, NULL, edit);
  appendsection(menu, NULL, instrument);
  appendsection(menu, "Playback", playback);
  appendsection(menu, "View", view);
  appendsection(menu, NULL, help);
  return G_MENU_MODEL(menu);
}

static void addactions(void)
{
  static const char *simple[] = {"undo", "redo", "back", "new", "open", "merge", "save", "saveas", "prefs", "exportagain", "wav", "loadinstr", "saveinstr", "export",
    "help", "quit", "mute", "fullscreen", "zoomin", "zoomout", NULL};
  static const struct { const char *name; int mode; } plays[] = {
    {"play", PLAY_BEGINNING}, {"playpos", PLAY_POS}, {"playpattern", PLAY_PATTERN}};
  GSimpleAction *action;
  int c;

  for (c = 0; simple[c]; c++)
  {
    action = g_simple_action_new(simple[c], NULL);
    g_signal_connect(action, "activate", G_CALLBACK(onsimpleaction), NULL);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(action));
    g_object_unref(action);
  }
  for (c = 0; c < 3; c++)
  {
    action = g_simple_action_new(plays[c].name, NULL);
    g_signal_connect(action, "activate", G_CALLBACK(onplay), GINT_TO_POINTER(plays[c].mode));
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(action));
    g_object_unref(action);
  }
  action = g_simple_action_new("stop", NULL);
  g_signal_connect(action, "activate", G_CALLBACK(onstop), NULL);
  g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(action));
  g_object_unref(action);

  action = g_simple_action_new("about", NULL);
  g_signal_connect(action, "activate", G_CALLBACK(onabout), NULL);
  g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(action));
  g_object_unref(action);

  action = g_simple_action_new_stateful("hexrows", NULL, g_variant_new_boolean(patterndispmode & 1));
  g_signal_connect(action, "activate", G_CALLBACK(ontoggleaction), GINT_TO_POINTER(1));
  g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(action));
  g_object_unref(action);
  action = g_simple_action_new_stateful("dots", NULL, g_variant_new_boolean(patterndispmode & 2));
  g_signal_connect(action, "activate", G_CALLBACK(ontoggleaction), GINT_TO_POINTER(2));
  g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(action));
  g_object_unref(action);
  action = g_simple_action_new_stateful("loop", NULL, g_variant_new_boolean(loopplay));
  g_signal_connect(action, "activate", G_CALLBACK(onloop), NULL);
  g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(action));
  g_object_unref(action);
  action = g_simple_action_new_stateful("showpiano", NULL, g_variant_new_boolean(settings_showpiano));
  g_signal_connect(action, "activate", G_CALLBACK(onshowmonitor), GINT_TO_POINTER(0));
  g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(action));
  g_object_unref(action);
  action = g_simple_action_new_stateful("showsidstate", NULL, g_variant_new_boolean(settings_showsidstate));
  g_signal_connect(action, "activate", G_CALLBACK(onshowmonitor), GINT_TO_POINTER(1));
  g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(action));
  g_object_unref(action);
  action = g_simple_action_new_stateful("decodetables", NULL, g_variant_new_boolean(settings_decodetables));
  g_signal_connect(action, "activate", G_CALLBACK(ondecodetables), NULL);
  g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(action));
  g_object_unref(action);
}

//
// Window construction
//

static GtkWidget *actionbutton(const char *icon, const char *action, const char *tooltip)
{
  GtkWidget *button = gtk_button_new_from_icon_name(icon);

  gtk_actionable_set_action_name(GTK_ACTIONABLE(button), action);
  gtk_widget_set_tooltip_text(button, tooltip);
  gtk_widget_set_focusable(button, FALSE);
  return button;
}

static GtkWidget *labelled(const char *text, GtkWidget *widget)
{
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  GtkWidget *label = gtk_label_new(text);

  gtk_widget_add_css_class(label, "dim-label");
  gtk_box_append(GTK_BOX(box), label);
  gtk_box_append(GTK_BOX(box), widget);
  return box;
}

static GtkWidget *dropdown(const char *const *items, GCallback callback, const char *tooltip)
{
  GtkWidget *drop = gtk_drop_down_new_from_strings(items);

  gtk_widget_set_tooltip_text(drop, tooltip);
  gtk_widget_set_focusable(drop, FALSE);
  g_signal_connect(drop, "notify::selected", callback, NULL);
  return drop;
}

static GtkWidget *spin(int min, int max, GCallback callback, const char *tooltip)
{
  GtkWidget *s = gtk_spin_button_new_with_range(min, max, 1);

  gtk_widget_set_tooltip_text(s, tooltip);
  g_signal_connect(s, "value-changed", callback, NULL);
  return s;
}

static GtkWidget *buildheaderbar(void)
{
  GtkWidget *header = adw_header_bar_new();
  GtkWidget *transport = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  GtkWidget *menubutton = gtk_menu_button_new();
  GMenuModel *menu = buildmenu();

  gtk_widget_add_css_class(transport, "linked");
  gtk_box_append(GTK_BOX(transport), actionbutton("media-playback-start-symbolic", "app.play", "Play from beginning (F1)"));
  gtk_box_append(GTK_BOX(transport), actionbutton("media-seek-forward-symbolic", "app.playpos", "Play from position (F2)"));
  gtk_box_append(GTK_BOX(transport), actionbutton("media-playlist-repeat-symbolic", "app.playpattern", "Play pattern (F3)"));
  gtk_box_append(GTK_BOX(transport), actionbutton("media-playback-stop-symbolic", "app.stop", "Stop (F4)"));
  adw_header_bar_pack_start(ADW_HEADER_BAR(header), transport);

  followbutton = gtk_toggle_button_new();
  gtk_button_set_icon_name(GTK_BUTTON(followbutton), "find-location-symbolic");
  gtk_widget_set_tooltip_text(followbutton, "Follow playback (Shift+F1–F3)");
  gtk_widget_set_focusable(followbutton, FALSE);
  g_signal_connect(followbutton, "toggled", G_CALLBACK(onfollowtoggled), NULL);
  adw_header_bar_pack_start(ADW_HEADER_BAR(header), followbutton);

  {
    GtkWidget *loopbutton = gtk_toggle_button_new();
    gtk_button_set_icon_name(GTK_BUTTON(loopbutton), "media-playlist-repeat-song-symbolic");
    gtk_actionable_set_action_name(GTK_ACTIONABLE(loopbutton), "app.loop");
    gtk_widget_set_tooltip_text(loopbutton, "Loop: repeat the patterns playing instead of moving on. "
      "With rows marked, Play Pattern (F3) loops just those rows.");
    gtk_widget_set_focusable(loopbutton, FALSE);
    adw_header_bar_pack_start(ADW_HEADER_BAR(header), loopbutton);
  }

  {
    GtkWidget *undobox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(undobox, "linked");
    gtk_box_append(GTK_BOX(undobox), actionbutton("edit-undo-symbolic", "app.undo", "Undo (Ctrl+Z)"));
    gtk_box_append(GTK_BOX(undobox), actionbutton("edit-redo-symbolic", "app.redo", "Redo (Ctrl+Shift+Z)"));
    adw_header_bar_pack_start(ADW_HEADER_BAR(header), undobox);
    adw_header_bar_pack_start(ADW_HEADER_BAR(header), actionbutton("go-previous-symbolic", "app.back",
      "Go back after jumping to table or instrument data (Alt+Left)"));
  }

  windowtitle = adw_window_title_new("GoatTracker", "Untitled");
  adw_header_bar_set_title_widget(ADW_HEADER_BAR(header), windowtitle);

  gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(menubutton), "open-menu-symbolic");
  gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(menubutton), menu);
  gtk_widget_set_tooltip_text(menubutton, "Main Menu");
  gtk_widget_set_focusable(menubutton, FALSE);
  adw_header_bar_pack_end(ADW_HEADER_BAR(header), menubutton);
  g_object_unref(menu);
  return header;
}

static GtkWidget *buildtoolbar(void)
{
  static const char *keymodes[] = {"Protracker", "DMC", "Janko", NULL};
  static const char *sids[] = {"6581", "8580", NULL};
  static const char *speeds[] = {"25 Hz", "1×", "2×", "3×", "4×", "5×", "6×", "7×", "8×",
    "9×", "10×", "11×", "12×", "13×", "14×", "15×", "16×", NULL};
  GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

  gtk_widget_add_css_class(bar, "toolbar");

  recordbutton = gtk_toggle_button_new();
  {
    GtkWidget *content = adw_button_content_new();
    adw_button_content_set_icon_name(ADW_BUTTON_CONTENT(content), "media-record-symbolic");
    adw_button_content_set_label(ADW_BUTTON_CONTENT(content), "Edit");
    gtk_button_set_child(GTK_BUTTON(recordbutton), content);
  }
  gtk_widget_set_tooltip_text(recordbutton, "Edit mode: typed notes are entered into the pattern (Space toggles jam mode)");
  gtk_widget_set_focusable(recordbutton, FALSE);
  g_signal_connect(recordbutton, "toggled", G_CALLBACK(onrecordtoggled), NULL);
  gtk_box_append(GTK_BOX(bar), recordbutton);

  octavespin = spin(0, 7, G_CALLBACK(onoctavechanged), "Octave for note entry (/ and *)");
  gtk_box_append(GTK_BOX(bar), labelled("Octave", octavespin));
  stepspin = spin(2, MAX_PATTROWS, G_CALLBACK(onstepchanged), "Pattern row highlighting step (Shift+M/N)");
  gtk_box_append(GTK_BOX(bar), labelled("Highlight", stepspin));
  keymodedrop = dropdown(keymodes, G_CALLBACK(onkeymodechanged), "Note entry keyboard layout");
  gtk_box_append(GTK_BOX(bar), labelled("Keys", keymodedrop));

  gtk_widget_set_hexpand(spacer, TRUE);
  gtk_box_append(GTK_BOX(bar), spacer);

  siddrop = dropdown(sids, G_CALLBACK(onsidchanged), "Emulated SID model (Shift+F8)");
  gtk_box_append(GTK_BOX(bar), labelled("SID", siddrop));
  speeddrop = dropdown(speeds, G_CALLBACK(onspeedchanged), "Playroutine speed multiplier (Shift+F5/F6)");
  gtk_box_append(GTK_BOX(bar), labelled("Speed", speeddrop));
  volumescale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 5);
  gtk_scale_set_draw_value(GTK_SCALE(volumescale), FALSE);
  gtk_widget_set_size_request(volumescale, 90, -1);
  gtk_widget_set_focusable(volumescale, FALSE);
  gtk_widget_set_tooltip_text(volumescale, "Playback volume (doesn't affect exported WAV files)");
  g_signal_connect(volumescale, "value-changed", G_CALLBACK(onvolumechanged), NULL);
  {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_box_append(GTK_BOX(box), gtk_image_new_from_icon_name("audio-volume-high-symbolic"));
    gtk_box_append(GTK_BOX(box), volumescale);
    gtk_box_append(GTK_BOX(bar), box);
  }
  hrbutton = gtk_button_new_with_label("HR 0F00");
  gtk_widget_set_tooltip_text(hrbutton, "Hard restart ADSR (Shift+F7)");
  gtk_widget_set_focusable(hrbutton, FALSE);
  g_signal_connect(hrbutton, "clicked", G_CALLBACK(onhrclicked), NULL);
  gtk_box_append(GTK_BOX(bar), hrbutton);
  return bar;
}

static GtkWidget *buildstatusbar(void)
{
  GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 24);

  gtk_widget_add_css_class(bar, "toolbar");
  statusplay = gtk_label_new("");
  statuspos = gtk_label_new("");
  statusinfo = gtk_label_new("");
  statusmode = gtk_label_new("");
  gtk_widget_add_css_class(statuspos, "monospace");
  gtk_widget_add_css_class(statusplay, "monospace");
  // What the item under the cursor means
  gtk_widget_set_hexpand(statusinfo, TRUE);
  gtk_label_set_xalign(GTK_LABEL(statusinfo), 0);
  gtk_label_set_ellipsize(GTK_LABEL(statusinfo), PANGO_ELLIPSIZE_END);
  gtk_widget_add_css_class(statusinfo, "dim-label");
  gtk_box_append(GTK_BOX(bar), statusplay);
  gtk_box_append(GTK_BOX(bar), statuspos);
  gtk_box_append(GTK_BOX(bar), statusinfo);
  gtk_box_append(GTK_BOX(bar), statusmode);
  return bar;
}

static GtkWidget *framed(GtkWidget *child)
{
  GtkWidget *frame = gtk_frame_new(NULL);

  gtk_frame_set_child(GTK_FRAME(frame), child);
  return frame;
}

static GtkWidget *buildorderpanel(void)
{
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  GtkWidget *title = gtk_label_new("Orderlist");

  gtk_widget_add_css_class(title, "heading");
  gtk_label_set_xalign(GTK_LABEL(title), 0);
  gtk_widget_set_hexpand(title, TRUE);
  subtunespin = spin(0, MAX_SONGS - 1, G_CALLBACK(onsubtunechanged), "Subtune (< and > in the orderlist)");
  gtk_box_append(GTK_BOX(header), title);
  gtk_box_append(GTK_BOX(header), labelled("Subtune", subtunespin));
  gtk_box_append(GTK_BOX(box), header);
  gtk_box_append(GTK_BOX(box), framed(ordergrid));
  gtk_widget_set_margin_start(box, 6);
  gtk_widget_set_margin_top(box, 6);
  gtk_widget_set_margin_bottom(box, 6);
  return box;
}

static gboolean oncloserequest(GtkWindow *window, gpointer data)
{
  undo_checkpoint(0);
  if (undo_isdirty()) ui_confirmdiscard(ui_quitnow);
  else ui_quitnow();
  return TRUE;
}

static void onfullscreenchanged(GObject *object, GParamSpec *pspec, gpointer data)
{
  win_fullscreen = gtk_window_is_fullscreen(mainwindow);
}

static gboolean onsignal(gpointer data)
{
  ui_quitnow();
  return G_SOURCE_REMOVE;
}

//
// Settings file
//

static char *settingspath(void)
{
  return g_build_filename(g_get_home_dir(), ".goattrk", "gtkedition.ini", NULL);
}

static void loadsettings(void)
{
  GKeyFile *keys = g_key_file_new();
  char *path = settingspath();

  if (g_key_file_load_from_file(keys, path, G_KEY_FILE_NONE, NULL))
  {
    GError *error = NULL;
    int v = g_key_file_get_integer(keys, "editor", "backup-interval", &error);
    if (!error) settings_backupinterval = CLAMP(v, 0, 3600);
    g_clear_error(&error);
    v = g_key_file_get_boolean(keys, "view", "describe-tables", &error);
    if (!error) settings_decodetables = v;
    g_clear_error(&error);
    v = g_key_file_get_integer(keys, "sound", "volume", &error);
    if (!error) mastervolume = CLAMP(v, 0, 100);
    g_clear_error(&error);
    v = g_key_file_get_integer(keys, "sound", "detune", &error);
    if (!error) ui_setdetune(CLAMP(v, -100, 100));
    g_clear_error(&error);
    v = g_key_file_get_boolean(keys, "export", "patterns-in-play-order", &error);
    if (!error) packplayorder = v;
    g_clear_error(&error);
    v = g_key_file_get_boolean(keys, "editor", "auto-next-pattern", &error);
    if (!error) autonextpattern = v;
    g_clear_error(&error);
    {
      char *name = g_key_file_get_string(keys, "sound", "midi-input", NULL);
      if ((name) && (name[0]) && (!midi_setinput(name)))
        g_printerr("goattrk2: MIDI input \"%s\" is not available\n", name);
      g_free(name);
    }
    v = g_key_file_get_boolean(keys, "view", "piano", &error);
    if (!error) settings_showpiano = v;
    g_clear_error(&error);
    v = g_key_file_get_boolean(keys, "view", "sid-registers", &error);
    if (!error) settings_showsidstate = v;
    g_clear_error(&error);
  }
  g_key_file_unref(keys);
  g_free(path);
}

static void savesettings(void)
{
  GKeyFile *keys = g_key_file_new();
  char *path = settingspath();
  char *dir = g_path_get_dirname(path);

  g_key_file_load_from_file(keys, path, G_KEY_FILE_KEEP_COMMENTS, NULL);
  g_key_file_set_integer(keys, "editor", "backup-interval", settings_backupinterval);
  g_key_file_set_boolean(keys, "view", "describe-tables", settings_decodetables);
  g_key_file_set_integer(keys, "sound", "volume", mastervolume);
  g_key_file_set_integer(keys, "sound", "detune", sid_detune);
  g_key_file_set_string(keys, "sound", "midi-input", settings_midiinput);
  g_key_file_set_boolean(keys, "editor", "auto-next-pattern", autonextpattern);
  g_key_file_set_boolean(keys, "export", "patterns-in-play-order", packplayorder);
  g_key_file_set_boolean(keys, "view", "piano", settings_showpiano);
  g_key_file_set_boolean(keys, "view", "sid-registers", settings_showsidstate);
  g_mkdir_with_parents(dir, 0755);
  g_key_file_save_to_file(keys, path, NULL);
  g_key_file_unref(keys);
  g_free(dir);
  g_free(path);
}

static gboolean onbackuptimer(gpointer data)
{
  ui_backup();
  return G_SOURCE_CONTINUE;
}

// (Re)start the backup timer after the interval changed
void ui_backupschanged(void)
{
  if (backuptimer) g_source_remove(backuptimer);
  backuptimer = 0;
  if (settings_backupinterval > 0)
    backuptimer = g_timeout_add_seconds(settings_backupinterval, onbackuptimer, NULL);
}

// Drag and drop: a song opens (or asks to save first), an instrument loads
// into the current instrument
static char *droppedpath = NULL;

static void opendropped(void)
{
  if (droppedpath) ui_opensongpath(droppedpath, 0);
}

static gboolean ondrop(GtkDropTarget *target, const GValue *value, double x, double y, gpointer data)
{
  GSList *files = g_value_get_boxed(value);
  char *path, *lower;
  int isinstrument;

  if ((!files) || (!files->data)) return FALSE;
  path = g_file_get_path(files->data);
  if (!path) return FALSE;

  lower = g_ascii_strdown(path, -1);
  isinstrument = g_str_has_suffix(lower, ".ins");
  g_free(lower);
  if (isinstrument) ui_loadinstrumentpath(path);
  else
  {
    g_free(droppedpath);
    droppedpath = g_strdup(path);
    undo_checkpoint(0);
    if (undo_isdirty()) ui_confirmdiscard(opendropped);
    else opendropped();
  }
  g_free(path);
  return TRUE;
}

void ui_quitnow(void)
{
  if (quitting) return;
  quitting = 1;
  savesettings();
  goattrk2_shutdown();
  g_application_quit(G_APPLICATION(app));
}

// The window opens at 1280x820, grown in step with the text size (-w2…-w4)
// but kept within the screen
static void setdefaultsize(void)
{
  double factor = (10 + 2 * (CLAMP(bigwindow, 1, 4) - 1)) / 10.0;
  int width = (int)(1280 * factor), height = (int)(820 * factor);
  GListModel *monitors = gdk_display_get_monitors(gtk_widget_get_display(GTK_WIDGET(mainwindow)));
  GdkMonitor *monitor = g_list_model_get_item(monitors, 0);

  if (monitor)
  {
    GdkRectangle area;

    gdk_monitor_get_geometry(monitor, &area);
    width = MIN(width, area.width * 95 / 100);
    height = MIN(height, area.height * 90 / 100);
    g_object_unref(monitor);
  }
  gtk_window_set_default_size(mainwindow, width, height);
}

static void onactivate(GtkApplication *application, gpointer data)
{
  GtkWidget *window = adw_application_window_new(application);
  GtkWidget *content = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
  GtkWidget *editors = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
  GtkWidget *top = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
  GtkWidget *bottom = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  GtkEventController *controller;

  mainwindow = GTK_WINDOW(window);
  adw_style_manager_set_color_scheme(adw_style_manager_get_default(), ADW_COLOR_SCHEME_PREFER_DARK);
  gtk_window_set_title(mainwindow, "GoatTracker");
  setdefaultsize();
  loadsettings();
  addactions();

  // The grids must exist before the panels that refer to them
  grid_pattern_new();
  grid_orderlist_new();
  grid_tables_new();
  instrpanel = panel_instruments_new();
  songpanel = panel_songinfo_new();

  // Orderlists on the left, the pattern editor in the middle
  gtk_paned_set_start_child(GTK_PANED(top), buildorderpanel());
  gtk_paned_set_resize_start_child(GTK_PANED(top), FALSE);
  gtk_paned_set_shrink_start_child(GTK_PANED(top), FALSE);
  {
    // The SID registers, when shown, sit under the pattern editor
    GtkWidget *middle = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *pattframe = framed(patterngrid);

    gtk_widget_set_vexpand(pattframe, TRUE);
    gtk_box_append(GTK_BOX(middle), pattframe);
    {
      // Scrolls sideways if larger text makes it wider than the pattern editor
      GtkWidget *scroll = gtk_scrolled_window_new();

      gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
      gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(scroll), TRUE);
      gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroll), TRUE);
      gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), monitor_sidview_new());
      gtk_box_append(GTK_BOX(middle), scroll);
      gtk_widget_set_visible(scroll, settings_showsidstate);
    }
    gtk_paned_set_end_child(GTK_PANED(top), middle);
  }
  gtk_paned_set_shrink_end_child(GTK_PANED(top), FALSE);

  // Tables and song information below them
  {
    // The tables scroll sideways when the window is too narrow for them
    // (with row descriptions they are wider than everything else)
    GtkWidget *scroll = gtk_scrolled_window_new();
    GtkWidget *frame;

    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
    gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(scroll), TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), tablegrid);
    frame = framed(scroll);
    gtk_widget_set_hexpand(frame, TRUE);
    gtk_box_append(GTK_BOX(bottom), frame);
  }
  gtk_box_append(GTK_BOX(bottom), songpanel);
  gtk_widget_set_margin_start(bottom, 6);
  gtk_widget_set_margin_bottom(bottom, 6);
  gtk_paned_set_start_child(GTK_PANED(editors), top);
  gtk_paned_set_end_child(GTK_PANED(editors), bottom);
  gtk_paned_set_resize_end_child(GTK_PANED(editors), FALSE);
  gtk_paned_set_shrink_end_child(GTK_PANED(editors), FALSE);
  gtk_paned_set_position(GTK_PANED(editors), 520);

  // The instrument column runs the full height on the right
  gtk_paned_set_start_child(GTK_PANED(content), editors);
  gtk_paned_set_shrink_start_child(GTK_PANED(content), FALSE);
  gtk_paned_set_end_child(GTK_PANED(content), instrpanel);
  gtk_paned_set_resize_end_child(GTK_PANED(content), FALSE);
  gtk_paned_set_shrink_end_child(GTK_PANED(content), FALSE);

  toastoverlay = adw_toast_overlay_new();
  adw_toast_overlay_set_child(ADW_TOAST_OVERLAY(toastoverlay), content);

  toolbarview = adw_toolbar_view_new();
  adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbarview), buildheaderbar());
  adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbarview), buildtoolbar());
  adw_toolbar_view_add_bottom_bar(ADW_TOOLBAR_VIEW(toolbarview), monitor_piano_new());
  adw_toolbar_view_add_bottom_bar(ADW_TOOLBAR_VIEW(toolbarview), buildstatusbar());
  adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbarview), toastoverlay);
  adw_application_window_set_content(ADW_APPLICATION_WINDOW(window), toolbarview);

  // Editor keys are handled before GTK's own key bindings
  controller = gtk_event_controller_key_new();
  gtk_event_controller_set_propagation_phase(controller, GTK_PHASE_CAPTURE);
  g_signal_connect(controller, "key-pressed", G_CALLBACK(onkeypressed), NULL);
  g_signal_connect(controller, "key-released", G_CALLBACK(onkeyreleased), NULL);
  gtk_widget_add_controller(window, controller);
  g_signal_connect(window, "notify::is-active", G_CALLBACK(onactivechanged), NULL);

  g_signal_connect(window, "close-request", G_CALLBACK(oncloserequest), NULL);
  g_signal_connect(window, "notify::fullscreened", G_CALLBACK(onfullscreenchanged), NULL);

  {
    GtkDropTarget *target = gtk_drop_target_new(GDK_TYPE_FILE_LIST, GDK_ACTION_COPY);
    g_signal_connect(target, "drop", G_CALLBACK(ondrop), NULL);
    gtk_widget_add_controller(window, GTK_EVENT_CONTROLLER(target));
  }

  if (win_fullscreen) gtk_window_fullscreen(mainwindow);
  undo_reset();
  gtk_window_present(mainwindow);
  ui_refresh();
  ui_focuseditmode();
  g_timeout_add(20, tick, NULL);
  ui_backupschanged();

  if (soundinitfailed) ui_showsoundfailure();
  else ui_afterstartupload();
  if (starthelp) ui_help(0);
}

int main(int argc, char **argv)
{
  int status = goattrk2_init(argc, argv);

  // goattrk2_init() returns an exit status when there is nothing to edit
  // (for example after printing the usage)
  if (status >= 0) return status;

  app = adw_application_new(APPID, G_APPLICATION_NON_UNIQUE);
  g_signal_connect(app, "activate", G_CALLBACK(onactivate), NULL);
  g_unix_signal_add(SIGINT, onsignal, NULL);
  g_unix_signal_add(SIGTERM, onsignal, NULL);
  // The command line was already handled by goattrk2_init()
  status = g_application_run(G_APPLICATION(app), 1, argv);
  g_object_unref(app);
  return status;
}
