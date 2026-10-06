//
// GOATTRACKER v2 GTK user interface: WAV export
//
// Renders the current subtune offline (grender.c) into memory, applies the
// fade-out and normalisation, and writes 16-bit mono WAV files: the full mix
// and, optionally, one stem per channel (the other channels muted).
//

#include <string.h>
#include "gtkui.h"

typedef struct
{
  char *path;               // full mix; stems get -ch1, -ch2, -ch3 added
  int loops;
  int fadeseconds;
  int normalise;
  int stems;
  int pass;                 // 0 = full mix, 1..MAX_CHN = stem of channel pass-1
  int passes;
  GArray *samples;
  double expected;          // seconds, for the progress bar
  double gain;              // normalisation gain, taken from the full mix
  int cancelled;
  int savedmute[MAX_CHN];
  AdwDialog *progress;
  GtkWidget *bar;
  GtkWidget *label;
} WAVJOB;

static WAVJOB *job = NULL;

// Settings of the export dialog, kept for the next export
static int lastloops = 1;
static int lastfade = 3;
static int lastnormalise = 1;
static int laststems = 0;

static void writele16(FILE *f, unsigned v) { fputc(v & 0xff, f); fputc(v >> 8, f); }
static void writele32(FILE *f, unsigned v) { writele16(f, v & 0xffff); writele16(f, v >> 16); }

static int writewav(const char *path, const short *data, unsigned count, unsigned rate)
{
  FILE *f = fopen(path, "wb");
  unsigned bytes = count * 2;

  if (!f) return 0;
  fwrite("RIFF", 4, 1, f);
  writele32(f, 36 + bytes);
  fwrite("WAVEfmt ", 8, 1, f);
  writele32(f, 16);
  writele16(f, 1);          // PCM
  writele16(f, 1);          // mono
  writele32(f, rate);
  writele32(f, rate * 2);
  writele16(f, 2);
  writele16(f, 16);
  fwrite("data", 4, 1, f);
  writele32(f, bytes);
  fwrite(data, 2, count, f);
  return fclose(f) == 0;
}

static char *passpath(WAVJOB *j)
{
  char *base, *dot;
  char *path;

  if (!j->pass) return g_strdup(j->path);
  base = g_strdup(j->path);
  dot = strrchr(base, '.');
  if ((dot) && (!strchr(dot, '/'))) *dot = 0;
  path = g_strdup_printf("%s-ch%d.wav", base, j->pass);
  g_free(base);
  return path;
}

static void finishpass(WAVJOB *j)
{
  short *data = (short *)j->samples->data;
  unsigned count = j->samples->len;
  unsigned rate = render_rate();
  unsigned fadesamples = j->fadeseconds * rate;
  unsigned c;
  char *path;

  // Fade out over the last seconds
  if (fadesamples > count) fadesamples = count;
  for (c = 0; c < fadesamples; c++)
  {
    unsigned i = count - fadesamples + c;
    data[i] = (short)(data[i] * (double)(fadesamples - c) / fadesamples);
  }

  // Scale the full mix's loudest sample to just below full scale, and the
  // stems by the same amount so that they keep their balance
  if (j->normalise)
  {
    if (!j->pass)
    {
      int peak = 1;
      for (c = 0; c < count; c++) if (abs(data[c]) > peak) peak = abs(data[c]);
      j->gain = 32000.0 / peak;
    }
    for (c = 0; c < count; c++) data[c] = (short)CLAMP(data[c] * j->gain, -32768, 32767);
  }

  path = passpath(j);
  if (!writewav(path, data, count, rate)) j->cancelled = 2;
  g_free(path);
}

static void startpass(WAVJOB *j)
{
  int c;

  // Stems: only the stem's channel is audible
  for (c = 0; c < MAX_CHN; c++)
    chn[c].mute = j->pass ? (c != j->pass - 1) : j->savedmute[c];
  g_array_set_size(j->samples, 0);
  render_begin(esnum, j->loops, 1800, 0);
}

