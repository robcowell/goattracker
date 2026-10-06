//
// SidMonkey: exporting the song
//
// As audio (WAV, or MP3 when libmp3lame is installed), rendered offline
// through the editor's playroutine (grender.c) in idle-callback chunks, then
// faded and normalised (gaudio.c). Or as C64 files through the packer
// (greloc.c): a .sid for SID players and emulators, or a .prg with the
// player at $1000 for use in C64 programs.
//

#include "sm.h"
#include "gaudio.h"

typedef struct
{
  char *path;
  int mp3;
  int loops;
  int fade;
  double expected;
  GArray *samples;
  int cancelled;
  int savedmute[MAX_CHN];
  AdwDialog *progress;
  GtkWidget *bar;
} AUDIOJOB;

static AUDIOJOB *job;
static int lastmp3, lastloops = 1, lastfade = 3;
static AdwDialog *optionsdialog;
static GtkWidget *formatrow, *loopsrow, *faderow;

static char *defaultname(const char *ext)
{
  char *base = g_strdup(strlen(songname) ? sm_toutf8(songname) :
    (strlen(loadedsongfilename) ? sm_toutf8(loadedsongfilename) : "song"));
  char *dot = strrchr(base, '.'), *name, *p;

  if ((dot) && (!strlen(songname))) *dot = 0;
  // Characters file names can't have
  for (p = base; *p; p++)
    if ((*p == '/') || (*p == '\\')) *p = '-';
  name = g_strconcat(base, ext, NULL);
  g_free(base);
  return name;
}

static void choosepath(const char *title, const char *ext, const char *filtername,
  void (*done)(GObject *, GAsyncResult *, gpointer))
{
  GtkFileDialog *dialog = gtk_file_dialog_new();
  GtkFileFilter *filter = gtk_file_filter_new();
  GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
  GFile *folder = g_file_new_for_path(strlen(songpath) ? songpath : ".");
  char *name = defaultname(ext);
  char pattern[16];

  snprintf(pattern, sizeof pattern, "*%s", ext);
  gtk_file_filter_set_name(filter, filtername);
  gtk_file_filter_add_pattern(filter, pattern);
  g_list_store_append(filters, filter);
  gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
  gtk_file_dialog_set_title(dialog, title);
  gtk_file_dialog_set_initial_folder(dialog, folder);
  gtk_file_dialog_set_initial_name(dialog, name);
  gtk_file_dialog_save(dialog, sm_window, NULL, done, NULL);
  g_free(name);
  g_object_unref(folder);
  g_object_unref(filters);
  g_object_unref(filter);
  g_object_unref(dialog);
}

// The chosen path, with the extension added if it has none
static char *chosenpath(GObject *source, GAsyncResult *result, const char *ext)
{
  GFile *file = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(source), result, NULL);
  char *path = file ? g_file_get_path(file) : NULL;

  if (file) g_object_unref(file);
  if ((path) && (!g_str_has_suffix(path, ext)))
  {
    char *withext = g_strconcat(path, ext, NULL);
    g_free(path);
    path = withext;
  }
  return path;
}

//
// Audio
//

static void endjob(AUDIOJOB *j)
{
  int c;

  render_end();
  for (c = 0; c < MAX_CHN; c++) chn[c].mute = j->savedmute[c];
  adw_dialog_force_close(j->progress);
  g_array_unref(j->samples);
  g_free(j->path);
  g_free(j);
  job = NULL;
}

