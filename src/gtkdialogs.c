//
// GOATTRACKER v2 GTK user interface: dialogs
//
// Everything the classic editor asked for with modal prompts: quitting,
// clearing, the hard restart ADSR, packing, help and file selection.
//

#include <string.h>
#include <unistd.h>
#include <glib/gstdio.h>
#include "gtkui.h"

//
// File selection
//

typedef struct
{
  int save;
  FILEDONE done;
  gpointer data;
} FILEREQUEST;

static void onfilechosen(GObject *source, GAsyncResult *result, gpointer data)
{
  FILEREQUEST *req = data;
  GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
  GFile *file;

  if (req->save) file = gtk_file_dialog_save_finish(dialog, result, NULL);
  else file = gtk_file_dialog_open_finish(dialog, result, NULL);
  if (file)
  {
    char *path = g_file_get_path(file);
    if (path) req->done(path, req->data);
    g_free(path);
    g_object_unref(file);
  }
  g_free(req);
}

static void addfilter(GListStore *filters, const char *pattern, const char *name)
{
  GtkFileFilter *filter = gtk_file_filter_new();

  // "*.ext" matches case-insensitively, like the classic file selector
  if ((pattern[0] == '*') && (pattern[1] == '.') && (!strpbrk(pattern + 2, "*?[")))
    gtk_file_filter_add_suffix(filter, pattern + 2);
  else
    gtk_file_filter_add_pattern(filter, pattern);
  gtk_file_filter_set_name(filter, name);
  g_list_store_append(filters, filter);
  g_object_unref(filter);
}

static void choosefile(const char *title, const char *dir, const char *name, const char *pattern,
  const char *filtername, int save, FILEDONE done, gpointer data)
{
  GtkFileDialog *dialog = gtk_file_dialog_new();
  GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
  FILEREQUEST *req = g_new0(FILEREQUEST, 1);
  GtkFileFilter *defaultfilter;
  GFile *folder = NULL;

  req->save = save;
  req->done = done;
  req->data = data;

  gtk_file_dialog_set_title(dialog, title);
  addfilter(filters, pattern, filtername);
  addfilter(filters, "*", "All Files");
  defaultfilter = g_list_model_get_item(G_LIST_MODEL(filters), 0);
  gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
  gtk_file_dialog_set_default_filter(dialog, defaultfilter);
  g_object_unref(defaultfilter);
  g_object_unref(filters);

  if ((dir) && (strlen(dir)))
  {
    folder = g_file_new_for_path(dir);
    gtk_file_dialog_set_initial_folder(dialog, folder);
  }
  if ((name) && (strlen(name)))
  {
    if (save) gtk_file_dialog_set_initial_name(dialog, name);
    else if (folder)
    {
      GFile *file = g_file_get_child(folder, name);
      if (g_file_query_exists(file, NULL)) gtk_file_dialog_set_initial_file(dialog, file);
      g_object_unref(file);
    }
  }
  if (folder) g_object_unref(folder);

  if (save) gtk_file_dialog_save(dialog, mainwindow, NULL, onfilechosen, req);
  else gtk_file_dialog_open(dialog, mainwindow, NULL, onfilechosen, req);
  g_object_unref(dialog);
}

void ui_choosefile(const char *title, const char *dir, const char *name, const char *pattern,
  const char *filtername, int save, FILEDONE done, gpointer data)
{
  choosefile(title, dir, name, pattern, filtername, save, done, data);
}

// The engine works with a directory (made current) and a bare file name
static int splitpath(const char *path, char *dir, char *name)
{
  char *base = g_path_get_basename(path);
  char *dirname = g_path_get_dirname(path);
  int ok = (strlen(base) < MAX_FILENAME) && (strlen(dirname) < MAX_PATHNAME);

  if (ok)
  {
    strcpy(name, base);
    strcpy(dir, dirname);
    if (chdir(dir)) ok = 0;
  }
  else ui_toast("The file name or path is too long");
  g_free(base);
  g_free(dirname);
  return ok;
}

static void alert(const char *heading, const char *body)
{
  AdwDialog *dialog = adw_alert_dialog_new(heading, body);

  adw_alert_dialog_add_response(ADW_ALERT_DIALOG(dialog), "close", "Close");
  adw_dialog_present(dialog, GTK_WIDGET(mainwindow));
}

//
// Songs and instruments
//

// The engine's loaders silently ignore files they cannot read, so check the
// identifier they accept ("GTS3".."GTS5" or "GTI3".."GTI5") first
static int checkfile(const char *path, const char *ident, const char *what)
{
  char header[4] = {0};
  char message[128];
  FILE *handle = fopen(path, "rb");

  if (handle)
  {
    if (fread(header, 4, 1, handle) != 1) memset(header, 0, sizeof header);
    fclose(handle);
  }
  if ((handle) && (!memcmp(header, ident, 3)) && (header[3] >= '3') && (header[3] <= '5')) return 1;
  snprintf(message, sizeof message, "This is not a GoatTracker v2 %s file, or it could not be read.", what);
  alert(handle ? "Unsupported File" : "Could Not Open the File", message);
  return 0;
}

// Action to continue with once the song has been saved (from the
// save/discard prompt)
static void (*aftersave)(void) = NULL;