static void endjob(WAVJOB *j)
{
  int c;
  char message[MAX_PATHNAME + 64];

  render_end();
  for (c = 0; c < MAX_CHN; c++) chn[c].mute = j->savedmute[c];
  adw_dialog_force_close(j->progress);

  if (j->cancelled == 2) ui_toast("The WAV file could not be written");
  else if (!j->cancelled)
  {
    char *name = g_path_get_basename(j->path);
    snprintf(message, sizeof message, "Exported %s%s", name, j->stems ? " and channel stems" : "");
    ui_toast(message);
    g_free(name);
  }
  g_array_unref(j->samples);
  g_free(j->path);
  g_free(j);
  job = NULL;
  ui_refresh();
}

static gboolean renderchunk(gpointer data)
{
  WAVJOB *j = data;
  short chunk[16384];
  int n, rounds;
  char text[64];

  if (j->cancelled)
  {
    endjob(j);
    return G_SOURCE_REMOVE;
  }

  // About a second of audio per round, a few rounds per idle call
  for (rounds = 0; rounds < 8; rounds++)
  {
    n = render_run(chunk, G_N_ELEMENTS(chunk));
    if (n) g_array_append_vals(j->samples, chunk, n);
    if (render_finished()) break;
  }

  if (render_finished())
  {
    render_end();
    finishpass(j);
    if ((j->cancelled) || (++j->pass >= j->passes))
    {
      endjob(j);
      return G_SOURCE_REMOVE;
    }
    startpass(j);
  }

  if (j->expected > 0)
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(j->bar),
      CLAMP((j->pass * j->expected + render_seconds()) / (j->passes * j->expected), 0, 1));
  if (j->pass) snprintf(text, sizeof text, "Rendering channel %d", j->pass);
  else snprintf(text, sizeof text, "Rendering the song");
  gtk_label_set_text(GTK_LABEL(j->label), text);
  return G_SOURCE_CONTINUE;
}

static void oncancelrender(AdwAlertDialog *dialog, const char *response, gpointer data)
{
  if (job) job->cancelled = 1;
}

static void onwavchosen(const char *path, gpointer data)
{
  WAVJOB *j;
  GtkWidget *box;
  double length;
  int c;

  if (job) return;
  length = render_songlength(esnum, 1800);
  if (length < 0)
  {
    ui_toast("The song doesn't end within 30 minutes, so it can't be exported");
    return;
  }

  j = g_new0(WAVJOB, 1);
  job = j;
  j->path = g_str_has_suffix(path, ".wav") ? g_strdup(path) : g_strdup_printf("%s.wav", path);
  j->loops = lastloops;
  j->fadeseconds = lastfade;
  j->normalise = lastnormalise;
  j->stems = laststems;
  j->passes = laststems ? MAX_CHN + 1 : 1;
  j->expected = length * lastloops;
  j->samples = g_array_new(FALSE, FALSE, sizeof(short));
  for (c = 0; c < MAX_CHN; c++) j->savedmute[c] = chn[c].mute;

  j->progress = adw_alert_dialog_new("Exporting WAV", NULL);
  box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  j->label = gtk_label_new("");
  j->bar = gtk_progress_bar_new();
  gtk_box_append(GTK_BOX(box), j->label);
  gtk_box_append(GTK_BOX(box), j->bar);
  adw_alert_dialog_set_extra_child(ADW_ALERT_DIALOG(j->progress), box);
  adw_alert_dialog_add_response(ADW_ALERT_DIALOG(j->progress), "cancel", "Cancel");
  g_signal_connect(j->progress, "response", G_CALLBACK(oncancelrender), NULL);
  adw_dialog_present(j->progress, GTK_WIDGET(mainwindow));

  startpass(j);
  g_idle_add(renderchunk, j);
}

//
// Options dialog
//

