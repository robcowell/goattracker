//
// SidMonkey: main window
//
// A beginner-friendly SID composer on the GoatTracker engine. goattrk2.c
// still provides the engine's settings, globals and startup
// (goattrk2_init(), which reads ~/.goattrk/goattrk2.cfg and the command
// line); this file replaces the tracker's GTK user interface.
//

#include <glib-unix.h>
#include "sm.h"

#define APPID "io.github.robcowell.SidMonkey"

GtkWindow *sm_window;
static AdwApplication *app;
static AdwToastOverlay *toasts;
static AdwWindowTitle *title;
static GtkButton *playbutton;
static GtkSpinButton *tunespin;
static GtkLabel *statuslabel, *timelabel, *limitslabel, *filterlabel;
static GtkWidget *filterbutton;
static int subtune;
static int wasplaying;
static double songseconds = -1;
static int quitting;

// Where the song was opened from or last saved (NULL for a new song)
static char *songfile;

// What to do once unsaved changes are saved or discarded
static void (*pending)(void);

static void savesongas(void);
static void savesettings(void);
static void togglepause(void);

// goattrk2.c's tracker key commands call these. SidMonkey never runs those
// commands, but they are linked in with the engine's startup code.
void ui_quit(void) {}
void ui_clear(void) {}
void ui_help(int context) {}
void ui_editadsr(void) {}
void ui_relocator(void) {}
void ui_load(int merge) {}
void ui_save(void) {}

const char *sm_toutf8(const char *latin1)
{
  static char buf[256];
  char *utf8 = g_convert(latin1, -1, "UTF-8", "ISO-8859-1", NULL, NULL, NULL);

  g_strlcpy(buf, utf8 ? utf8 : "", sizeof buf);
  g_free(utf8);
  return buf;
}

void sm_toast(const char *message)
{
  adw_toast_overlay_add_toast(toasts, adw_toast_new(message));
}

void sm_setstatus(const char *text)
{
  gtk_label_set_text(statuslabel, text);
}

int sm_subtune(void)
{
  return subtune;
}

// Subtunes in the song, counted as the song file stores them
int sm_subtunes(void)
{
  int c;

  for (c = MAX_SONGS - 1; c > 0; c--)
    if ((songlen[c][0]) && (songlen[c][1]) && (songlen[c][2])) break;
  return c + 1;
}

static void formattime(double seconds, char *buf, int size)
{
  int s = (int)seconds;
  snprintf(buf, size, "%d:%02d", s / 60, s % 60);
}

static void updatetime(void)
{
  char now[16], total[16], buf[48];

  formattime(isplaying() ? timemin * 60 + timesec : 0, now, sizeof now);
  if (songseconds >= 0) formattime(songseconds, total, sizeof total);
  else strcpy(total, "--:--");
  snprintf(buf, sizeof buf, "%s / %s", now, total);
  gtk_label_set_text(timelabel, buf);
}

// Measuring the song (a silent render) stops playback and resets the
// voices, cutting off any note being played. So after an edit it waits
// until nothing has changed for a moment, the song is stopped and no note
// is held.
static guint measuretimer;

static void measurenow(void)
{
  songseconds = render_songlength(subtune, 15 * 60);
  updatetime();
}

static gboolean onmeasuretimer(gpointer data)
{
  if ((isplaying()) || (jam_active()) || (roll_busy())) return G_SOURCE_CONTINUE;
  measuretimer = 0;
  measurenow();
  return G_SOURCE_REMOVE;
}

static void measuresong(void)
{
  if (measuretimer) g_source_remove(measuretimer);
  measuretimer = g_timeout_add(1000, onmeasuretimer, NULL);
}

static void updateplaybutton(void)
{
  int playing = isplaying();

  gtk_button_set_icon_name(playbutton, playing ? "media-playback-stop-symbolic" : "media-playback-start-symbolic");
  gtk_widget_set_tooltip_text(GTK_WIDGET(playbutton), playing ? "Stop (Space)" : "Play the song (Space)");
}

static const char *songtitle(void)
{
  if (strlen(songname)) return sm_toutf8(songname);
  if (songfile)
  {
    static char base[MAX_PATHNAME];
    char *b = g_path_get_basename(songfile);
    g_strlcpy(base, b, sizeof base);
    g_free(b);
    return base;
  }
  return "Untitled Song";
}