static void reportmerge(void)
{
  static const char *what[] = {"", "subtunes", "instruments", "table rows", "patterns"};
  char body[256];

  if (mergeresult == MERGE_OK) return;
  if (mergeresult == MERGE_BADFILE)
  {
    alert("Could Not Merge the Song", "The file is damaged or uses more than 3 channels, so nothing was merged.");
    return;
  }
  snprintf(body, sizeof body, "The song has run out of %s, so only part of the other song was merged. "
    "Press Ctrl+Z to undo the merge.", what[mergeresult]);
  alert("Merge Incomplete", body);
}

// Warn about GoatTracker Ultra features this editor doesn't support
static void reportsongextras(void)
{
  char body[256];

  if (songsidchannels > MAX_CHN)
  {
    snprintf(body, sizeof body, "This song was made for %d channels (GoatTracker Stereo or Ultra). "
      "Only 3-channel songs are supported, so it will not play or save correctly.", songsidchannels);
    alert("Unsupported Song", body);
  }
  else if (songsidtracker64)
    alert("SIDTracker64 Mode Song", "This song was made in GoatTracker Ultra's SIDTracker64 mode, where empty rows "
      "release notes. It will sound different here. The mode setting is kept when you save.");
}

static void reportloadfailure(void)
{
  if (loadresult == LOAD_MULTICHANNEL)
    alert("Unsupported Song", "This song uses 6 channels (GoatTracker Stereo or Ultra). "
      "Only 3-channel songs are supported. The current song was not changed.");
  else if (loadresult == LOAD_DAMAGED)
    alert("Damaged Song File", "The file is damaged or not a GoatTracker v2 song. "
      "The current song was not changed.");
}

// A song named on the command line was loaded before sound started
void ui_afterstartupload(void)
{
  if (songsettingsloaded) ui_restartsound();
  if (loadresult != LOAD_OK) reportloadfailure();
  else reportsongextras();
}

// Open or merge the song at path (from the file chooser or drag and drop)
void ui_opensongpath(const char *path, int merge)
{
  char message[MAX_PATHNAME + 64];

  if (!checkfile(path, "GTS", "song")) return;
  if (!splitpath(path, songpath, songfilename)) return;
  if (merge)
  {
    // Merging adds to the song, so it can be undone
    mergesong();
    undo_checkpoint(0);
  }
  else
  {
    loadsong();
    if (loadresult != LOAD_OK)
    {
      reportloadfailure();
      return;
    }
    undo_reset();
    // Songs saved by GoatTracker Ultra (and this editor) carry their
    // playback settings
    if (songsettingsloaded) ui_restartsound();
  }
  snprintf(message, sizeof message, "%s %s%s", merge ? "Merged" : "Loaded", ui_toutf8(songfilename),
    ((!merge) && (songsettingsloaded)) ? " with its saved settings" : "");
  ui_toast(message);
  panels_syncall();
  ui_refresh();
  if (merge) reportmerge();
  else reportsongextras();
}

static void onsongopened(const char *path, gpointer data)
{
  ui_opensongpath(path, GPOINTER_TO_INT(data));
}

static void choosesongtoopen(void)
{
  choosefile("Open Song", songpath, songfilename, songfilter, "GoatTracker Songs", 0, onsongopened, GINT_TO_POINTER(0));
}

void ui_loadsong(int merge)
{
  if (merge)
    choosefile("Merge Song", songpath, songfilename, songfilter, "GoatTracker Songs", 0, onsongopened, GINT_TO_POINTER(1));
  else
  {
    // Opening replaces the song and its undo history
    undo_checkpoint(0);
    if (undo_isdirty()) ui_confirmdiscard(choosesongtoopen);
    else choosesongtoopen();
  }
}

static void onsongsaved(const char *path, gpointer data)
{
  void (*proceed)(void) = aftersave;

  aftersave = NULL;
  if (!splitpath(path, songpath, songfilename)) return;
  if (savesong())
  {
    undo_marksaved();
    ui_toast("Song saved");
    ui_refresh();
    if (proceed) proceed();
  }
  else
  {
    alert("Could Not Save the Song", "The file could not be written.");
    ui_refresh();
  }
}

// Ctrl+S: save over the current file, or ask for a name the first time
void ui_quicksave(void)
{
  char *path;

  if ((!strlen(loadedsongfilename)) || (!strlen(songpath)))
  {
    ui_savesong();
    return;
  }
  path = g_build_filename(songpath, loadedsongfilename, NULL);
  if (savesongfile(path))
  {
    undo_marksaved();
    ui_toast("Song saved");
  }
  else alert("Could Not Save the Song", "The file could not be written.");
  g_free(path);
  ui_refresh();
}

//
// Automatic backups
//

// Write a timestamped copy to ~/.goattrk/backups when the song has changed
// since the last backup, keeping the newest MAX_BACKUPS of each song
#define MAX_BACKUPS 20

static int comparenames(gconstpointer a, gconstpointer b)
{
  return strcmp(*(const char **)a, *(const char **)b);
}

