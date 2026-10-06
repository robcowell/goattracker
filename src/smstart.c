//
// SidMonkey: getting started
//
// The welcome dialog (a starter song, an empty song, the example songs, or
// a song of your own), the starter song itself, and "Learn the SID", a short
// guide to what the chip can do and how SidMonkey shows it.
//

#include "sm.h"
#include "groll.h"

static AdwDialog *welcome;

//
// The starter song: a two-bar beat, a bass line and a short melody, each
// played twice, with preset instruments
//

typedef struct
{
  int row, len;
  int note;      // semitones from C-0
  int instr;
} STARTNOTE;

static void writepattern(int patt, const STARTNOTE *notes, int n)
{
  static ROLL roll;
  int i;

  roll_setlength(patt, 32);
  roll_read(patt, &roll);
  roll.nnotes = 0;
  for (i = 0; i < n; i++)
  {
    ROLLNOTE *r = &roll.notes[roll.nnotes++];
    memset(r, 0, sizeof *r);
    r->start = notes[i].row;
    r->len = notes[i].len;
    r->note = FIRSTNOTE + notes[i].note;
    r->instr = notes[i].instr;
    r->keyoff = 1;
  }
  roll_write(patt, &roll);
}

static void writevoice(int chnum, int patt)
{
  CLIP clips[2];
  int i;

  memset(clips, 0, sizeof clips);
  for (i = 0; i < 2; i++)
  {
    clips[i].patt = patt;
    clips[i].orderpos = -1;
  }
  clips[0].flags = CLIP_LOOPSTART;
  arr_write(0, chnum, clips, 2);
}

static const PRESET *preset(const char *name)
{
  int i;

  for (i = 0; i < numpresets; i++)
    if (!strcmp(presets[i].name, name)) return &presets[i];
  return &presets[0];
}

static void starter(void)
{
  // Instruments 1-5
  static const char *sounds[] = {"Kick", "Snare", "Closed Hi-Hat", "Pulse Bass", "Square Lead"};
  static const STARTNOTE drums[] = {
    {0, 4, 48, 1}, {4, 4, 48, 3}, {8, 4, 48, 2}, {12, 4, 48, 3},
    {16, 4, 48, 1}, {20, 2, 48, 1}, {22, 2, 48, 3}, {24, 4, 48, 2}, {28, 4, 48, 3}};
  static const STARTNOTE bass[] = {
    {0, 6, 24, 4}, {6, 2, 24, 4}, {8, 4, 36, 4}, {12, 4, 27, 4},
    {16, 6, 29, 4}, {22, 2, 29, 4}, {24, 4, 31, 4}, {28, 4, 34, 4}};
  static const STARTNOTE melody[] = {
    {0, 6, 48, 5}, {8, 4, 51, 5}, {12, 4, 53, 5}, {16, 10, 55, 5}, {28, 4, 58, 5}};
  int i;

  host_stop();
  clearsong(1, 1, 1, 1, 1);
  for (i = 0; i < 5; i++) recipe_build(i + 1, &preset(sounds[i])->recipe, sounds[i]);
  writepattern(0, drums, G_N_ELEMENTS(drums));
  writepattern(1, bass, G_N_ELEMENTS(bass));
  writepattern(2, melody, G_N_ELEMENTS(melody));
  for (i = 0; i < MAX_CHN; i++) writevoice(i, i);
  countpatternlengths();
  recipe_updatefilters();
  strcpy(songname, "My First Tune");
  sm_newsong();
  sm_toast("Press Space to hear it, then click a clip to change its notes");
}

//
// Welcome
//

static void closewelcome(void)
{
  if (welcome) adw_dialog_close(welcome);
  welcome = NULL;
}

static void onstarter(GtkButton *button, gpointer data)
{
  closewelcome();
  starter();
}

static void onempty(GtkButton *button, gpointer data)
{
  closewelcome();
}

static void onopensong(GtkButton *button, gpointer data)
{
  closewelcome();
  gtk_widget_activate_action(GTK_WIDGET(sm_window), "win.open", NULL);
}

