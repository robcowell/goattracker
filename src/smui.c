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
static GtkLabel *statuslabel, *timelabel;
static int subtune;
static int wasplaying;
static double songseconds = -1;
static int quitting;

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

static void updateplaybutton(void)
{
  int playing = isplaying();

  gtk_button_set_icon_name(playbutton, playing ? "media-playback-stop-symbolic" : "media-playback-start-symbolic");
  gtk_widget_set_tooltip_text(GTK_WIDGET(playbutton), playing ? "Stop (Space)" : "Play the song (Space)");
}

// After loading a song or switching subtunes
void sm_songchanged(void)
{
  if (subtune >= sm_subtunes()) subtune = 0;
  adw_window_title_set_title(title, strlen(songname) ? sm_toutf8(songname) :
    (strlen(loadedsongfilename) ? sm_toutf8(loadedsongfilename) : "Untitled Song"));
  adw_window_title_set_subtitle(title, strlen(authorname) ? sm_toutf8(authorname) : "");
  gtk_spin_button_set_range(tunespin, 1, sm_subtunes());
  gtk_spin_button_set_value(tunespin, subtune + 1);
  gtk_widget_set_visible(gtk_widget_get_parent(GTK_WIDGET(tunespin)), sm_subtunes() > 1);
  songseconds = render_songlength(subtune, 15 * 60);
  arrange_songchanged();
  sm_setstatus("Click a clip to see what it is. Double-click to play from there.");
  updatetime();
  updateplaybutton();
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
  subtune = 0;
  sm_songchanged();
}

static void onopened(GObject *source, GAsyncResult *result, gpointer data)
{
  GFile *file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source), result, NULL);

  if (!file) return;
  if (g_file_get_path(file))
  {
    char *path = g_file_get_path(file);
    opensong(path);
    g_free(path);
  }
  g_object_unref(file);
}

static void onopen(void)
{
  GtkFileDialog *dialog = gtk_file_dialog_new();
  GtkFileFilter *filter = gtk_file_filter_new();
  GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
  GFile *folder = g_file_new_for_path(strlen(songpath) ? songpath : ".");

  gtk_file_filter_set_name(filter, "GoatTracker Songs");
  gtk_file_filter_add_suffix(filter, "sng");
  gtk_file_filter_add_suffix(filter, "SNG");
  g_list_store_append(filters, filter);
  gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
  gtk_file_dialog_set_title(dialog, "Open Song");
  gtk_file_dialog_set_initial_folder(dialog, folder);
  gtk_file_dialog_open(dialog, sm_window, NULL, onopened, NULL);
  g_object_unref(folder);
  g_object_unref(filters);
  g_object_unref(filter);
  g_object_unref(dialog);
}

static gboolean ondrop(GtkDropTarget *target, const GValue *value, double x, double y, gpointer data)
{
  GFile *file = g_value_get_object(value);
  char *path = file ? g_file_get_path(file) : NULL;

  if (!path) return FALSE;
  opensong(path);
  g_free(path);
  return TRUE;
}

static gboolean tick(gpointer data)
{
  int playing = isplaying();

  arrange_tick();
  if (playing || wasplaying) updatetime();
  if (playing != wasplaying) updateplaybutton();
  wasplaying = playing;
  return G_SOURCE_CONTINUE;
}

static void quitnow(void)
{
  if (quitting) return;
  quitting = 1;
  goattrk2_shutdown();
  g_application_quit(G_APPLICATION(app));
}

static gboolean oncloserequest(GtkWindow *window, gpointer data)
{
  quitnow();
  return TRUE;
}

static gboolean onsignal(gpointer data)
{
  quitnow();
  return G_SOURCE_REMOVE;
}

static void addshortcut(GtkShortcutController *sc, const char *accel, GtkShortcutFunc func)
{
  gtk_shortcut_controller_add_shortcut(sc, gtk_shortcut_new(gtk_shortcut_trigger_parse_string(accel),
    gtk_callback_action_new(func, NULL, NULL)));
}