void ui_backup(void)
{
  static unsigned lastversion = 0;
  char base[MAX_FILENAME];
  char *dir, *prefix, *name, *path, *dot;
  GDateTime *now;
  GDir *listing;
  GPtrArray *existing;
  const char *entry;

  if ((isplaying()) || (!undo_isdirty()) || (undo_version() == lastversion)) return;

  g_strlcpy(base, strlen(loadedsongfilename) ? loadedsongfilename : "untitled", sizeof base);
  dot = strrchr(base, '.');
  if (dot) *dot = 0;
  dir = g_build_filename(g_get_home_dir(), ".goattrk", "backups", NULL);
  g_mkdir_with_parents(dir, 0755);

  now = g_date_time_new_now_local();
  prefix = g_strdup_printf("%s-", base);
  name = g_date_time_format(now, "%Y%m%d-%H%M%S.sng");
  path = g_strdup_printf("%s/%s%s", dir, prefix, name);
  if (savesongfile(path)) lastversion = undo_version();

  // Remove the oldest backups of this song beyond MAX_BACKUPS
  existing = g_ptr_array_new_with_free_func(g_free);
  listing = g_dir_open(dir, 0, NULL);
  if (listing)
  {
    while ((entry = g_dir_read_name(listing)))
      if ((g_str_has_prefix(entry, prefix)) && (g_str_has_suffix(entry, ".sng"))) g_ptr_array_add(existing, g_strdup(entry));
    g_dir_close(listing);
  }
  g_ptr_array_sort(existing, comparenames);
  while (existing->len > MAX_BACKUPS)
  {
    char *old = g_build_filename(dir, g_ptr_array_index(existing, 0), NULL);
    g_remove(old);
    g_free(old);
    g_ptr_array_remove_index(existing, 0);
  }

  g_ptr_array_unref(existing);
  g_free(path);
  g_free(name);
  g_free(prefix);
  g_date_time_unref(now);
  g_free(dir);
}

void ui_savesong(void)
{
  aftersave = NULL;
  if (strlen(loadedsongfilename)) strcpy(songfilename, loadedsongfilename);
  choosefile("Save Song", songpath, songfilename, songfilter, "GoatTracker Songs", 1, onsongsaved, NULL);
}

//
// Unsaved changes
//

static void ondiscardresponse(AdwAlertDialog *dialog, const char *response, gpointer data)
{
  void (*proceed)(void) = data;

  if (!strcmp(response, "discard")) proceed();
  else if (!strcmp(response, "save"))
  {
    ui_savesong();
    // The save dialog may still be cancelled; only continue after saving
    aftersave = proceed;
  }
}

// Ask whether to save the song before an action that would lose it
void ui_confirmdiscard(void (*proceed)(void))
{
  char heading[MAX_FILENAME + 32];
  AdwDialog *dialog;

  snprintf(heading, sizeof heading, "Save Changes to “%s”?",
    strlen(loadedsongfilename) ? ui_toutf8(loadedsongfilename) : "Untitled");
  dialog = adw_alert_dialog_new(heading, "Unsaved changes will be lost if you don't save them.");
  adw_alert_dialog_add_responses(ADW_ALERT_DIALOG(dialog), "cancel", "Cancel",
    "discard", "Discard", "save", "Save…", NULL);
  adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(dialog), "discard", ADW_RESPONSE_DESTRUCTIVE);
  adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(dialog), "save", ADW_RESPONSE_SUGGESTED);
  adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(dialog), "save");
  adw_alert_dialog_set_close_response(ADW_ALERT_DIALOG(dialog), "cancel");
  g_signal_connect(dialog, "response", G_CALLBACK(ondiscardresponse), proceed);
  adw_dialog_present(dialog, GTK_WIDGET(mainwindow));
}

// Load the instrument at path into the current instrument slot
void ui_loadinstrumentpath(const char *path)
{
  if (!einum)
  {
    ui_toast("Select an instrument to load into first");
    return;
  }
  if (!checkfile(path, "GTI", "instrument")) return;
  if (!splitpath(path, instrpath, instrfilename)) return;
  loadinstrument();
  undo_checkpoint(0);
  ui_toast("Instrument loaded");
  ui_refresh();
}

static void oninstrumentopened(const char *path, gpointer data)
{
  if (!checkfile(path, "GTI", "instrument")) return;
  if (!splitpath(path, instrpath, instrfilename)) return;
  loadinstrument();
  undo_checkpoint(0);
  ui_toast("Instrument loaded");
  ui_refresh();
}

void ui_loadinstrument(void)
{
  if (!einum)
  {
    ui_toast("Select an instrument to load into first");
    return;
  }
  choosefile("Load Instrument", instrpath, instrfilename, instrfilter, "GoatTracker Instruments",
    0, oninstrumentopened, NULL);
}

static void oninstrumentsaved(const char *path, gpointer data)
{
  if (!splitpath(path, instrpath, instrfilename)) return;
  if (saveinstrument()) ui_toast("Instrument saved");
  else alert("Could Not Save the Instrument", "The file could not be written.");
}

void ui_saveinstrument(void)
{
  char name[MAX_FILENAME];

  if (!einum)
  {
    ui_toast("Select an instrument to save first");
    return;
  }
  // Default to the instrument's name, as the classic editor did
  if ((!strlen(instrfilename)) && (strlen(instr[einum].name)))
    snprintf(name, sizeof name, "%s.ins", instr[einum].name);
  else strcpy(name, instrfilename);
  choosefile("Save Instrument", instrpath, ui_toutf8(name), instrfilter, "GoatTracker Instruments",
    1, oninstrumentsaved, NULL);
}

// F10/F11 act on the instrument in the instrument and table editors
void ui_load(int merge)
{
  if ((editmode == EDIT_INSTRUMENT) || (editmode == EDIT_TABLES)) ui_loadinstrument();
  else ui_loadsong(merge);
}

void ui_save(void)
{
  if ((editmode == EDIT_INSTRUMENT) || (editmode == EDIT_TABLES)) ui_saveinstrument();
  else ui_savesong();
}