static void setaction(const char *name, int enabled)
{
  GAction *a = g_action_map_lookup_action(G_ACTION_MAP(sm_window), name);
  if (a) g_simple_action_set_enabled(G_SIMPLE_ACTION(a), enabled);
}

static void updatetitle(void)
{
  char buf[MAX_PATHNAME + 8];

  snprintf(buf, sizeof buf, "%s%s", undo_isdirty() ? "• " : "", songtitle());
  adw_window_title_set_title(title, buf);
  adw_window_title_set_subtitle(title, strlen(authorname) ? sm_toutf8(authorname) : "");
  setaction("undo", undo_canundo());
  setaction("redo", undo_canredo());
}

// What the song uses of the chip's and the song file's limits, and whether
// instruments compete for the one filter
static void updatehealth(void)
{
  static unsigned char used[MAX_PATT];
  int masks[MAX_INSTR];
  int s, c, i, patterns = 0, instruments = 0, filtered = 0, first = 0;
  char buf[400];

  memset(used, 0, sizeof used);
  for (s = 0; s < MAX_SONGS; s++)
    for (c = 0; c < MAX_CHN; c++)
      for (i = 0; i < songlen[s][c]; i++)
        if (songorder[s][c][i] < MAX_PATT) used[songorder[s][c][i]] = 1;
  for (i = 0; i < MAX_PATT; i++) patterns += used[i];
  recipe_voicemasks(masks);
  for (i = 1; i < MAX_INSTR; i++)
  {
    int voices = masks[i];
    if ((voices) || (instr[i].name[0])) instruments++;
    if ((voices) && (instr[i].ptr[FTBL]))
    {
      if (!filtered) first = i;
      filtered++;
    }
  }
  snprintf(buf, sizeof buf, "%d of %d patterns \u00B7 %d of %d instruments", patterns, MAX_PATT, instruments,
    MAX_INSTR - 1);
  gtk_label_set_text(limitslabel, buf);
  gtk_widget_set_visible(filterbutton, filtered > 1);
  if (filtered > 1)
  {
    snprintf(buf, sizeof buf, "%d instruments (%02X and others) use the filter, but the SID has only one. "
      "Whichever played last controls it, for all the voices it filters, so these instruments may change each "
      "other's sound. Turning the filter off on all but one of them avoids that.", filtered, first);
    gtk_label_set_text(filterlabel, buf);
  }
}

// After loading a song or switching subtunes
void sm_songchanged(void)
{
  if (subtune >= sm_subtunes()) subtune = 0;
  gtk_spin_button_set_range(tunespin, 1, sm_subtunes());
  gtk_spin_button_set_value(tunespin, subtune + 1);
  gtk_widget_set_visible(gtk_widget_get_parent(GTK_WIDGET(tunespin)), sm_subtunes() > 1);
  updatetitle();
  roll_refreshinstruments();
  sound_refresh();
  arrange_songchanged();
  sm_setstatus("Click a clip to select it, drag it to move it (hold Ctrl to copy), right-click for more.");
  updatehealth();
  if (measuretimer) g_source_remove(measuretimer);
  measuretimer = 0;
  if (isplaying()) measuresong();
  else measurenow();
  updatetime();
  updateplaybutton();
}

// After an edit of the song data
void sm_edited(void)
{
  // A recipe instrument's filter follows the voices it plays on
  host_lock();
  recipe_updatefilters();
  host_unlock();
  undo_checkpoint(0);
  updatetitle();
  updatehealth();
  measuresong();
}

static void undoredo(int redo)
{
  if (!(redo ? undo_redo() : undo_undo())) return;
  arrange_refresh();
  roll_refresh();
  roll_refreshinstruments();
  sound_refresh();
  updatetitle();
  updatehealth();
  measuresong();
}

void sm_togglepause(void)
{
  togglepause();
}

static void togglepause(void)
{
  if (isplaying()) host_stop();
  else host_play(subtune);
  updateplaybutton();
}