static GtkWidget *loopsrow, *faderow, *normaliserow, *stemsrow;
static AdwDialog *optionsdialog;

static void onwavexport(GtkButton *button, gpointer data)
{
  char name[MAX_FILENAME + 8];
  char *dot;

  lastloops = (int)adw_spin_row_get_value(ADW_SPIN_ROW(loopsrow));
  lastfade = (int)adw_spin_row_get_value(ADW_SPIN_ROW(faderow));
  lastnormalise = adw_switch_row_get_active(ADW_SWITCH_ROW(normaliserow));
  laststems = adw_switch_row_get_active(ADW_SWITCH_ROW(stemsrow));
  adw_dialog_close(optionsdialog);

  g_strlcpy(name, strlen(loadedsongfilename) ? loadedsongfilename : "song", sizeof name);
  dot = strrchr(name, '.');
  if (dot) *dot = 0;
  g_strlcat(name, ".wav", sizeof name);
  ui_choosefile("Export WAV", songpath, ui_toutf8(name), "*.wav", "WAV Audio", 1, onwavchosen, NULL);
}

void ui_wavexport(void)
{
  GtkWidget *view = adw_toolbar_view_new();
  GtkWidget *header = adw_header_bar_new();
  GtkWidget *page = adw_preferences_page_new();
  GtkWidget *group = adw_preferences_group_new();
  GtkWidget *exportbutton = gtk_button_new_with_label("Export…");
  char description[128];
  double length;

  if (job) return;
  length = render_songlength(esnum, 1800);
  ui_refresh();
  if (length >= 0)
    snprintf(description, sizeof description, "Subtune %d plays for %d:%02d. The file is mono, 16-bit, %d Hz.",
      esnum, (int)length / 60, (int)length % 60, render_rate());
  else
    snprintf(description, sizeof description, "Subtune %d doesn't end within 30 minutes.", esnum);

  optionsdialog = adw_dialog_new();
  adw_dialog_set_title(optionsdialog, "Export WAV");
  adw_dialog_set_content_width(optionsdialog, 460);

  gtk_widget_add_css_class(exportbutton, "suggested-action");
  gtk_widget_set_sensitive(exportbutton, length >= 0);
  g_signal_connect(exportbutton, "clicked", G_CALLBACK(onwavexport), NULL);
  adw_header_bar_pack_end(ADW_HEADER_BAR(header), exportbutton);
  adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(view), header);

  adw_preferences_group_set_description(ADW_PREFERENCES_GROUP(group), description);
  loopsrow = adw_spin_row_new_with_range(1, 8, 1);
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(loopsrow), "Times Through the Song");
  adw_spin_row_set_value(ADW_SPIN_ROW(loopsrow), lastloops);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), loopsrow);
  faderow = adw_spin_row_new_with_range(0, 20, 1);
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(faderow), "Fade Out");
  adw_action_row_set_subtitle(ADW_ACTION_ROW(faderow), "Seconds at the end");
  adw_spin_row_set_value(ADW_SPIN_ROW(faderow), lastfade);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), faderow);
  normaliserow = adw_switch_row_new();
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(normaliserow), "Normalise");
  adw_action_row_set_subtitle(ADW_ACTION_ROW(normaliserow), "Raise the level so the loudest moment is just below full scale");
  adw_switch_row_set_active(ADW_SWITCH_ROW(normaliserow), lastnormalise);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), normaliserow);
  stemsrow = adw_switch_row_new();
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(stemsrow), "Channel Stems");
  adw_action_row_set_subtitle(ADW_ACTION_ROW(stemsrow), "Also write each channel on its own (name-ch1.wav and so on)");
  adw_switch_row_set_active(ADW_SWITCH_ROW(stemsrow), laststems);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), stemsrow);
  adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(group));

  adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(view), page);
  adw_dialog_set_child(optionsdialog, view);
  adw_dialog_present(optionsdialog, GTK_WIDGET(mainwindow));
}