//
// Quit
//

static void onquitresponse(AdwAlertDialog *dialog, const char *response, gpointer data)
{
  if (!strcmp(response, "quit")) ui_quitnow();
}

void ui_quit(void)
{
  AdwDialog *dialog;

  undo_checkpoint(0);
  if (undo_isdirty())
  {
    ui_confirmdiscard(ui_quitnow);
    return;
  }
  dialog = adw_alert_dialog_new("Quit GoatTracker?", NULL);

  adw_alert_dialog_add_responses(ADW_ALERT_DIALOG(dialog), "cancel", "Cancel", "quit", "Quit", NULL);
  adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(dialog), "quit", ADW_RESPONSE_DESTRUCTIVE);
  adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(dialog), "cancel");
  g_signal_connect(dialog, "response", G_CALLBACK(onquitresponse), NULL);
  adw_dialog_present(dialog, GTK_WIDGET(mainwindow));
}

//
// Optimize / clear (Shift+ESC)
//

static GtkWidget *clearchecks[5];
static GtkWidget *clearlength;

static void onclearresponse(AdwAlertDialog *dialog, const char *response, gpointer data)
{
  int c, parts[5];

  if (!strcmp(response, "optimize"))
  {
    optimizeeverything(1, 1);
    ui_toast("Unused song data removed");
  }
  else if (!strcmp(response, "clear"))
  {
    for (c = 0; c < 5; c++) parts[c] = gtk_check_button_get_active(GTK_CHECK_BUTTON(clearchecks[c]));
    doclear(parts[0], parts[1], parts[2], parts[3], parts[4],
      (int)gtk_spin_button_get_value(GTK_SPIN_BUTTON(clearlength)));
  }
  // Both can be undone
  undo_checkpoint(0);
  panels_syncall();
  ui_refresh();
}

static void onpatternstoggled(GtkCheckButton *check, gpointer data)
{
  gtk_widget_set_sensitive(GTK_WIDGET(data), gtk_check_button_get_active(check));
}

void ui_clear(void)
{
  static const char *parts[] = {"Orderlists", "Patterns", "Instruments", "Tables", "Song name"};
  AdwDialog *dialog = adw_alert_dialog_new("Optimize or Clear the Song",
    "Optimizing removes unused patterns, instruments and table data. "
    "Clearing erases the parts chosen below.");
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  GtkWidget *lengthrow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  int c;

  for (c = 0; c < 5; c++)
  {
    clearchecks[c] = gtk_check_button_new_with_label(parts[c]);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(clearchecks[c]), TRUE);
    gtk_box_append(GTK_BOX(box), clearchecks[c]);
  }
  clearlength = gtk_spin_button_new_with_range(1, MAX_PATTROWS, 1);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(clearlength), defaultpatternlength);
  gtk_box_append(GTK_BOX(lengthrow), gtk_label_new("New pattern length"));
  gtk_box_append(GTK_BOX(lengthrow), clearlength);
  gtk_widget_set_margin_start(lengthrow, 28);
  gtk_box_append(GTK_BOX(box), lengthrow);
  g_signal_connect(clearchecks[1], "toggled", G_CALLBACK(onpatternstoggled), lengthrow);

  adw_alert_dialog_set_extra_child(ADW_ALERT_DIALOG(dialog), box);
  adw_alert_dialog_add_responses(ADW_ALERT_DIALOG(dialog), "cancel", "Cancel",
    "optimize", "Optimize", "clear", "Clear", NULL);
  adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(dialog), "clear", ADW_RESPONSE_DESTRUCTIVE);
  adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(dialog), "cancel");
  g_signal_connect(dialog, "response", G_CALLBACK(onclearresponse), NULL);
  adw_dialog_present(dialog, GTK_WIDGET(mainwindow));
}

//
// Hard restart ADSR (Shift+F7)
//

static void onadsrresponse(AdwAlertDialog *dialog, const char *response, gpointer data)
{
  const char *text = gtk_editable_get_text(GTK_EDITABLE(data));
  char *end;
  long v;

  if (strcmp(response, "apply")) return;
  v = strtol(text, &end, 16);
  if ((end == text) || (*end)) ui_toast("Enter four hexadecimal digits");
  else adparam = v & 0xffff;
  ui_refresh();
}

void ui_editadsr(void)
{
  AdwDialog *dialog = adw_alert_dialog_new("Hard Restart ADSR",
    "Attack/decay and sustain/release used for the hard restart before each note, "
    "as four hexadecimal digits. 0F00 is soft, 0000 hard, and an attack of F "
    "writes the waveform before the ADSR.");
  GtkWidget *entry = gtk_entry_new();
  char buf[8];

  sprintf(buf, "%04X", adparam);
  gtk_editable_set_text(GTK_EDITABLE(entry), buf);
  gtk_entry_set_max_length(GTK_ENTRY(entry), 4);
  gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
  gtk_widget_add_css_class(entry, "monospace");
  adw_alert_dialog_set_extra_child(ADW_ALERT_DIALOG(dialog), entry);
  adw_alert_dialog_add_responses(ADW_ALERT_DIALOG(dialog), "cancel", "Cancel", "apply", "Apply", NULL);
  adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(dialog), "apply", ADW_RESPONSE_SUGGESTED);
  adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(dialog), "apply");
  g_signal_connect(dialog, "response", G_CALLBACK(onadsrresponse), entry);
  adw_dialog_present(dialog, GTK_WIDGET(mainwindow));
}