static void ontunechanged(GtkSpinButton *spin, gpointer data)
{
  int t = (int)gtk_spin_button_get_value(spin) - 1;

  if (t == subtune) return;
  host_stop();
  subtune = t;
  sm_songchanged();
}

//
// Files
//

static void setsongfile(const char *path)
{
  g_free(songfile);
  songfile = path ? g_canonicalize_filename(path, NULL) : NULL;
}

static void reportload(int result)
{
  switch (result)
  {
    case LOAD_OK: break;
    case LOAD_DAMAGED: sm_toast("This song file is damaged and could not be opened"); break;
    case LOAD_MULTICHANNEL: sm_toast("This song is for a 6-voice tracker; SidMonkey plays the 3 voices of one SID"); break;
    case HOST_LOAD_BADPATH: sm_toast("The file name or folder name is too long"); break;
    default: sm_toast("This is not a GoatTracker song, or it could not be read"); break;
  }
}

static void opensong(const char *path)
{
  int result = host_loadsong(path);

  reportload(result);
  if (result != LOAD_OK) return;
  setsongfile(path);
  subtune = 0;
  sm_songchanged();
}

// A song made in code (a template) replaces the current one
void sm_newsong(void)
{
  undo_reset();
  setsongfile(NULL);
  subtune = 0;
  sm_songchanged();
}

void sm_opensong(const char *path)
{
  opensong(path);
}

static void newsong(void)
{
  host_stop();
  clearsong(1, 1, 1, 1, 1);
  undo_reset();
  setsongfile(NULL);
  subtune = 0;
  sm_songchanged();
}

static void writesong(const char *path)
{
  if (!host_savesong(path))
  {
    sm_toast("The song could not be saved there (file names can be up to 59 characters)");
    pending = NULL;
    return;
  }
  setsongfile(path);
  updatetitle();
  sm_toast("Saved");
  if (pending)
  {
    void (*next)(void) = pending;
    pending = NULL;
    next();
  }
}

static void savesong_(void)
{
  if (songfile) writesong(songfile);
  else savesongas();
}

static GListModel *songfilters(void)
{
  GtkFileFilter *filter = gtk_file_filter_new();
  GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);

  gtk_file_filter_set_name(filter, "GoatTracker Songs");
  gtk_file_filter_add_suffix(filter, "sng");
  gtk_file_filter_add_suffix(filter, "SNG");
  g_list_store_append(filters, filter);
  g_object_unref(filter);
  return G_LIST_MODEL(filters);
}

static void onsavechosen(GObject *source, GAsyncResult *result, gpointer data)
{
  GFile *file = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(source), result, NULL);
  char *path = file ? g_file_get_path(file) : NULL;

  if (path)
  {
    // The engine adds .sng to names without an extension; do it here so
    // that the remembered path is the file that was written
    if (!strchr(strrchr(path, '/') ? strrchr(path, '/') : path, '.'))
    {
      char *withext = g_strconcat(path, ".sng", NULL);
      g_free(path);
      path = withext;
    }
    writesong(path);
  }
  else pending = NULL;
  g_free(path);
  if (file) g_object_unref(file);
}

static void savesongas(void)
{
  GtkFileDialog *dialog = gtk_file_dialog_new();
  GListModel *filters = songfilters();
  char name[MAX_STR * 2 + 8];

  gtk_file_dialog_set_filters(dialog, filters);
  gtk_file_dialog_set_title(dialog, "Save Song");
  if (songfile)
  {
    GFile *file = g_file_new_for_path(songfile);
    gtk_file_dialog_set_initial_file(dialog, file);
    g_object_unref(file);
  }
  else
  {
    GFile *folder = g_file_new_for_path(strlen(songpath) ? songpath : ".");
    g_strlcpy(name, strlen(songname) ? sm_toutf8(songname) : "Untitled", sizeof name - 4);
    strcat(name, ".sng");
    gtk_file_dialog_set_initial_folder(dialog, folder);
    gtk_file_dialog_set_initial_name(dialog, name);
    g_object_unref(folder);
  }
  gtk_file_dialog_save(dialog, sm_window, NULL, onsavechosen, NULL);
  g_object_unref(filters);
  g_object_unref(dialog);
}