static gboolean renderchunk(gpointer data)
{
  AUDIOJOB *j = data;
  short chunk[16384];
  int rounds;

  if (j->cancelled)
  {
    endjob(j);
    return G_SOURCE_REMOVE;
  }
  // About a second of audio per round, a few rounds per idle call
  for (rounds = 0; (rounds < 8) && (!render_finished()); rounds++)
  {
    int n = render_run(chunk, G_N_ELEMENTS(chunk));
    if (n) g_array_append_vals(j->samples, chunk, n);
  }
  if (j->expected > 0)
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(j->bar), CLAMP(render_seconds() / j->expected, 0, 1));
  if (!render_finished()) return G_SOURCE_CONTINUE;

  {
    short *samples = (short *)j->samples->data;
    unsigned count = j->samples->len, rate = render_rate();
    char *name = g_path_get_basename(j->path), message[MAX_PATHNAME + 32];
    int ok;

    audio_fade(samples, count, rate, j->fade);
    audio_gain(samples, count, audio_normalgain(samples, count));
    ok = j->mp3 ? audio_writemp3(j->path, samples, count, rate, 192) : audio_writewav(j->path, samples, count, rate);
    snprintf(message, sizeof message, ok ? "Exported %s" : "%s could not be written", name);
    sm_toast(message);
    g_free(name);
  }
  endjob(j);
  return G_SOURCE_REMOVE;
}

static void oncancel(AdwAlertDialog *dialog, const char *response, gpointer data)
{
  if (job) job->cancelled = 1;
}

static void onaudiochosen(GObject *source, GAsyncResult *result, gpointer data)
{
  char *path = chosenpath(source, result, lastmp3 ? ".mp3" : ".wav");
  double length;
  AUDIOJOB *j;
  int c;

  if ((!path) || (job))
  {
    g_free(path);
    return;
  }
  length = render_songlength(sm_subtune(), 1800);
  j = g_new0(AUDIOJOB, 1);
  job = j;
  j->path = path;
  j->mp3 = lastmp3;
  j->loops = lastloops;
  j->fade = lastfade;
  j->expected = length * lastloops;
  j->samples = g_array_new(FALSE, FALSE, sizeof(short));
  // The whole mix, whatever is muted for listening
  for (c = 0; c < MAX_CHN; c++)
  {
    j->savedmute[c] = chn[c].mute;
    chn[c].mute = 0;
  }
  j->progress = adw_alert_dialog_new(j->mp3 ? "Exporting MP3" : "Exporting WAV", NULL);
  j->bar = gtk_progress_bar_new();
  adw_alert_dialog_set_extra_child(ADW_ALERT_DIALOG(j->progress), j->bar);
  adw_alert_dialog_add_response(ADW_ALERT_DIALOG(j->progress), "cancel", "_Cancel");
  g_signal_connect(j->progress, "response", G_CALLBACK(oncancel), NULL);
  adw_dialog_present(j->progress, GTK_WIDGET(sm_window));
  render_begin(sm_subtune(), j->loops, 1800, 0);
  g_idle_add(renderchunk, j);
}

static void onaudioexport(GtkButton *button, gpointer data)
{
  lastmp3 = audio_mp3available() && (adw_combo_row_get_selected(ADW_COMBO_ROW(formatrow)) == 1);
  lastloops = (int)adw_spin_row_get_value(ADW_SPIN_ROW(loopsrow));
  lastfade = (int)adw_spin_row_get_value(ADW_SPIN_ROW(faderow));
  adw_dialog_close(optionsdialog);
  choosepath(lastmp3 ? "Export MP3" : "Export WAV", lastmp3 ? ".mp3" : ".wav", lastmp3 ? "MP3 Audio" : "WAV Audio",
    onaudiochosen);
}