//
// Pack, relocate & export (F9)
//

static AdwDialog *exportdialog;
static GtkWidget *optionrows[MAX_OPTIONS];
static GtkWidget *playeradrspin, *zeropagespin, *formatrow;
static int updatingoptions = 0;

static const char *formatext[] = {"sid", "prg", "bin"};

static void showoptions(void)
{
  int c;

  updatingoptions = 1;
  for (c = 0; c < MAX_OPTIONS; c++)
    adw_switch_row_set_active(ADW_SWITCH_ROW(optionrows[c]), (playerversion & (PLAYER_BUFFERED << c)) != 0);
  updatingoptions = 0;
}

static void onoptiontoggled(GObject *row, GParamSpec *pspec, gpointer data)
{
  int c = GPOINTER_TO_INT(data);

  if (updatingoptions) return;
  if (adw_switch_row_get_active(ADW_SWITCH_ROW(row))) playerversion |= (PLAYER_BUFFERED << c);
  else playerversion &= ~(PLAYER_BUFFERED << c);
  // Sound effects, zeropage ghostregs and full buffering need buffered writes
  // Writing the zeropage ghostregs to the SID needs them
  if ((c) && (playerversion & PLAYER_ZPPLAYSID))
    playerversion |= PLAYER_ZPGHOSTREGS;
  if (c)
  {
    if (playerversion & (PLAYER_SOUNDEFFECTS | PLAYER_ZPGHOSTREGS | PLAYER_FULLBUFFERED))
      playerversion |= PLAYER_BUFFERED;
  }
  else if (!(playerversion & PLAYER_BUFFERED))
    playerversion &= ~(PLAYER_SOUNDEFFECTS | PLAYER_ZPGHOSTREGS | PLAYER_FULLBUFFERED);
  if (!(playerversion & PLAYER_ZPGHOSTREGS))
    playerversion &= ~PLAYER_ZPPLAYSID;
  showoptions();
}

static void onexported(const char *path, gpointer data)
{
  char dir[MAX_PATHNAME];
  char *log = NULL;
  size_t loglen = 0;
  AdwDialog *dialog;
  GtkWidget *label;

  if (strlen(path) >= MAX_PATHNAME)
  {
    ui_toast("The file name or path is too long");
    return;
  }
  strcpy(packedsongname, path);
  {
    char *dirname = g_path_get_dirname(path);
    g_strlcpy(dir, dirname, sizeof dir);
    g_free(dirname);
  }
  strcpy(packedpath, dir);

  // The packer writes its report to relocout
  relocout = open_memstream(&log, &loglen);
  relocator();
  fclose(relocout);
  relocout = NULL;

  dialog = adw_alert_dialog_new(relocsuccess ? "Export Complete" : "Export Failed", NULL);
  label = gtk_label_new(log);
  gtk_widget_add_css_class(label, "monospace");
  gtk_label_set_xalign(GTK_LABEL(label), 0);
  adw_alert_dialog_set_extra_child(ADW_ALERT_DIALOG(dialog), label);
  adw_alert_dialog_add_response(ADW_ALERT_DIALOG(dialog), "close", "Close");
  adw_dialog_present(dialog, GTK_WIDGET(mainwindow));
  free(log);
  ui_refresh();
}

static void onexportclicked(GtkButton *button, gpointer data)
{
  char name[MAX_FILENAME + 8];
  char filter[8];
  int c;

  playeradr = ((int)gtk_spin_button_get_value(GTK_SPIN_BUTTON(playeradrspin))) & 0xff00;
  zeropageadr = (int)gtk_spin_button_get_value(GTK_SPIN_BUTTON(zeropagespin));
  // The zeropage ghost registers take 25 bytes
  if (playerversion & PLAYER_ZPGHOSTREGS) zeropageadr = CLAMP(zeropageadr, 0x02, 0xe5);
  fileformat = adw_combo_row_get_selected(ADW_COMBO_ROW(formatrow));
  adw_dialog_close(exportdialog);

  // Default to the song's name with the format's extension
  memset(name, 0, sizeof name);
  for (c = 0; (c < (int)strlen(loadedsongfilename)) && (loadedsongfilename[c] != '.'); c++)
    name[c] = loadedsongfilename[c];
  if (!strlen(name)) strcpy(name, "song");
  strcat(name, ".");
  strcat(name, formatext[fileformat]);
  sprintf(filter, "*.%s", formatext[fileformat]);
  choosefile("Export Packed Song", packedpath, ui_toutf8(name), filter, "C64 Music Files", 1, onexported, NULL);
}

// Ctrl+F9: pack again to the last export's file with the same options
void ui_exportagain(void)
{
  if (!strlen(packedsongname))
  {
    ui_relocator();
    return;
  }
  onexported(packedsongname, NULL);
}

static void onplayorder(AdwSwitchRow *row, GParamSpec *pspec, gpointer data)
{
  packplayorder = adw_switch_row_get_active(row);
}