static void ondiscardresponse(AdwAlertDialog *dialog, const char *response, gpointer data)
{
  if (!strcmp(response, "save")) savesong_();
  else if (!strcmp(response, "discard"))
  {
    void (*next)(void) = pending;
    pending = NULL;
    if (next) next();
  }
  else pending = NULL;
}

// Run next now, or after the unsaved changes are saved or discarded
static void confirmdiscard(void (*next)(void))
{
  AdwDialog *dialog;
  char body[MAX_PATHNAME + 64];

  undo_checkpoint(0);
  if (!undo_isdirty())
  {
    next();
    return;
  }
  pending = next;
  snprintf(body, sizeof body, "“%s” has changes that haven't been saved.", songtitle());
  dialog = adw_alert_dialog_new("Save Changes?", body);
  adw_alert_dialog_add_responses(ADW_ALERT_DIALOG(dialog), "cancel", "_Cancel", "discard", "_Discard",
    "save", "_Save", NULL);
  adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(dialog), "discard", ADW_RESPONSE_DESTRUCTIVE);
  adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(dialog), "save", ADW_RESPONSE_SUGGESTED);
  adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(dialog), "save");
  adw_alert_dialog_set_close_response(ADW_ALERT_DIALOG(dialog), "cancel");
  g_signal_connect(dialog, "response", G_CALLBACK(ondiscardresponse), NULL);
  adw_dialog_present(dialog, GTK_WIDGET(sm_window));
}

static void onopened(GObject *source, GAsyncResult *result, gpointer data)
{
  GFile *file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source), result, NULL);
  char *path = file ? g_file_get_path(file) : NULL;

  if (path) opensong(path);
  g_free(path);
  if (file) g_object_unref(file);
}

static void chooseopen(void)
{
  GtkFileDialog *dialog = gtk_file_dialog_new();
  GListModel *filters = songfilters();
  GFile *folder = g_file_new_for_path(strlen(songpath) ? songpath : ".");

  gtk_file_dialog_set_filters(dialog, filters);
  gtk_file_dialog_set_title(dialog, "Open Song");
  gtk_file_dialog_set_initial_folder(dialog, folder);
  gtk_file_dialog_open(dialog, sm_window, NULL, onopened, NULL);
  g_object_unref(folder);
  g_object_unref(filters);
  g_object_unref(dialog);
}

static char *droppedpath;

static void opendropped(void)
{
  if (droppedpath) opensong(droppedpath);
  g_free(droppedpath);
  droppedpath = NULL;
}

static gboolean ondrop(GtkDropTarget *target, const GValue *value, double x, double y, gpointer data)
{
  GFile *file = g_value_get_object(value);
  char *path = file ? g_file_get_path(file) : NULL;

  if (!path) return FALSE;
  g_free(droppedpath);
  droppedpath = path;
  confirmdiscard(opendropped);
  return TRUE;
}

static gboolean tick(gpointer data)
{
  int playing = isplaying();

  arrange_tick();
  roll_tick();
  if (playing || wasplaying) updatetime();
  if (playing != wasplaying) updateplaybutton();
  wasplaying = playing;
  return G_SOURCE_CONTINUE;
}

static void quitnow(void)
{
  if (quitting) return;
  quitting = 1;
  roll_releaseall();
  savesettings();
  goattrk2_shutdown();
  g_application_quit(G_APPLICATION(app));
}

static gboolean oncloserequest(GtkWindow *window, gpointer data)
{
  confirmdiscard(quitnow);
  return TRUE;
}

static gboolean onsignal(gpointer data)
{
  quitnow();
  return G_SOURCE_REMOVE;
}

//
// Settings of SidMonkey itself, in ~/.goattrk/sidmonkey.ini (the engine's
// are shared with GoatTracker in goattrk2.cfg)
//

static char *settingspath(void)
{
  return g_build_filename(g_get_home_dir(), ".goattrk", "sidmonkey.ini", NULL);
}