static void onexample(GtkListBox *box, GtkListBoxRow *row, gpointer data)
{
  const char *path = g_object_get_data(G_OBJECT(row), "path");
  char *copy = g_strdup(path);

  closewelcome();
  sm_opensong(copy);
  g_free(copy);
}

// The examples folder next to the program (../examples from linux/)
static char *examplesdir(void)
{
  char *exe = g_file_read_link("/proc/self/exe", NULL);
  char *dir, *examples;

  if (!exe) return NULL;
  dir = g_path_get_dirname(exe);
  examples = g_build_filename(dir, "..", "examples", NULL);
  g_free(exe);
  g_free(dir);
  if (!g_file_test(examples, G_FILE_TEST_IS_DIR))
  {
    g_free(examples);
    return NULL;
  }
  return examples;
}

static GtkWidget *bigbutton(const char *title, const char *subtitle, const char *icon, GCallback func)
{
  GtkWidget *button = gtk_button_new();
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  GtkWidget *text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  GtkWidget *label = gtk_label_new(title);
  GtkWidget *sub = gtk_label_new(subtitle);

  gtk_box_append(GTK_BOX(box), gtk_image_new_from_icon_name(icon));
  gtk_widget_add_css_class(label, "heading");
  gtk_label_set_xalign(GTK_LABEL(label), 0);
  gtk_label_set_xalign(GTK_LABEL(sub), 0);
  gtk_label_set_wrap(GTK_LABEL(sub), TRUE);
  gtk_widget_add_css_class(sub, "dim-label");
  gtk_box_append(GTK_BOX(text), label);
  gtk_box_append(GTK_BOX(text), sub);
  gtk_box_append(GTK_BOX(box), text);
  gtk_widget_set_margin_top(box, 8);
  gtk_widget_set_margin_bottom(box, 8);
  gtk_button_set_child(GTK_BUTTON(button), box);
  g_signal_connect(button, "clicked", func, NULL);
  return button;
}

void start_welcome(void)
{
  GtkWidget *view = adw_toolbar_view_new();
  GtkWidget *header = adw_header_bar_new();
  GtkWidget *scroll = gtk_scrolled_window_new();
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  GtkWidget *label;
  char *dir = examplesdir();

  welcome = adw_dialog_new();
  adw_dialog_set_title(welcome, "Welcome to SidMonkey");
  adw_dialog_set_content_width(welcome, 480);
  adw_dialog_set_content_height(welcome, 600);
  adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(view), header);
  gtk_widget_set_margin_start(box, 18);
  gtk_widget_set_margin_end(box, 18);
  gtk_widget_set_margin_top(box, 12);
  gtk_widget_set_margin_bottom(box, 18);

  label = gtk_label_new("Make music for the Commodore 64's SID chip: three voices, four waveforms and one "
    "filter. Start with something that already plays, and change it bit by bit.");
  gtk_label_set_wrap(GTK_LABEL(label), TRUE);
  gtk_label_set_xalign(GTK_LABEL(label), 0);
  gtk_box_append(GTK_BOX(box), label);

  gtk_box_append(GTK_BOX(box), bigbutton("Start with a Beat", "Drums, a bass line and a melody to change",
    "media-playback-start-symbolic", G_CALLBACK(onstarter)));
  gtk_box_append(GTK_BOX(box), bigbutton("Empty Song", "Three empty voices",
    "document-new-symbolic", G_CALLBACK(onempty)));
  gtk_box_append(GTK_BOX(box), bigbutton("Open a Song…", "A GoatTracker song (.sng) of your own",
    "document-open-symbolic", G_CALLBACK(onopensong)));

  if (dir)
  {
    GDir *d = g_dir_open(dir, 0, NULL);
    GPtrArray *names = g_ptr_array_new_with_free_func(g_free);
    const char *name;
    GtkWidget *list;
    guint i;

    while ((d) && ((name = g_dir_read_name(d))))
      if (g_str_has_suffix(name, ".sng")) g_ptr_array_add(names, g_strdup(name));
    if (d) g_dir_close(d);
    g_ptr_array_sort_values(names, (GCompareFunc)g_strcmp0);
    if (names->len)
    {
      label = gtk_label_new("Example Songs");
      gtk_widget_add_css_class(label, "heading");
      gtk_label_set_xalign(GTK_LABEL(label), 0);
      gtk_widget_set_margin_top(label, 6);
      gtk_box_append(GTK_BOX(box), label);
      list = gtk_list_box_new();
      gtk_widget_add_css_class(list, "boxed-list");
      gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
      g_signal_connect(list, "row-activated", G_CALLBACK(onexample), NULL);
      for (i = 0; i < names->len; i++)
      {
        GtkWidget *row = adw_action_row_new();
        char *base = g_strdup(g_ptr_array_index(names, i));
        *strrchr(base, '.') = 0;
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), base);
        gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), TRUE);
        g_object_set_data_full(G_OBJECT(row), "path", g_build_filename(dir, g_ptr_array_index(names, i), NULL), g_free);
        gtk_list_box_append(GTK_LIST_BOX(list), row);
        g_free(base);
      }
      gtk_box_append(GTK_BOX(box), list);
    }
    g_ptr_array_unref(names);
    g_free(dir);
  }

  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), box);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(view), scroll);
  adw_dialog_set_child(welcome, view);
  adw_dialog_present(welcome, GTK_WIDGET(sm_window));
}

