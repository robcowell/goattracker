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
static GtkWidget *statusplay, *statuspos, *statusmode;
static int syncing = 0;
static int wasplaying = 0;
static int quitting = 0;

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

static void runkey(unsigned raw, unsigned ascii, int shift, int allowhex)
{
  int oldmode = editmode;

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

  undo_markcursor();
  if ((editmode != EDIT_ORDERLIST) || (!orderlistcursor()))
    docommand();
  undo_checkpoint(0);

  if (editmode != oldmode) ui_focuseditmode();
  ui_refresh();
}

void ui_runkey(unsigned raw, unsigned ascii, int shift)
{
  runkey(raw, ascii, shift, 1);
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

  // Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y undo and redo everywhere, text fields
  // included (their own undo is off). Shift+Z still cycles auto-advance.
  if ((state & GDK_CONTROL_MASK) && ((raw == KEY_Z) || (raw == KEY_Y)))
  {
    if ((raw == KEY_Y) || (state & GDK_SHIFT_MASK)) ui_redo();
    else ui_undo();
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

  sprintf(buf, "%s  %02d:%02d", isplaying() ? "Playing" : "Stopped", timemin, timesec);
  gtk_label_set_text(GTK_LABEL(statusplay), buf);

  for (c = 0; c < MAX_CHN; c++)
  {
    int chnpos = MAX(chn[c].songptr - 1, 0);
    int chnrow = chn[c].pattptr / 4;
    if (chnrow > pattlen[chn[c].pattnum]) chnrow = pattlen[chn[c].pattnum];
    len += sprintf(buf + len, "%sCh%d %02X/%02d", c ? "   " : "", c + 1, chnpos, chnrow);
  }
  gtk_label_set_text(GTK_LABEL(statuspos), buf);

  {
    static const char *advance[] = {"", "  ·  Auto-advance", "  ·  Auto-advance (all)"};
    sprintf(buf, "%s%s", recordmode ? "Edit mode" : "Jam mode", advance[autoadvance % 3]);
    gtk_label_set_text(GTK_LABEL(statusmode), buf);
  }
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

static void syncundoactions(void)
{
  g_simple_action_set_enabled(G_SIMPLE_ACTION(g_action_map_lookup_action(G_ACTION_MAP(app), "undo")), undo_canundo());
  g_simple_action_set_enabled(G_SIMPLE_ACTION(g_action_map_lookup_action(G_ACTION_MAP(app), "redo")), undo_canredo());
}

// Called after a native widget changed the song: record it and show it
void ui_edited(void)
{
  ui_settitle();
  syncundoactions();
}

static void afterundo(void)
{
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

static gboolean tick(gpointer data)
{
  int playing = isplaying();

  if ((playing) || (wasplaying))
  {
    followplayupdate();
    grid_redraw();
    updatestatus();
  }
  wasplaying = playing;
  return G_SOURCE_CONTINUE;
}

//
// Toolbar handlers
//

static void restartsound(void)
{
  if (!sound_init(b, mr, writer, hardsid, sidmodel, ntsc, multiplier, catweasel, interpolate, customclockrate))
    ui_toast("Sound output could not be restarted");
}

static void onrecordtoggled(GtkToggleButton *button, gpointer data)
{
  if (syncing) return;
  recordmode = gtk_toggle_button_get_active(button);
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
  restartsound();
}

static void onspeedchanged(GObject *drop, GParamSpec *pspec, gpointer data)
{
  if (syncing) return;
  multiplier = gtk_drop_down_get_selected(GTK_DROP_DOWN(drop));
  restartsound();
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
  else if (!strcmp(name, "save")) ui_savesong();
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
    {NULL}
  };
  static const MENUITEM file[] = {
    {"New Song…", "app.new", "<Shift>Escape"},
    {"Open Song…", "app.open", "F10"},
    {"Merge Song…", "app.merge", "<Shift>F10"},
    {"Save Song…", "app.save", "F11"},
    {NULL}
  };
  static const MENUITEM instrument[] = {
    {"Load Instrument…", "app.loadinstr", NULL},
    {"Save Instrument…", "app.saveinstr", NULL},
    {"Pack, Relocate & Export…", "app.export", "F9"},
    {NULL}
  };
  static const MENUITEM playback[] = {
    {"Play from Beginning", "app.play", "F1"},
    {"Play from Position", "app.playpos", "F2"},
    {"Play Pattern", "app.playpattern", "F3"},
    {"Stop", "app.stop", "F4"},
    {"Mute Channel", "app.mute", "<Shift>F4"},
    {NULL}
  };
  static const MENUITEM view[] = {
    {"Larger Text", "app.zoomin", NULL},
    {"Smaller Text", "app.zoomout", NULL},
    {"Hexadecimal Row Numbers", "app.hexrows", NULL},
    {"Dots for Empty Fields", "app.dots", NULL},
    {"Fullscreen", "app.fullscreen", "<Alt>Return"},
    {NULL}
  };
  static const MENUITEM help[] = {
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
  static const char *simple[] = {"undo", "redo", "new", "open", "merge", "save", "loadinstr", "saveinstr", "export",
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
    GtkWidget *undobox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(undobox, "linked");
    gtk_box_append(GTK_BOX(undobox), actionbutton("edit-undo-symbolic", "app.undo", "Undo (Ctrl+Z)"));
    gtk_box_append(GTK_BOX(undobox), actionbutton("edit-redo-symbolic", "app.redo", "Redo (Ctrl+Shift+Z)"));
    adw_header_bar_pack_start(ADW_HEADER_BAR(header), undobox);
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
  statusmode = gtk_label_new("");
  gtk_widget_add_css_class(statuspos, "monospace");
  gtk_widget_add_css_class(statusplay, "monospace");
  gtk_widget_set_hexpand(statuspos, TRUE);
  gtk_label_set_xalign(GTK_LABEL(statuspos), 0);
  gtk_box_append(GTK_BOX(bar), statusplay);
  gtk_box_append(GTK_BOX(bar), statuspos);
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

void ui_quitnow(void)
{
  if (quitting) return;
  quitting = 1;
  goattrk2_shutdown();
  g_application_quit(G_APPLICATION(app));
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
  gtk_window_set_default_size(mainwindow, 1280, 820);
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
  gtk_paned_set_end_child(GTK_PANED(top), framed(patterngrid));
  gtk_paned_set_shrink_end_child(GTK_PANED(top), FALSE);

  // Tables and song information below them
  gtk_box_append(GTK_BOX(bottom), framed(tablegrid));
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
  adw_toolbar_view_add_bottom_bar(ADW_TOOLBAR_VIEW(toolbarview), buildstatusbar());
  adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbarview), toastoverlay);
  adw_application_window_set_content(ADW_APPLICATION_WINDOW(window), toolbarview);

  // Editor keys are handled before GTK's own key bindings
  controller = gtk_event_controller_key_new();
  gtk_event_controller_set_propagation_phase(controller, GTK_PHASE_CAPTURE);
  g_signal_connect(controller, "key-pressed", G_CALLBACK(onkeypressed), NULL);
  gtk_widget_add_controller(window, controller);

  g_signal_connect(window, "close-request", G_CALLBACK(oncloserequest), NULL);
  g_signal_connect(window, "notify::fullscreened", G_CALLBACK(onfullscreenchanged), NULL);

  if (win_fullscreen) gtk_window_fullscreen(mainwindow);
  undo_reset();
  gtk_window_present(mainwindow);
  ui_refresh();
  ui_focuseditmode();
  g_timeout_add(20, tick, NULL);

  if (soundinitfailed) ui_showsoundfailure();
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