static void loadsettings(void)
{
  GKeyFile *keys = g_key_file_new();
  char *path = settingspath();

  if (g_key_file_load_from_file(keys, path, G_KEY_FILE_NONE, NULL))
  {
    char *name = g_key_file_get_string(keys, "midi", "input", NULL);
    if ((name) && (name[0]) && (!midi_setinput(name)))
      g_printerr("sidmonkey: MIDI input \"%s\" is not available\n", name);
    // Remembered even when it isn't plugged in
    if (name) g_strlcpy(midi_inputname, name, sizeof midi_inputname);
    g_free(name);
  }
  g_free(path);
  g_key_file_unref(keys);
}

static void savesettings(void)
{
  GKeyFile *keys = g_key_file_new();
  char *path = settingspath();
  char *dir = g_path_get_dirname(path);

  g_key_file_load_from_file(keys, path, G_KEY_FILE_KEEP_COMMENTS, NULL);
  g_key_file_set_string(keys, "midi", "input", midi_inputname);
  g_mkdir_with_parents(dir, 0700);
  g_key_file_save_to_file(keys, path, NULL);
  g_free(dir);
  g_free(path);
  g_key_file_unref(keys);
}

//
// MIDI
//

static void onmidinote(int midinote, int velocity)
{
  int note = midi_tonote(midinote);

  if (!velocity) roll_noteoff(MIDIVOICE(midinote));
  else if (note >= 0) roll_noteon(MIDIVOICE(midinote), note);
}

static void onmidichosen(GObject *drop, GParamSpec *pspec, gpointer data)
{
  guint sel = gtk_drop_down_get_selected(GTK_DROP_DOWN(drop));
  GtkStringObject *item = gtk_drop_down_get_selected_item(GTK_DROP_DOWN(drop));
  const char *name = ((sel) && (item)) ? gtk_string_object_get_string(item) : "";

  if (!midi_setinput(name)) sm_toast("That MIDI input could not be connected");
  savesettings();
}

// Choose the MIDI controller to play notes with
static void choosemidi(void)
{
  AdwDialog *dialog = adw_alert_dialog_new("MIDI Input", "Notes played on this MIDI controller sound with "
    "the current instrument, and are written into the clip when step entry is on.");
  char **sources = midi_listsources();
  GtkStringList *list = gtk_string_list_new(NULL);
  GtkWidget *drop;
  int c, selected = 0;

  gtk_string_list_append(list, "Off");
  for (c = 0; sources[c]; c++)
  {
    gtk_string_list_append(list, sources[c]);
    if (!strcmp(sources[c], midi_inputname)) selected = c + 1;
  }
  // The saved one stays listed when it isn't plugged in
  if ((midi_inputname[0]) && (!selected))
  {
    gtk_string_list_append(list, midi_inputname);
    selected = c + 1;
  }
  g_strfreev(sources);
  drop = gtk_drop_down_new(G_LIST_MODEL(list), NULL);
  gtk_drop_down_set_selected(GTK_DROP_DOWN(drop), selected);
  g_signal_connect(drop, "notify::selected", G_CALLBACK(onmidichosen), NULL);
  adw_alert_dialog_set_extra_child(ADW_ALERT_DIALOG(dialog), drop);
  adw_alert_dialog_add_response(ADW_ALERT_DIALOG(dialog), "close", "_Close");
  adw_dialog_present(dialog, GTK_WIDGET(sm_window));
}

// Key releases don't arrive while another window has the focus
static void onactivechanged(GtkWindow *window, GParamSpec *pspec, gpointer data)
{
  if (!gtk_window_is_active(window)) roll_releaseall();
}

//
// Actions and shortcuts
//

static void act(GSimpleAction *action, GVariant *parameter, gpointer data)
{
  const char *name = g_action_get_name(G_ACTION(action));

  if (!strcmp(name, "new")) confirmdiscard(newsong);
  else if (!strcmp(name, "open")) confirmdiscard(chooseopen);
  else if (!strcmp(name, "save")) savesong_();
  else if (!strcmp(name, "save-as")) savesongas();
  else if (!strcmp(name, "undo")) undoredo(0);
  else if (!strcmp(name, "redo")) undoredo(1);
  else if (!strcmp(name, "midi")) choosemidi();
  else if (!strcmp(name, "template")) confirmdiscard(start_newfromtemplate);
  else if (!strcmp(name, "export-audio")) export_audio();
  else if (!strcmp(name, "export-sid")) export_c64(FORMAT_SID);
  else if (!strcmp(name, "export-prg")) export_c64(FORMAT_PRG);
  else if (!strcmp(name, "learn")) start_learn();
  else if (!strcmp(name, "quit")) confirmdiscard(quitnow);
}