void export_audio(void)
{
  static const char *formats[] = {"WAV (uncompressed)", "MP3", NULL};
  GtkWidget *view = adw_toolbar_view_new();
  GtkWidget *header = adw_header_bar_new();
  GtkWidget *page = adw_preferences_page_new();
  GtkWidget *group = adw_preferences_group_new();
  GtkWidget *button = gtk_button_new_with_label("Export…");
  double length;
  char description[160];

  if (job) return;
  host_stop();
  length = render_songlength(sm_subtune(), 1800);
  if (length < 0)
  {
    sm_toast("The song doesn't end within 30 minutes, so it can't be exported");
    return;
  }
  snprintf(description, sizeof description, "The song plays for %d:%02d. Everything is exported, even voices "
    "muted for listening.", (int)length / 60, (int)length % 60);

  optionsdialog = adw_dialog_new();
  adw_dialog_set_title(optionsdialog, "Export Audio");
  adw_dialog_set_content_width(optionsdialog, 440);
  gtk_widget_add_css_class(button, "suggested-action");
  g_signal_connect(button, "clicked", G_CALLBACK(onaudioexport), NULL);
  adw_header_bar_pack_end(ADW_HEADER_BAR(header), button);
  adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(view), header);

  adw_preferences_group_set_description(ADW_PREFERENCES_GROUP(group), description);
  formatrow = adw_combo_row_new();
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(formatrow), "Format");
  adw_combo_row_set_model(ADW_COMBO_ROW(formatrow), G_LIST_MODEL(gtk_string_list_new(formats)));
  if (!audio_mp3available())
  {
    adw_action_row_set_subtitle(ADW_ACTION_ROW(formatrow), "MP3 needs libmp3lame (the lame package) installed");
    gtk_widget_set_sensitive(formatrow, FALSE);
  }
  else adw_combo_row_set_selected(ADW_COMBO_ROW(formatrow), lastmp3);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), formatrow);
  loopsrow = adw_spin_row_new_with_range(1, 8, 1);
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(loopsrow), "Times Through the Song");
  adw_spin_row_set_value(ADW_SPIN_ROW(loopsrow), lastloops);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), loopsrow);
  faderow = adw_spin_row_new_with_range(0, 20, 1);
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(faderow), "Fade Out");
  adw_action_row_set_subtitle(ADW_ACTION_ROW(faderow), "Seconds at the end");
  adw_spin_row_set_value(ADW_SPIN_ROW(faderow), lastfade);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), faderow);
  adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(group));
  adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(view), page);
  adw_dialog_set_child(optionsdialog, view);
  adw_dialog_present(optionsdialog, GTK_WIDGET(sm_window));
}

//
// C64 files
//

static int packformat;

static void onpackchosen(GObject *source, GAsyncResult *result, gpointer data)
{
  char *path = chosenpath(source, result, packformat == FORMAT_SID ? ".sid" : ".prg");
  char *log = NULL, *name;
  size_t loglen = 0;
  int oldformat = fileformat;

  if (!path) return;
  if (strlen(path) >= MAX_PATHNAME)
  {
    sm_toast("The file name or folder name is too long");
    g_free(path);
    return;
  }
  host_stop();
  strcpy(packedsongname, path);
  // The format is chosen here; the packer's other settings are GoatTracker's
  fileformat = packformat;
  relocout = open_memstream(&log, &loglen);
  relocator();
  fclose(relocout);
  relocout = NULL;
  fileformat = oldformat;

  name = g_path_get_basename(path);
  if (relocsuccess)
  {
    char message[MAX_PATHNAME + 96];
    snprintf(message, sizeof message, packformat == FORMAT_SID ?
      "Exported %s: play it in a SID player or C64 emulator" :
      "Exported %s: the player is at $1000 (call $1000 to start, $1003 every frame)", name);
    sm_toast(message);
  }
  else
  {
    AdwDialog *dialog = adw_alert_dialog_new("Export Failed", "The packer couldn't make the file:");
    GtkWidget *label = gtk_label_new(log ? log : "");
    gtk_widget_add_css_class(label, "monospace");
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    adw_alert_dialog_set_extra_child(ADW_ALERT_DIALOG(dialog), label);
    adw_alert_dialog_add_response(ADW_ALERT_DIALOG(dialog), "close", "_Close");
    adw_dialog_present(dialog, GTK_WIDGET(sm_window));
  }
  g_free(name);
  free(log);
  g_free(path);
}

void export_c64(int format)
{
  packformat = format;
  if (format == FORMAT_SID) choosepath("Export SID", ".sid", "SID Music Files", onpackchosen);
  else choosepath("Export C64 Program", ".prg", "C64 Programs", onpackchosen);
}