void ui_relocator(void)
{
  static const char *formats[] = {"SID – SIDPlay music file", "PRG – C64 program with load address",
    "BIN – Raw binary, no load address", NULL};
  GtkWidget *view = adw_toolbar_view_new();
  GtkWidget *header = adw_header_bar_new();
  GtkWidget *page = adw_preferences_page_new();
  GtkWidget *group;
  GtkWidget *row;
  GtkWidget *exportbutton = gtk_button_new_with_label("Export…");
  int c;

  exportdialog = adw_dialog_new();
  adw_dialog_set_title(exportdialog, "Pack, Relocate & Export");
  adw_dialog_set_content_width(exportdialog, 520);
  adw_dialog_set_content_height(exportdialog, 640);

  gtk_widget_add_css_class(exportbutton, "suggested-action");
  g_signal_connect(exportbutton, "clicked", G_CALLBACK(onexportclicked), NULL);
  adw_header_bar_pack_end(ADW_HEADER_BAR(header), exportbutton);
  adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(view), header);

  group = adw_preferences_group_new();
  adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(group), "Playroutine");
  for (c = 0; c < MAX_OPTIONS; c++)
  {
    optionrows[c] = adw_switch_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(optionrows[c]), playeroptname[c]);
    g_signal_connect(optionrows[c], "notify::active", G_CALLBACK(onoptiontoggled), GINT_TO_POINTER(c));
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), optionrows[c]);
  }
  adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(group));
  showoptions();

  group = adw_preferences_group_new();
  adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(group), "Memory");
  row = adw_action_row_new();
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), "Player address");
  adw_action_row_set_subtitle(ADW_ACTION_ROW(row), "Start of the playroutine, in whole pages");
  playeradrspin = hexspin_new(0xff00, 4);
  gtk_spin_button_set_increments(GTK_SPIN_BUTTON(playeradrspin), 0x100, 0x400);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(playeradrspin), playeradr);
  gtk_widget_set_valign(playeradrspin, GTK_ALIGN_CENTER);
  adw_action_row_add_suffix(ADW_ACTION_ROW(row), playeradrspin);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), row);

  row = adw_action_row_new();
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), "Zeropage address");
  adw_action_row_set_subtitle(ADW_ACTION_ROW(row), "Two bytes from here ($FB–$FE are unused by BASIC and KERNAL)");
  zeropagespin = hexspin_new(0xfe, 2);
  gtk_spin_button_set_range(GTK_SPIN_BUTTON(zeropagespin), 0x02, 0xfe);
  gtk_spin_button_set_increments(GTK_SPIN_BUTTON(zeropagespin), 1, 0x10);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(zeropagespin), zeropageadr);
  gtk_widget_set_valign(zeropagespin, GTK_ALIGN_CENTER);
  adw_action_row_add_suffix(ADW_ACTION_ROW(row), zeropagespin);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), row);
  adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(group));

  group = adw_preferences_group_new();
  adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(group), "Output");
  formatrow = adw_combo_row_new();
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(formatrow), "Format");
  adw_combo_row_set_model(ADW_COMBO_ROW(formatrow), G_LIST_MODEL(gtk_string_list_new(formats)));
  adw_combo_row_set_selected(ADW_COMBO_ROW(formatrow), fileformat);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), formatrow);
  {
    GtkWidget *row = adw_switch_row_new();

    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), "Patterns in Play Order");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(row), "Store the patterns in the order the song first plays them, "
      "rather than by pattern number. The music plays the same.");
    adw_switch_row_set_active(ADW_SWITCH_ROW(row), packplayorder);
    g_signal_connect(row, "notify::active", G_CALLBACK(onplayorder), NULL);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), row);
  }
  adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(group));

  adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(view), page);
  adw_dialog_set_child(exportdialog, view);
  adw_dialog_present(exportdialog, GTK_WIDGET(mainwindow));
}

//
// Help (F12; Shift+F12 starts with the current editor's sections)
//

static void addhelpsection(GtkWidget *box, const HELPSECTION *section)
{
  GtkWidget *title = gtk_label_new(section->title);
  GString *text = g_string_new(NULL);
  GtkWidget *label;
  int c;

  for (c = 0; section->rows[c]; c++)
  {
    char *row = g_strchomp(g_strdup(section->rows[c]));
    if (c) g_string_append_c(text, '\n');
    g_string_append(text, row);
    g_free(row);
  }
  label = gtk_label_new(text->str);
  g_string_free(text, TRUE);

  gtk_label_set_xalign(GTK_LABEL(title), 0);
  gtk_widget_add_css_class(title, "title-4");
  gtk_widget_set_margin_top(title, 12);
  gtk_label_set_xalign(GTK_LABEL(label), 0);
  gtk_widget_add_css_class(label, "monospace");
  gtk_box_append(GTK_BOX(box), title);
  gtk_box_append(GTK_BOX(box), label);
}

void ui_help(int context)
{
  AdwDialog *dialog = adw_dialog_new();
  GtkWidget *view = adw_toolbar_view_new();
  GtkWidget *scroll = gtk_scrolled_window_new();
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  const HELPSECTION *sections = gethelpsections();
  int c;

  adw_dialog_set_title(dialog, "Keyboard Help");
  adw_dialog_set_content_width(dialog, 820);
  adw_dialog_set_content_height(dialog, 700);
  adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(view), adw_header_bar_new());

  gtk_widget_set_margin_start(box, 18);
  gtk_widget_set_margin_end(box, 18);
  gtk_widget_set_margin_bottom(box, 18);
  // In context mode the current editor's sections come first
  if (context)
  {
    for (c = 0; sections[c].title; c++)
      if (helpsectionforeditmode(&sections[c], editmode)) addhelpsection(box, &sections[c]);
  }
  for (c = 0; sections[c].title; c++)
    if ((!context) || (!helpsectionforeditmode(&sections[c], editmode))) addhelpsection(box, &sections[c]);

  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), box);
  adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(view), scroll);
  adw_dialog_set_child(dialog, view);
  adw_dialog_present(dialog, GTK_WIDGET(mainwindow));
}