static const GActionEntry winactions[] = {
  {"new", act}, {"open", act}, {"save", act}, {"save-as", act}, {"undo", act}, {"redo", act}, {"midi", act}, {"quit", act}, {"template", act}, {"export-audio", act},
  {"export-sid", act}, {"export-prg", act}, {"learn", act}};

static void addshortcut(GtkShortcutController *sc, const char *accel, GtkShortcutFunc func)
{
  gtk_shortcut_controller_add_shortcut(sc, gtk_shortcut_new(gtk_shortcut_trigger_parse_string(accel),
    gtk_callback_action_new(func, NULL, NULL)));
}

static gboolean scplay(GtkWidget *w, GVariant *args, gpointer data) { togglepause(); return TRUE; }
static gboolean scstop(GtkWidget *w, GVariant *args, gpointer data) { host_stop(); updateplaybutton(); return TRUE; }

static GMenuModel *primarymenu(void)
{
  GMenu *menu = g_menu_new(), *s = g_menu_new();

  g_menu_append(s, "_New Song", "win.new");
  g_menu_append(s, "New Song with a _Beat", "win.template");
  g_menu_append(s, "_Open…", "win.open");
  g_menu_append_section(menu, NULL, G_MENU_MODEL(s));
  g_object_unref(s);
  s = g_menu_new();
  g_menu_append(s, "_Save", "win.save");
  g_menu_append(s, "Save _As…", "win.save-as");
  g_menu_append_section(menu, NULL, G_MENU_MODEL(s));
  g_object_unref(s);
  s = g_menu_new();
  g_menu_append(s, "_Learn the SID", "win.learn");
  g_menu_append(s, "_MIDI Input…", "win.midi");
  g_menu_append_section(menu, NULL, G_MENU_MODEL(s));
  g_object_unref(s);
  s = g_menu_new();
  g_menu_append(s, "_Quit", "win.quit");
  g_menu_append_section(menu, NULL, G_MENU_MODEL(s));
  g_object_unref(s);
  return G_MENU_MODEL(menu);
}

static GtkWidget *iconbutton(const char *icon, const char *tooltip, const char *action)
{
  GtkWidget *button = gtk_button_new_from_icon_name(icon);

  gtk_widget_set_tooltip_text(button, tooltip);
  gtk_actionable_set_action_name(GTK_ACTIONABLE(button), action);
  return button;
}