static gboolean scplay(GtkWidget *w, GVariant *args, gpointer data) { togglepause(); return TRUE; }
static gboolean scstop(GtkWidget *w, GVariant *args, gpointer data) { host_stop(); updateplaybutton(); return TRUE; }
static gboolean scopen(GtkWidget *w, GVariant *args, gpointer data) { onopen(); return TRUE; }
static gboolean scquit(GtkWidget *w, GVariant *args, gpointer data) { quitnow(); return TRUE; }

static void onactivate(GtkApplication *application, gpointer data)
{
  GtkWidget *view, *header, *content, *button, *box, *bottom, *label;
  GtkEventController *controller;
  GtkDropTarget *drop;

  sm_window = GTK_WINDOW(adw_application_window_new(application));
  gtk_window_set_default_size(sm_window, 1100, 560);
  g_signal_connect(sm_window, "close-request", G_CALLBACK(oncloserequest), NULL);

  view = adw_toolbar_view_new();
  header = adw_header_bar_new();
  title = ADW_WINDOW_TITLE(adw_window_title_new("SidMonkey", ""));
  adw_header_bar_set_title_widget(ADW_HEADER_BAR(header), GTK_WIDGET(title));

  button = gtk_button_new_from_icon_name("document-open-symbolic");
  gtk_widget_set_tooltip_text(button, "Open a song (Ctrl+O)");
  g_signal_connect_swapped(button, "clicked", G_CALLBACK(onopen), NULL);
  adw_header_bar_pack_start(ADW_HEADER_BAR(header), button);

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
  adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(view), header);

  content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_append(GTK_BOX(content), arrange_new());
  gtk_box_append(GTK_BOX(content), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
  // The piano roll for the selected clip goes here (phase 3)
  label = gtk_label_new(NULL);
  gtk_widget_set_vexpand(label, TRUE);
  gtk_box_append(GTK_BOX(content), label);

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
  timelabel = GTK_LABEL(gtk_label_new(""));
  gtk_widget_add_css_class(GTK_WIDGET(timelabel), "numeric");
  gtk_box_append(GTK_BOX(bottom), GTK_WIDGET(timelabel));
  adw_toolbar_view_add_bottom_bar(ADW_TOOLBAR_VIEW(view), bottom);

  adw_application_window_set_content(ADW_APPLICATION_WINDOW(sm_window), view);

  controller = gtk_shortcut_controller_new();
  gtk_shortcut_controller_set_scope(GTK_SHORTCUT_CONTROLLER(controller), GTK_SHORTCUT_SCOPE_GLOBAL);
  addshortcut(GTK_SHORTCUT_CONTROLLER(controller), "space", scplay);
  addshortcut(GTK_SHORTCUT_CONTROLLER(controller), "Escape", scstop);
  addshortcut(GTK_SHORTCUT_CONTROLLER(controller), "<Control>o", scopen);
  addshortcut(GTK_SHORTCUT_CONTROLLER(controller), "<Control>q", scquit);
  gtk_widget_add_controller(GTK_WIDGET(sm_window), controller);

  drop = gtk_drop_target_new(G_TYPE_FILE, GDK_ACTION_COPY);
  g_signal_connect(drop, "drop", G_CALLBACK(ondrop), NULL);
  gtk_widget_add_controller(GTK_WIDGET(sm_window), GTK_EVENT_CONTROLLER(drop));

  g_timeout_add(20, tick, NULL);
  sm_songchanged();
  gtk_window_present(sm_window);

  // A song named on the command line was loaded by goattrk2_init()
  if (strlen(songfilename))
  {
    if (loadresult != LOAD_OK) reportload(loadresult);
    else
    {
      undo_reset();
      if (songsettingsloaded) host_restartsound();
    }
  }
  if (soundinitfailed) sm_toast("Sound output could not be started");
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