void ui_showsoundfailure(void)
{
  alert("Sound Could Not Be Started", "GoatTracker will run without sound output, and the song timer won't advance. "
    "Try a larger buffer (-B) or a different mixing rate (-M).");
}

//
// Preferences
//

typedef enum
{
  PREF_BUFFER, PREF_MIXRATE, PREF_INTERPOLATION, PREF_TIMING, PREF_HARDSID, PREF_CATWEASEL,
  PREF_FINEVIB, PREF_PULSEOPT, PREF_REALTIMEOPT, PREF_BACKUPS, PREF_VOLUME, PREF_DETUNE, PREF_AUTONEXT
} PREFERENCE;

static const unsigned mixrates[] = {11025, 22050, 32000, 44100, 48000};

static void onprefspin(AdwSpinRow *row, GParamSpec *pspec, gpointer data)
{
  int v = (int)adw_spin_row_get_value(row);

  switch (GPOINTER_TO_INT(data))
  {
    case PREF_BUFFER: b = v; break;
    case PREF_VOLUME: mastervolume = v; ui_refresh(); break;
    case PREF_DETUNE: ui_setdetune(v); break;
    case PREF_HARDSID: hardsid = v; ui_restartsound(); break;
    case PREF_BACKUPS: settings_backupinterval = v; ui_backupschanged(); break;
  }
}

static void onprefcombo(AdwComboRow *row, GParamSpec *pspec, gpointer data)
{
  unsigned v = adw_combo_row_get_selected(row);

  switch (GPOINTER_TO_INT(data))
  {
    case PREF_MIXRATE: mr = mixrates[v]; break;
    case PREF_INTERPOLATION: interpolate = v; ui_restartsound(); break;
    case PREF_TIMING:
    ntsc = v;
    customclockrate = 0;
    ui_restartsound();
    break;
  }
  ui_refresh();
}

static void onprefswitch(AdwSwitchRow *row, GParamSpec *pspec, gpointer data)
{
  unsigned v = adw_switch_row_get_active(row);

  switch (GPOINTER_TO_INT(data))
  {
    case PREF_CATWEASEL: catweasel = v; ui_restartsound(); break;
    case PREF_FINEVIB: usefinevib = v; finevibrato = v; break;
    case PREF_PULSEOPT: optimizepulse = v; break;
    case PREF_REALTIMEOPT: optimizerealtime = v; break;
    case PREF_AUTONEXT: autonextpattern = v; break;
  }
  ui_refresh();
}

static GtkWidget *prefgroup(GtkWidget *page, const char *title, const char *description)
{
  GtkWidget *group = adw_preferences_group_new();

  adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(group), title);
  if (description) adw_preferences_group_set_description(ADW_PREFERENCES_GROUP(group), description);
  adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(group));
  return group;
}

static GtkWidget *prefspin(GtkWidget *group, const char *title, const char *subtitle, int min, int max,
  int step, int value, PREFERENCE pref)
{
  GtkWidget *row = adw_spin_row_new_with_range(min, max, step);

  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
  if (subtitle) adw_action_row_set_subtitle(ADW_ACTION_ROW(row), subtitle);
  adw_spin_row_set_value(ADW_SPIN_ROW(row), value);
  g_signal_connect(row, "notify::value", G_CALLBACK(onprefspin), GINT_TO_POINTER(pref));
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), row);
  return row;
}

static GtkWidget *prefcombo(GtkWidget *group, const char *title, const char *subtitle, const char *const *items,
  int selected, PREFERENCE pref)
{
  GtkWidget *row = adw_combo_row_new();
  GtkStringList *list = gtk_string_list_new(items);

  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
  if (subtitle) adw_action_row_set_subtitle(ADW_ACTION_ROW(row), subtitle);
  adw_combo_row_set_model(ADW_COMBO_ROW(row), G_LIST_MODEL(list));
  g_object_unref(list);
  adw_combo_row_set_selected(ADW_COMBO_ROW(row), selected);
  g_signal_connect(row, "notify::selected", G_CALLBACK(onprefcombo), GINT_TO_POINTER(pref));
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), row);
  return row;
}

static GtkWidget *prefswitch(GtkWidget *group, const char *title, const char *subtitle, int active, PREFERENCE pref)
{
  GtkWidget *row = adw_switch_row_new();

  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
  if (subtitle) adw_action_row_set_subtitle(ADW_ACTION_ROW(row), subtitle);
  adw_switch_row_set_active(ADW_SWITCH_ROW(row), active);
  g_signal_connect(row, "notify::active", G_CALLBACK(onprefswitch), GINT_TO_POINTER(pref));
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), row);
  return row;
}

static void onmidiinput(AdwComboRow *row, GParamSpec *pspec, gpointer data)
{
  GtkStringObject *item = adw_combo_row_get_selected_item(row);
  const char *name;

  if ((!item) || (!adw_combo_row_get_selected(row))) name = "";
  else name = gtk_string_object_get_string(item);
  if (!midi_setinput(name)) ui_toast("That MIDI input could not be connected");
}