static void onactivate(GtkApplication *application, gpointer data)
{
  static const char *accels[][3] = {
    {"win.new", "<Control>n", NULL}, {"win.open", "<Control>o", NULL}, {"win.save", "<Control>s", NULL},
    {"win.save-as", "<Control><Shift>s", NULL}, {"win.undo", "<Control>z", NULL},
    {"win.redo", "<Control><Shift>z", "<Control>y"}, {"win.quit", "<Control>q", NULL},
    {"win.export-audio", "<Control>e", NULL}, {"win.learn", "F1", NULL}};
  GtkWidget *view, *header, *content, *button, *box, *bottom, *label;
  GtkEventController *controller;
  GtkDropTarget *drop;
  GMenuModel *model;
  int i;

  sm_window = GTK_WINDOW(adw_application_window_new(application));
  gtk_window_set_default_size(sm_window, 1440, 820);
  g_signal_connect(sm_window, "close-request", G_CALLBACK(oncloserequest), NULL);
  g_signal_connect(sm_window, "notify::is-active", G_CALLBACK(onactivechanged), NULL);
  midi_sethandler("SidMonkey", onmidinote);
  loadsettings();
  g_action_map_add_action_entries(G_ACTION_MAP(sm_window), winactions, G_N_ELEMENTS(winactions), NULL);
  for (i = 0; i < (int)G_N_ELEMENTS(accels); i++)
  {
    const char *list[] = {accels[i][1], accels[i][2], NULL};
    gtk_application_set_accels_for_action(application, accels[i][0], list);
  }

  view = adw_toolbar_view_new();
  header = adw_header_bar_new();
  title = ADW_WINDOW_TITLE(adw_window_title_new("SidMonkey", ""));
  adw_header_bar_set_title_widget(ADW_HEADER_BAR(header), GTK_WIDGET(title));

  adw_header_bar_pack_start(ADW_HEADER_BAR(header), iconbutton("document-open-symbolic", "Open a song (Ctrl+O)", "win.open"));
  playbutton = GTK_BUTTON(gtk_button_new_from_icon_name("media-playback-start-symbolic"));
  gtk_widget_add_css_class(GTK_WIDGET(playbutton), "suggested-action");
  g_signal_connect_swapped(playbutton, "clicked", G_CALLBACK(togglepause), NULL);
  adw_header_bar_pack_start(ADW_HEADER_BAR(header), GTK_WIDGET(playbutton));

  box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  label = gtk_label_new("Tune");
  gtk_box_append(GTK_BOX(box), label);
  tunespin = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(1, 1, 1));
  gtk_widget_set_tooltip_text(GTK_WIDGET(tunespin), "This song has several tunes in it");
  g_signal_connect(tunespin, "value-changed", G_CALLBACK(ontunechanged), NULL);
  gtk_box_append(GTK_BOX(box), GTK_WIDGET(tunespin));
  adw_header_bar_pack_start(ADW_HEADER_BAR(header), box);

  button = gtk_menu_button_new();
  gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(button), "open-menu-symbolic");
  gtk_widget_set_tooltip_text(button, "Main menu");
  model = primarymenu();
  gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(button), model);
  g_object_unref(model);
  gtk_menu_button_set_primary(GTK_MENU_BUTTON(button), TRUE);
  adw_header_bar_pack_end(ADW_HEADER_BAR(header), button);
  box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_add_css_class(box, "linked");
  gtk_box_append(GTK_BOX(box), iconbutton("edit-undo-symbolic", "Undo (Ctrl+Z)", "win.undo"));
  gtk_box_append(GTK_BOX(box), iconbutton("edit-redo-symbolic", "Redo (Ctrl+Shift+Z)", "win.redo"));
  adw_header_bar_pack_end(ADW_HEADER_BAR(header), box);
  adw_header_bar_pack_end(ADW_HEADER_BAR(header), iconbutton("document-save-symbolic", "Save (Ctrl+S)", "win.save"));
  {
    GMenu *m = g_menu_new();
    g_menu_append(m, "Audio (WAV or MP3)…", "win.export-audio");
    g_menu_append(m, "SID Music File…", "win.export-sid");
    g_menu_append(m, "C64 Program (PRG)…", "win.export-prg");
    button = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(button), "document-send-symbolic");
    gtk_widget_set_tooltip_text(button, "Export the song");
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(button), G_MENU_MODEL(m));
    g_object_unref(m);
    adw_header_bar_pack_end(ADW_HEADER_BAR(header), button);
  }
  adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(view), header);

  content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_append(GTK_BOX(content), arrange_new());
  gtk_box_append(GTK_BOX(content), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
  gtk_box_append(GTK_BOX(content), roll_new());

  // The instruments sidebar at the right
  {
    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    GtkWidget *side = sound_new();
    gtk_paned_set_start_child(GTK_PANED(paned), content);
    gtk_paned_set_end_child(GTK_PANED(paned), side);
    gtk_paned_set_resize_end_child(GTK_PANED(paned), FALSE);
    gtk_paned_set_shrink_end_child(GTK_PANED(paned), FALSE);
    gtk_widget_set_size_request(side, 340, -1);
    content = paned;
  }
  toasts = ADW_TOAST_OVERLAY(adw_toast_overlay_new());
  adw_toast_overlay_set_child(toasts, content);
  adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(view), GTK_WIDGET(toasts));

  bottom = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_widget_set_margin_start(bottom, 12);
  gtk_widget_set_margin_end(bottom, 12);
  gtk_widget_set_margin_top(bottom, 6);
  gtk_widget_set_margin_bottom(bottom, 6);
  statuslabel = GTK_LABEL(gtk_label_new(""));
  gtk_label_set_xalign(statuslabel, 0);
  gtk_label_set_ellipsize(statuslabel, PANGO_ELLIPSIZE_END);
  gtk_widget_set_hexpand(GTK_WIDGET(statuslabel), TRUE);
  gtk_box_append(GTK_BOX(bottom), GTK_WIDGET(statuslabel));
  limitslabel = GTK_LABEL(gtk_label_new(""));
  gtk_widget_add_css_class(GTK_WIDGET(limitslabel), "dim-label");
  gtk_widget_set_tooltip_text(GTK_WIDGET(limitslabel), "How much of what a song can hold this one uses");
  gtk_box_append(GTK_BOX(bottom), GTK_WIDGET(limitslabel));
  {
    GtkWidget *popover = gtk_popover_new();
    filterlabel = GTK_LABEL(gtk_label_new(""));
    gtk_label_set_wrap(GTK_LABEL(filterlabel), TRUE);
    gtk_label_set_max_width_chars(filterlabel, 40);
    gtk_popover_set_child(GTK_POPOVER(popover), GTK_WIDGET(filterlabel));
    filterbutton = gtk_menu_button_new();
    gtk_menu_button_set_label(GTK_MENU_BUTTON(filterbutton), "Filter shared");
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(filterbutton), "dialog-warning-symbolic");
    gtk_menu_button_set_always_show_arrow(GTK_MENU_BUTTON(filterbutton), FALSE);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(filterbutton), popover);
    gtk_widget_add_css_class(filterbutton, "flat");
    gtk_widget_add_css_class(filterbutton, "warning");
    gtk_widget_set_tooltip_text(filterbutton, "More than one instrument uses the filter");
    gtk_widget_set_visible(filterbutton, FALSE);
    gtk_box_append(GTK_BOX(bottom), filterbutton);
  }
  timelabel = GTK_LABEL(gtk_label_new(""));
  gtk_widget_add_css_class(GTK_WIDGET(timelabel), "numeric");
  gtk_box_append(GTK_BOX(bottom), GTK_WIDGET(timelabel));
  adw_toolbar_view_add_bottom_bar(ADW_TOOLBAR_VIEW(view), bottom);

  adw_application_window_set_content(ADW_APPLICATION_WINDOW(sm_window), view);

  controller = gtk_shortcut_controller_new();
  gtk_shortcut_controller_set_scope(GTK_SHORTCUT_CONTROLLER(controller), GTK_SHORTCUT_SCOPE_GLOBAL);
  addshortcut(GTK_SHORTCUT_CONTROLLER(controller), "space", scplay);
  addshortcut(GTK_SHORTCUT_CONTROLLER(controller), "Escape", scstop);
  gtk_widget_add_controller(GTK_WIDGET(sm_window), controller);

  drop = gtk_drop_target_new(G_TYPE_FILE, GDK_ACTION_COPY);
  g_signal_connect(drop, "drop", G_CALLBACK(ondrop), NULL);
  gtk_widget_add_controller(GTK_WIDGET(sm_window), GTK_EVENT_CONTROLLER(drop));

  // A song named on the command line was loaded by goattrk2_init()
  if (strlen(songfilename))
  {
    if (loadresult != LOAD_OK)
    {
      clearsong(1, 1, 1, 1, 1);
      reportload(loadresult);
    }
    else
    {
      setsongfile(songfilename);
      if (songsettingsloaded) host_restartsound();
    }
  }
  undo_reset();

  g_timeout_add(20, tick, NULL);
  sm_songchanged();
  gtk_window_present(sm_window);
  if (soundinitfailed) sm_toast("Sound output could not be started");
  if (!strlen(songfilename)) start_welcome();
}

int main(int argc, char **argv)
{
  int status = goattrk2_init(argc, argv);

  if (status >= 0) return status;
  app = adw_application_new(APPID, G_APPLICATION_NON_UNIQUE);
  g_signal_connect(app, "activate", G_CALLBACK(onactivate), NULL);
  g_unix_signal_add(SIGINT, onsignal, NULL);
  g_unix_signal_add(SIGTERM, onsignal, NULL);
  status = g_application_run(G_APPLICATION(app), 1, argv);
  g_object_unref(app);
  return status;
}