void start_newfromtemplate(void)
{
  starter();
}

//
// Learn the SID
//

static const struct
{
  const char *title, *text;
} lessons[] = {
  {"Three voices", "The SID chip plays three notes at once, one on each voice. SidMonkey shows each voice as a "
    "lane. A voice plays one note at a time, so a new note cuts off the one before it. Chords come from flicking "
    "very quickly between notes: that is what an instrument's Chord setting does."},
  {"Four waveforms", "Triangle is soft and flute-like. Saw is bright and buzzy. Pulse is hollow or nasal "
    "depending on its width (8 is a square wave), and sweeping the width gives the shimmering sound the C64 is "
    "known for. Noise is a hiss, for drums and effects. Triangle, saw and pulse can be combined, though the result "
    "depends on the SID model."},
  {"The envelope", "Every note's volume follows an envelope. Attack is how quickly it starts, Decay how quickly it "
    "falls to the Sustain level, which it keeps while the note is held, and Release how long it fades after the "
    "note ends. Short attack and decay with no sustain gives plucks and drums; slow attack gives pads."},
  {"One filter", "The filter darkens (low-pass), thins (high-pass) or hollows out (band-pass) the sound, and "
    "Resonance makes it ring. The chip has only one, shared by the voices: an instrument with a filter takes it "
    "over for every voice it is set to filter, so use it on one instrument at a time."},
  {"Clips and patterns", "A clip plays a pattern: a short run of notes. Several clips can play the same pattern, "
    "so a chorus can repeat without copying it. Changing the notes changes every clip that plays the pattern; use "
    "Make Pattern Unique to change one on its own. Clips can be transposed without changing their notes."},
  {"Time", "The music moves in rows, like the steps of a drum machine. By default a row lasts 6 frames of the "
    "C64's 50 a second, and a beat is 4 rows: about 125 beats a minute. Note lengths in the piano roll are in "
    "these rows and beats."},
  {"Going further", "SidMonkey's songs are GoatTracker songs. Open one in GoatTracker for full control, or look "
    "at an instrument's Under the Hood section to see the tables GoatTracker uses. Export SID files to play them "
    "in SID players and C64 emulators."},
};

void start_learn(void)
{
  AdwDialog *dialog = adw_dialog_new();
  GtkWidget *view = adw_toolbar_view_new();
  GtkWidget *page = adw_preferences_page_new();
  int i;

  adw_dialog_set_title(dialog, "Learn the SID");
  adw_dialog_set_content_width(dialog, 520);
  adw_dialog_set_content_height(dialog, 640);
  adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(view), adw_header_bar_new());
  for (i = 0; i < (int)G_N_ELEMENTS(lessons); i++)
  {
    GtkWidget *group = adw_preferences_group_new();
    GtkWidget *label = gtk_label_new(lessons[i].text);
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(group), lessons[i].title);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), label);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(group));
  }
  adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(view), page);
  adw_dialog_set_child(dialog, view);
  adw_dialog_present(dialog, GTK_WIDGET(sm_window));
}