static void onshowbackups(GtkButton *button, gpointer data)
{
  char *dir = g_build_filename(g_get_home_dir(), ".goattrk", "backups", NULL);
  GFile *folder;
  GtkFileLauncher *launcher;

  g_mkdir_with_parents(dir, 0755);
  folder = g_file_new_for_path(dir);
  launcher = gtk_file_launcher_new(folder);
  gtk_file_launcher_launch(launcher, mainwindow, NULL, NULL, NULL);
  g_object_unref(launcher);
  g_object_unref(folder);
  g_free(dir);
}

void ui_preferences(void)
{
  static const char *mixratenames[] = {"11025 Hz", "22050 Hz", "32000 Hz", "44100 Hz", "48000 Hz", NULL};
  static const char *interpolations[] = {"Fast", "Interpolated", "reSID-fp (distortion)",
    "reSID-fp, interpolated", NULL};
  static const char *timings[] = {"PAL (50 Hz)", "NTSC (60 Hz)", NULL};
  AdwDialog *dialog = adw_preferences_dialog_new();
  GtkWidget *page = adw_preferences_page_new();
  GtkWidget *group, *row, *button;
  int c, rate = 3;

  for (c = 0; c < (int)G_N_ELEMENTS(mixrates); c++) if (mixrates[c] == mr) rate = c;

  adw_preferences_page_set_title(ADW_PREFERENCES_PAGE(page), "General");
  adw_preferences_page_set_icon_name(ADW_PREFERENCES_PAGE(page), "preferences-system-symbolic");

  group = prefgroup(page, "Sound Output", "Buffer and mixing rate take effect after restarting GoatTracker.");
  prefspin(group, "Buffer Length", "Milliseconds; raise it if playback stutters", MINBUF, MAXBUF, 10, b, PREF_BUFFER);
  prefcombo(group, "Mixing Rate", NULL, mixratenames, rate, PREF_MIXRATE);
  prefcombo(group, "SID Emulation", "reSID-fp models the 6581 filter distortion; interpolation costs more CPU",
    interpolations, interpolate & 3, PREF_INTERPOLATION);
  prefcombo(group, "Timing", NULL, timings, ntsc & 1, PREF_TIMING);
  prefspin(group, "Volume", "Percent; exported WAV files are not affected", 0, 100, 5, mastervolume, PREF_VOLUME);
  prefspin(group, "Detune", "Cents; runs the emulated SID slightly fast or slow to match other instruments. "
    "WAV exports are detuned too.", -100, 100, 1, sid_detune, PREF_DETUNE);

  {
    // MIDI sources from the ALSA sequencer; the saved one stays listed when
    // it isn't plugged in
    char **sources = midi_listsources();
    GtkStringList *list = gtk_string_list_new(NULL);
    int selected = 0;

    gtk_string_list_append(list, "Off");
    for (c = 0; sources[c]; c++)
    {
      gtk_string_list_append(list, sources[c]);
      if (!strcmp(sources[c], midi_inputname)) selected = c + 1;
    }
    if ((midi_inputname[0]) && (!selected))
    {
      gtk_string_list_append(list, midi_inputname);
      selected = c + 1;
    }
    g_strfreev(sources);

    group = prefgroup(page, "MIDI", NULL);
    row = adw_combo_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), "MIDI Input");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(row), "Notes are entered at the pattern cursor in edit mode, "
      "and played on any free channel in jam mode");
    adw_combo_row_set_model(ADW_COMBO_ROW(row), G_LIST_MODEL(list));
    g_object_unref(list);
    adw_combo_row_set_selected(ADW_COMBO_ROW(row), selected);
    g_signal_connect(row, "notify::selected", G_CALLBACK(onmidiinput), NULL);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), row);
  }

  group = prefgroup(page, "Hardware", NULL);
  prefspin(group, "HardSID Device", "0 = off, 1 = first device, and so on", 0, 8, 1, hardsid, PREF_HARDSID);
  prefswitch(group, "CatWeasel MK3 SID", NULL, catweasel, PREF_CATWEASEL);

  group = prefgroup(page, "Playroutine", "Saved with each song. They affect the editor's playback and the packed music.");
  prefswitch(group, "Fine Vibrato", "Convert old-style vibrato parameters to finer speedtable values", usefinevib, PREF_FINEVIB);
  prefswitch(group, "Pulse Optimization", "Skip pulsetable execution when a new note or pattern starts", optimizepulse, PREF_PULSEOPT);
  prefswitch(group, "Realtime Command Optimization", "Skip portamento and vibrato on the first tick of each row",
    optimizerealtime, PREF_REALTIMEOPT);

  group = prefgroup(page, "Editor", NULL);
  row = prefspin(group, "Automatic Backups", "Seconds between backups of changed songs; 0 turns them off",
    0, 600, 10, settings_backupinterval, PREF_BACKUPS);
  button = gtk_button_new_from_icon_name("folder-open-symbolic");
  gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
  gtk_widget_set_tooltip_text(button, "Show the backups folder");
  gtk_widget_add_css_class(button, "flat");
  g_signal_connect(button, "clicked", G_CALLBACK(onshowbackups), NULL);
  adw_action_row_add_suffix(ADW_ACTION_ROW(row), button);
  prefswitch(group, "Continue into the Next Pattern", "Moving the cursor past the end of a pattern goes on to "
    "the next pattern in the channel's orderlist", autonextpattern, PREF_AUTONEXT);

  adw_preferences_dialog_add(ADW_PREFERENCES_DIALOG(dialog), ADW_PREFERENCES_PAGE(page));
  adw_dialog_present(dialog, GTK_WIDGET(mainwindow));
}
