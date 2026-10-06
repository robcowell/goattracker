//
// SidMonkey: the instruments sidebar
//
// The song's instruments, a library of preset sounds (grecipe.c), and a
// designer for instruments made from a recipe: plain controls for the
// waveform, envelope, chord, pulse sweep, filter and vibrato, each change
// rebuilding the instrument and its table programs. Instruments made in
// GoatTracker have no recipe; they can be played, renamed, have their
// envelope changed, or be replaced with a preset.
//

#include "sm.h"

static GtkWidget *listbox, *designer, *customgroup, *recipegroups[6], *hoodlabel, *envarea;
static GtkWidget *nameentry, *changebutton, *stack, *customlabel;
static GtkWidget *kindrow, *waverow, *wavebuttons[4], *clickrow, *hardrow, *pitchrow;
static GtkWidget *chordrow, *chordspeedrow, *widthrow, *pwmrow, *pwmspeedrow;
static GtkWidget *filterrow, *cutoffrow, *resrow, *sweeprow, *sweeptimerow, *vibrow, *vibdelayrow;
static GtkWidget *adsr[4];
static int current = 1;
static int syncing;

// Undo merges the steps of one drag or one library visit
#define COALESCE_DESIGN 0x5d00
#define COALESCE_LIBRARY 0x5e00

static void syncdesigner(void);

static int isemptyinstr(int c)
{
  const INSTR *in = &instr[c];
  int p, row;

  if ((in->name[0]) || (in->ad) || (in->sr) || (in->ptr[0]) || (in->ptr[1]) || (in->ptr[2]) || (in->ptr[3])) return 0;
  for (p = 0; p < MAX_PATT; p++)
    for (row = 0; row < pattlen[p]; row++)
      if (pattern[p][row * 4 + 1] == c) return 0;
  return 1;
}

static int lastshown(void)
{
  int c, n = 0;

  for (c = 1; c < MAX_INSTR; c++)
    if (!isemptyinstr(c)) n = c;
  return n;
}

static void releasevoice0(gpointer data)
{
  host_release(0);
}

static void preview(int instrnum)
{
  if (isplaying()) return;
  host_preview(FIRSTNOTE + 36, instrnum, 0);
  g_timeout_add_once(350, releasevoice0, NULL);
}

//
// The list of instruments
//

static void onrowselected(GtkListBox *box, GtkListBoxRow *row, gpointer data)
{
  if ((syncing) || (!row)) return;
  current = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "instr"));
  roll_setinstrument(current);
  syncdesigner();
}

void sound_refresh(void)
{
  GtkWidget *child;
  int c, n = lastshown();

  syncing = 1;
  while ((child = gtk_widget_get_first_child(listbox))) gtk_list_box_remove(GTK_LIST_BOX(listbox), child);
  if (current > n + 1) current = n ? n : 1;
  for (c = 1; c <= n; c++)
  {
    GtkWidget *row = adw_action_row_new();
    char title[MAX_INSTRNAMELEN * 2 + 8];

    snprintf(title, sizeof title, "%02X  %s", c, instr[c].name[0] ? sm_toutf8(instr[c].name) : "(no name)");
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
    adw_preferences_row_set_use_markup(ADW_PREFERENCES_ROW(row), FALSE);
    adw_action_row_set_subtitle(ADW_ACTION_ROW(row), recipe_get(c) ? "Preset sound" :
      (isemptyinstr(c) ? "Empty" : "Made in GoatTracker"));
    g_object_set_data(G_OBJECT(row), "instr", GINT_TO_POINTER(c));
    gtk_list_box_append(GTK_LIST_BOX(listbox), row);
    if (c == current) gtk_list_box_select_row(GTK_LIST_BOX(listbox), GTK_LIST_BOX_ROW(row));
  }
  syncing = 0;
  gtk_stack_set_visible_child_name(GTK_STACK(stack), n ? "designer" : "empty");
  syncdesigner();
}

void sound_select(int instrnum)
{
  GtkListBoxRow *row;
  int i;

  if (instrnum == current) return;
  current = instrnum;
  syncing = 1;
  for (i = 0; (row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(listbox), i)); i++)
    if (GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "instr")) == instrnum)
      gtk_list_box_select_row(GTK_LIST_BOX(listbox), row);
  syncing = 0;
  syncdesigner();
}

static int build(int instrnum, const RECIPE *rc, const char *name, int coalesce)
{
  int ok;

  host_lock();
  ok = recipe_build(instrnum, rc, name);
  host_unlock();
  if (!ok)
  {
    sm_toast("The tables are full: there is no room for this sound");
    return 0;
  }
  undo_checkpoint(coalesce);
  sm_edited();
  return 1;
}

//
// The preset library
//

static int librarytarget;

static void onpresetactivated(GtkListBox *box, GtkListBoxRow *row, gpointer data)
{
  int p = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "preset"));

  char oldname[MAX_INSTRNAMELEN];

  // Each preset tried replaces the last one, and the name follows it
  memcpy(oldname, instr[librarytarget].name, MAX_INSTRNAMELEN);
  if (recipe_get(librarytarget)) memset(instr[librarytarget].name, 0, MAX_INSTRNAMELEN);
  if (!build(librarytarget, &presets[p].recipe, presets[p].name, COALESCE_LIBRARY + librarytarget))
  {
    memcpy(instr[librarytarget].name, oldname, MAX_INSTRNAMELEN);
    return;
  }
  current = librarytarget;
  roll_refreshinstruments();
  roll_setinstrument(current);
  sound_refresh();
  preview(librarytarget);
}

static void openlibrary(int target)
{
  AdwDialog *dialog = adw_dialog_new();
  GtkWidget *view = adw_toolbar_view_new();
  GtkWidget *header = adw_header_bar_new();
  GtkWidget *scroll = gtk_scrolled_window_new();
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  const char *category = NULL;
  GtkWidget *list = NULL;
  char title[64];
  int p;

  librarytarget = target;
  snprintf(title, sizeof title, "Sounds for Instrument %02X", target);
  adw_dialog_set_title(dialog, title);
  adw_dialog_set_content_width(dialog, 420);
  adw_dialog_set_content_height(dialog, 560);
  adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(view), header);
  gtk_widget_set_margin_start(box, 12);
  gtk_widget_set_margin_end(box, 12);
  gtk_widget_set_margin_top(box, 12);
  gtk_widget_set_margin_bottom(box, 12);
  {
    GtkWidget *hint = gtk_label_new("Click a sound to try it: it replaces the instrument and plays a note. "
      "Keep the one you like, and change it to taste in the designer.");
    gtk_label_set_wrap(GTK_LABEL(hint), TRUE);
    gtk_label_set_xalign(GTK_LABEL(hint), 0);
    gtk_widget_add_css_class(hint, "dim-label");
    gtk_box_append(GTK_BOX(box), hint);
  }
  for (p = 0; p < numpresets; p++)
  {
    GtkWidget *row;

    if ((!category) || (strcmp(category, presets[p].category)))
    {
      GtkWidget *label = gtk_label_new(presets[p].category);
      category = presets[p].category;
      gtk_label_set_xalign(GTK_LABEL(label), 0);
      gtk_widget_add_css_class(label, "heading");
      gtk_box_append(GTK_BOX(box), label);
      list = gtk_list_box_new();
      gtk_widget_add_css_class(list, "boxed-list");
      gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
      g_signal_connect(list, "row-activated", G_CALLBACK(onpresetactivated), NULL);
      gtk_box_append(GTK_BOX(box), list);
    }
    row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), presets[p].name);
    adw_action_row_set_subtitle(ADW_ACTION_ROW(row), presets[p].description);
    gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), TRUE);
    adw_action_row_add_suffix(ADW_ACTION_ROW(row), gtk_image_new_from_icon_name("media-playback-start-symbolic"));
    g_object_set_data(G_OBJECT(row), "preset", GINT_TO_POINTER(p));
    gtk_list_box_append(GTK_LIST_BOX(list), row);
  }
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), box);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(view), scroll);
  adw_dialog_set_child(dialog, view);
  adw_dialog_present(dialog, GTK_WIDGET(sm_window));
}

static void onadd(GtkButton *button, gpointer data)
{
  int c;

  for (c = 1; c < MAX_INSTR; c++)
    if (isemptyinstr(c)) break;
  if (c >= MAX_INSTR)
  {
    sm_toast("The song already has 63 instruments");
    return;
  }
  openlibrary(c);
}

static void onchange(GtkButton *button, gpointer data)
{
  openlibrary(current);
}

//
// The designer
//

static double rowvalue(GtkWidget *row)
{
  GtkWidget *scale = g_object_get_data(G_OBJECT(row), "scale");
  return gtk_range_get_value(GTK_RANGE(scale));
}

static void setrowvalue(GtkWidget *row, double v)
{
  GtkWidget *scale = g_object_get_data(G_OBJECT(row), "scale");
  gtk_range_set_value(GTK_RANGE(scale), v);
}

// The recipe the controls describe
static void readcontrols(RECIPE *rc)
{
  static const unsigned char kinds[] = {RK_TONE, RK_KICK, RK_SNARE, RK_HIHAT, RK_TOM};
  int c;

  memset(rc, 0, sizeof *rc);
  rc->kind = kinds[adw_combo_row_get_selected(ADW_COMBO_ROW(kindrow))];
  for (c = 0; c < 4; c++)
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wavebuttons[c]))) rc->wave |= 0x10 << c;
  rc->attack = rowvalue(adsr[0]);
  rc->decay = rowvalue(adsr[1]);
  rc->sustain = rowvalue(adsr[2]);
  rc->release = rowvalue(adsr[3]);
  rc->click = adw_switch_row_get_active(ADW_SWITCH_ROW(clickrow));
  rc->hardrestart = adw_switch_row_get_active(ADW_SWITCH_ROW(hardrow));
  rc->pitch = rowvalue(pitchrow);
  rc->chord = adw_combo_row_get_selected(ADW_COMBO_ROW(chordrow));
  rc->chordspeed = rowvalue(chordspeedrow);
  rc->width = rowvalue(widthrow);
  rc->pwm = rowvalue(pwmrow);
  rc->pwmspeed = rowvalue(pwmspeedrow);
  rc->filter = adw_combo_row_get_selected(ADW_COMBO_ROW(filterrow));
  rc->cutoff = rowvalue(cutoffrow);
  rc->resonance = rowvalue(resrow);
  rc->sweep = rowvalue(sweeprow);
  rc->sweeptime = rowvalue(sweeptimerow);
  rc->vibrato = adw_combo_row_get_selected(ADW_COMBO_ROW(vibrow));
  rc->vibdelay = rowvalue(vibdelayrow);
}

static void showtables(void)
{
  static const char *names[] = {"Wavetable", "Pulsetable", "Filtertable", "Speedtable"};
  GString *s = g_string_new(NULL);
  int t;

  for (t = 0; t < MAX_TABLES; t++)
  {
    int pos = instr[current].ptr[t] - 1, n;
    if (pos < 0) continue;
    g_string_append_printf(s, "%s%s from row %02X\n", s->len ? "\n" : "", names[t], pos + 1);
    for (n = 0; (n < 32) && (pos < MAX_TABLELEN); n++, pos++)
    {
      char desc[96];
      table_describe(t, pos, desc, sizeof desc, 1);
      g_string_append_printf(s, "  %02X  %02X %02X  %s\n", pos + 1, ltable[t][pos], rtable[t][pos], desc);
      if ((t == STBL) || (ltable[t][pos] == 0xff)) break;
    }
  }
  g_string_append_printf(s, "%sAttack/Decay %02X  Sustain/Release %02X\nVibrato delay %02X  HR/Gate timer %02X  "
    "1st frame wave %02X", s->len ? "\n" : "", instr[current].ad, instr[current].sr, instr[current].vibdelay,
    instr[current].gatetimer, instr[current].firstwave);
  gtk_label_set_text(GTK_LABEL(hoodlabel), s->str);
  g_string_free(s, TRUE);
}

// Show the controls that matter for this kind of sound
static void updatesensitivity(void)
{
  int tone = adw_combo_row_get_selected(ADW_COMBO_ROW(kindrow)) == 0;
  int pulse = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wavebuttons[2]));
  int filter = adw_combo_row_get_selected(ADW_COMBO_ROW(filterrow)) != RF_OFF;

  gtk_widget_set_visible(waverow, tone);
  gtk_widget_set_visible(clickrow, tone);
  gtk_widget_set_visible(pitchrow, !tone);
  gtk_widget_set_visible(recipegroups[2], tone);
  gtk_widget_set_visible(recipegroups[3], tone && pulse);
  gtk_widget_set_visible(recipegroups[4], tone);
  gtk_widget_set_visible(recipegroups[5], tone);
  gtk_widget_set_sensitive(chordspeedrow, adw_combo_row_get_selected(ADW_COMBO_ROW(chordrow)) != RC_OFF);
  gtk_widget_set_sensitive(pwmspeedrow, rowvalue(pwmrow) > 0);
  gtk_widget_set_sensitive(cutoffrow, filter);
  gtk_widget_set_sensitive(resrow, filter);
  gtk_widget_set_sensitive(sweeprow, filter);
  gtk_widget_set_sensitive(sweeptimerow, filter);
  gtk_widget_set_sensitive(vibdelayrow, adw_combo_row_get_selected(ADW_COMBO_ROW(vibrow)) != RV_OFF);
}

static void syncdesigner(void)
{
  const RECIPE *rc = recipe_get(current);
  int c;

  syncing = 1;
  // Not while typing in it: setting the same text moves the cursor
  if (strcmp(gtk_editable_get_text(GTK_EDITABLE(nameentry)), sm_toutf8(instr[current].name)))
    gtk_editable_set_text(GTK_EDITABLE(nameentry), sm_toutf8(instr[current].name));
  gtk_button_set_label(GTK_BUTTON(changebutton), rc ? "Choose a Different Sound…" : "Replace with a Preset Sound…");
  for (c = 0; c < 6; c++) gtk_widget_set_visible(recipegroups[c], rc != NULL);
  gtk_widget_set_visible(customgroup, !rc);
  gtk_label_set_text(GTK_LABEL(customlabel), isemptyinstr(current) ?
    "This instrument is empty: choose a sound for it." :
    "This instrument was made in GoatTracker, so it has no settings here beyond its envelope. "
    "Play it and use it as it is, or replace it with a preset sound.");
  if (rc)
  {
    static const unsigned char kindindex[] = {0, 1, 2, 3, 4};
    adw_combo_row_set_selected(ADW_COMBO_ROW(kindrow), rc->kind < RK_KINDS ? kindindex[rc->kind] : 0);
    for (c = 0; c < 4; c++) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wavebuttons[c]), (rc->wave >> (4 + c)) & 1);
    setrowvalue(adsr[0], rc->attack);
    setrowvalue(adsr[1], rc->decay);
    setrowvalue(adsr[2], rc->sustain);
    setrowvalue(adsr[3], rc->release);
    adw_switch_row_set_active(ADW_SWITCH_ROW(clickrow), rc->click);
    adw_switch_row_set_active(ADW_SWITCH_ROW(hardrow), rc->hardrestart);
    setrowvalue(pitchrow, rc->pitch);
    adw_combo_row_set_selected(ADW_COMBO_ROW(chordrow), rc->chord < RC_CHORDS ? rc->chord : 0);
    setrowvalue(chordspeedrow, rc->chordspeed);
    setrowvalue(widthrow, rc->width);
    setrowvalue(pwmrow, rc->pwm);
    setrowvalue(pwmspeedrow, rc->pwmspeed);
    adw_combo_row_set_selected(ADW_COMBO_ROW(filterrow), rc->filter);
    setrowvalue(cutoffrow, rc->cutoff);
    setrowvalue(resrow, rc->resonance);
    setrowvalue(sweeprow, rc->sweep);
    setrowvalue(sweeptimerow, rc->sweeptime);
    adw_combo_row_set_selected(ADW_COMBO_ROW(vibrow), rc->vibrato);
    setrowvalue(vibdelayrow, rc->vibdelay);
    updatesensitivity();
  }
  else
  {
    setrowvalue(adsr[0], instr[current].ad >> 4);
    setrowvalue(adsr[1], instr[current].ad & 15);
    setrowvalue(adsr[2], instr[current].sr >> 4);
    setrowvalue(adsr[3], instr[current].sr & 15);
  }
  // The envelope is shared by both kinds of instrument
  gtk_widget_set_visible(recipegroups[1], !isemptyinstr(current));
  gtk_widget_queue_draw(envarea);
  showtables();
  syncing = 0;
}

// A control changed: rebuild the instrument from the recipe
static void ondesignchanged(void)
{
  RECIPE rc;

  if (syncing) return;
  if (!recipe_get(current))
  {
    // Instruments without a recipe: only the envelope is edited
    host_lock();
    instr[current].ad = ((int)rowvalue(adsr[0]) << 4) | (int)rowvalue(adsr[1]);
    instr[current].sr = ((int)rowvalue(adsr[2]) << 4) | (int)rowvalue(adsr[3]);
    host_unlock();
    undo_checkpoint(COALESCE_DESIGN + current);
    sm_edited();
    gtk_widget_queue_draw(envarea);
    showtables();
    return;
  }
  readcontrols(&rc);
  updatesensitivity();
  gtk_widget_queue_draw(envarea);
  if (build(current, &rc, NULL, COALESCE_DESIGN + current)) showtables();
}

static void onwavetoggled(GtkToggleButton *button, gpointer data)
{
  int c = GPOINTER_TO_INT(data), i;

  if (syncing) return;
  syncing = 1;
  // Noise doesn't combine with the other waveforms; and there is always one
  if (gtk_toggle_button_get_active(button))
  {
    for (i = 0; i < 4; i++)
      if ((i != c) && ((c == 3) || (i == 3))) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wavebuttons[i]), FALSE);
  }
  else
  {
    for (i = 0; i < 4; i++)
      if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wavebuttons[i]))) break;
    if (i == 4) gtk_toggle_button_set_active(button, TRUE);
  }
  syncing = 0;
  ondesignchanged();
}

static void onnamechanged(GtkEditable *editable, gpointer data)
{
  char latin1[MAX_INSTRNAMELEN];
  char *conv;

  if (syncing) return;
  conv = g_convert_with_fallback(gtk_editable_get_text(editable), -1, "ISO-8859-1", "UTF-8", "?", NULL, NULL, NULL);
  memset(latin1, 0, sizeof latin1);
  if (conv) strncpy(latin1, conv, MAX_INSTRNAMELEN - 1);
  g_free(conv);
  memcpy(instr[current].name, latin1, MAX_INSTRNAMELEN);
  undo_checkpoint(COALESCE_DESIGN + 0x80 + current);
  sm_edited();
  roll_refreshinstruments();
  sound_refresh();
}

static void ontry(GtkButton *button, gpointer data)
{
  preview(current);
}

static void drawenvelope(GtkDrawingArea *area, cairo_t *cr, int w, int h, gpointer data)
{
  // Rough SID envelope timings, in seconds
  static const double rate[16] = {0.002, 0.008, 0.016, 0.024, 0.038, 0.056, 0.068, 0.08, 0.1, 0.25, 0.5, 0.8,
    1.0, 3.0, 5.0, 8.0};
  GdkRGBA fg;
  int a = rowvalue(adsr[0]), d = rowvalue(adsr[1]), s = rowvalue(adsr[2]), r = rowvalue(adsr[3]);
  double ta = rate[a], td = rate[d] * 3, th = 0.4, tr = rate[r] * 3, total = ta + td + th + tr;
  double x = 4, y0 = h - 6, top = 6, sx = (w - 8) / total, sl = top + (y0 - top) * (1 - s / 15.0);

  gtk_widget_get_color(GTK_WIDGET(area), &fg);
  cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.08);
  cairo_rectangle(cr, 0, 0, w, h);
  cairo_fill(cr);
  cairo_set_source_rgba(cr, 0.35, 0.65, 1.0, 1);
  cairo_set_line_width(cr, 2);
  cairo_move_to(cr, x, y0);
  x += ta * sx;
  cairo_line_to(cr, x, top);
  x += td * sx;
  cairo_line_to(cr, x, sl);
  x += th * sx;
  cairo_line_to(cr, x, sl);
  x += tr * sx;
  cairo_line_to(cr, x, y0);
  cairo_stroke(cr);
}

static GtkWidget *scalerow(const char *title, const char *subtitle, double lo, double hi, double step)
{
  GtkWidget *row = adw_action_row_new();
  GtkWidget *scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, lo, hi, step);

  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
  if (subtitle) adw_action_row_set_subtitle(ADW_ACTION_ROW(row), subtitle);
  gtk_scale_set_draw_value(GTK_SCALE(scale), TRUE);
  gtk_scale_set_value_pos(GTK_SCALE(scale), GTK_POS_LEFT);
  gtk_scale_set_digits(GTK_SCALE(scale), 0);
  gtk_widget_set_size_request(scale, 140, -1);
  gtk_widget_set_valign(scale, GTK_ALIGN_CENTER);
  g_signal_connect_swapped(scale, "value-changed", G_CALLBACK(ondesignchanged), NULL);
  adw_action_row_add_suffix(ADW_ACTION_ROW(row), scale);
  g_object_set_data(G_OBJECT(row), "scale", scale);
  return row;
}

static GtkWidget *comborow(const char *title, const char *subtitle, const char *const *items)
{
  GtkWidget *row = adw_combo_row_new();

  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
  if (subtitle) adw_action_row_set_subtitle(ADW_ACTION_ROW(row), subtitle);
  adw_combo_row_set_model(ADW_COMBO_ROW(row), G_LIST_MODEL(gtk_string_list_new(items)));
  g_signal_connect_swapped(row, "notify::selected", G_CALLBACK(ondesignchanged), NULL);
  return row;
}

static GtkWidget *switchrow(const char *title, const char *subtitle)
{
  GtkWidget *row = adw_switch_row_new();

  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
  if (subtitle) adw_action_row_set_subtitle(ADW_ACTION_ROW(row), subtitle);
  g_signal_connect_swapped(row, "notify::active", G_CALLBACK(ondesignchanged), NULL);
  return row;
}

static GtkWidget *group(GtkWidget *page, const char *title, const char *description)
{
  GtkWidget *g = adw_preferences_group_new();

  if (title) adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(g), title);
  if (description) adw_preferences_group_set_description(ADW_PREFERENCES_GROUP(g), description);
  adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(g));
  return g;
}

GtkWidget *sound_new(void)
{
  static const char *kinds[] = {"Tone", "Kick drum", "Snare drum", "Hi-hat", "Tom", NULL};
  static const char *chordnames[] = {"Off", "Major", "Minor", "Octave", "Power (fifth)", "Suspended 4th",
    "Major 7th", "Minor 7th", NULL};
  static const char *filters[] = {"Off", "Low-pass (darker)", "Band-pass (nasal)", "High-pass (thinner)", NULL};
  static const char *vibratos[] = {"Off", "Gentle", "Normal", "Wide", NULL};
  static const char *wavenames[] = {"Triangle", "Saw", "Pulse", "Noise"};
  GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
  GtkWidget *top = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  GtkWidget *scroll, *g, *row, *box, *button, *expander;
  int c;

  // Instrument list
  gtk_widget_set_margin_start(bar, 12);
  gtk_widget_set_margin_end(bar, 12);
  gtk_widget_set_margin_top(bar, 8);
  gtk_widget_set_margin_bottom(bar, 8);
  {
    GtkWidget *label = gtk_label_new("Instruments");
    gtk_widget_add_css_class(label, "heading");
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_box_append(GTK_BOX(bar), label);
  }
  button = gtk_button_new_from_icon_name("list-add-symbolic");
  gtk_widget_set_tooltip_text(button, "Add an instrument from the sound library");
  g_signal_connect(button, "clicked", G_CALLBACK(onadd), NULL);
  gtk_box_append(GTK_BOX(bar), button);
  gtk_box_append(GTK_BOX(top), bar);
  listbox = gtk_list_box_new();
  gtk_widget_add_css_class(listbox, "navigation-sidebar");
  g_signal_connect(listbox, "row-selected", G_CALLBACK(onrowselected), NULL);
  scroll = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), listbox);
  gtk_widget_set_vexpand(scroll, TRUE);
  gtk_box_append(GTK_BOX(top), scroll);
  gtk_paned_set_start_child(GTK_PANED(paned), top);
  gtk_paned_set_resize_start_child(GTK_PANED(paned), FALSE);
  gtk_widget_set_size_request(top, -1, 140);

  // Designer
  designer = adw_preferences_page_new();
  g = group(designer, NULL, NULL);
  nameentry = adw_entry_row_new();
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(nameentry), "Name");
  g_signal_connect(nameentry, "changed", G_CALLBACK(onnamechanged), NULL);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(g), nameentry);
  box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_set_margin_top(box, 8);
  button = gtk_button_new_with_label("Play");
  gtk_widget_set_tooltip_text(button, "Play a note with this instrument");
  g_signal_connect(button, "clicked", G_CALLBACK(ontry), NULL);
  gtk_box_append(GTK_BOX(box), button);
  changebutton = gtk_button_new_with_label("Choose a Different Sound…");
  gtk_widget_set_hexpand(changebutton, TRUE);
  g_signal_connect(changebutton, "clicked", G_CALLBACK(onchange), NULL);
  gtk_box_append(GTK_BOX(box), changebutton);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(g), box);

  customgroup = group(designer, NULL, NULL);
  customlabel = gtk_label_new("");
  gtk_label_set_wrap(GTK_LABEL(customlabel), TRUE);
  gtk_label_set_xalign(GTK_LABEL(customlabel), 0);
  gtk_widget_add_css_class(customlabel, "dim-label");
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(customgroup), customlabel);

  recipegroups[0] = g = group(designer, "Sound", NULL);
  kindrow = comborow("Kind", NULL, kinds);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(g), kindrow);
  waverow = adw_action_row_new();
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(waverow), "Waveform");
  box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_add_css_class(box, "linked");
  gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
  for (c = 0; c < 4; c++)
  {
    wavebuttons[c] = gtk_toggle_button_new_with_label(wavenames[c]);
    g_signal_connect(wavebuttons[c], "toggled", G_CALLBACK(onwavetoggled), GINT_TO_POINTER(c));
    gtk_box_append(GTK_BOX(box), wavebuttons[c]);
  }
  adw_action_row_add_suffix(ADW_ACTION_ROW(waverow), box);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(g), waverow);
  clickrow = switchrow("Click", "A short noise at the start of each note");
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(g), clickrow);
  pitchrow = scalerow("Pitch", "Semitones up or down", -12, 12, 1);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(g), pitchrow);
  hardrow = switchrow("Crisp start", "Reset the sound before each note (hard restart)");
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(g), hardrow);

  recipegroups[1] = g = group(designer, "Envelope", "How the volume of a note rises and falls");
  envarea = gtk_drawing_area_new();
  gtk_widget_set_size_request(envarea, -1, 64);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(envarea), drawenvelope, NULL, NULL);
  // A preferences row, so the group lists it in order with the sliders
  row = adw_preferences_row_new();
  gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), FALSE);
  gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), envarea);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(g), row);
  adsr[0] = scalerow("Attack", "How quickly it starts", 0, 15, 1);
  adsr[1] = scalerow("Decay", "How quickly it falls to the sustain level", 0, 15, 1);
  adsr[2] = scalerow("Sustain", "How loud it stays while the note is held", 0, 15, 1);
  adsr[3] = scalerow("Release", "How long it fades after the note ends", 0, 15, 1);
  for (c = 0; c < 4; c++) adw_preferences_group_add(ADW_PREFERENCES_GROUP(g), adsr[c]);

  recipegroups[2] = g = group(designer, "Chord", "One voice plays a chord by flicking quickly between its notes");
  chordrow = comborow("Chord", NULL, chordnames);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(g), chordrow);
  chordspeedrow = scalerow("Speed", "Frames per note (1 is fastest)", 1, 8, 1);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(g), chordspeedrow);

  recipegroups[3] = g = group(designer, "Pulse", "The pulse waveform's shape, and how it sweeps");
  widthrow = scalerow("Width", "8 is a square wave; lower and higher are thinner", 1, 15, 1);
  pwmrow = scalerow("Sweep", "How far the width moves (0 = it doesn't)", 0, 15, 1);
  pwmspeedrow = scalerow("Sweep speed", NULL, 1, 15, 1);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(g), widthrow);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(g), pwmrow);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(g), pwmspeedrow);

  recipegroups[4] = g = group(designer, "Filter", "The SID has one filter, shared by all three voices");
  filterrow = comborow("Filter", NULL, filters);
  cutoffrow = scalerow("Cutoff", "Where the filter starts", 0, 255, 1);
  resrow = scalerow("Resonance", "A ringing peak at the cutoff", 0, 15, 1);
  sweeprow = scalerow("Sweep", "How fast the cutoff moves (minus = down)", -16, 16, 1);
  sweeptimerow = scalerow("Sweep length", "Frames (50 a second)", 0, 255, 1);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(g), filterrow);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(g), cutoffrow);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(g), resrow);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(g), sweeprow);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(g), sweeptimerow);

  recipegroups[5] = g = group(designer, "Vibrato", "A wobble in pitch, after the note has started");
  vibrow = comborow("Vibrato", NULL, vibratos);
  vibdelayrow = scalerow("Delay", "Frames before it starts", 0, 60, 1);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(g), vibrow);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(g), vibdelayrow);

  g = group(designer, NULL, NULL);
  expander = adw_expander_row_new();
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(expander), "Under the Hood");
  adw_expander_row_set_subtitle(ADW_EXPANDER_ROW(expander), "What GoatTracker sees: the instrument and its tables");
  hoodlabel = gtk_label_new("");
  gtk_label_set_xalign(GTK_LABEL(hoodlabel), 0);
  gtk_label_set_selectable(GTK_LABEL(hoodlabel), TRUE);
  gtk_widget_add_css_class(hoodlabel, "monospace");
  gtk_widget_set_margin_start(hoodlabel, 12);
  gtk_widget_set_margin_end(hoodlabel, 12);
  gtk_widget_set_margin_top(hoodlabel, 8);
  gtk_widget_set_margin_bottom(hoodlabel, 8);
  adw_expander_row_add_row(ADW_EXPANDER_ROW(expander), hoodlabel);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(g), expander);

  {
    GtkWidget *empty = adw_status_page_new();
    button = gtk_button_new_with_label("Add a Sound");
    gtk_widget_add_css_class(button, "pill");
    gtk_widget_add_css_class(button, "suggested-action");
    gtk_widget_set_halign(button, GTK_ALIGN_CENTER);
    g_signal_connect(button, "clicked", G_CALLBACK(onadd), NULL);
    adw_status_page_set_icon_name(ADW_STATUS_PAGE(empty), "audio-x-generic-symbolic");
    adw_status_page_set_title(ADW_STATUS_PAGE(empty), "No Instruments Yet");
    adw_status_page_set_description(ADW_STATUS_PAGE(empty), "Instruments are the sounds your notes play with. "
      "Start with one from the sound library.");
    adw_status_page_set_child(ADW_STATUS_PAGE(empty), button);
    gtk_widget_add_css_class(empty, "compact");
    stack = gtk_stack_new();
    gtk_stack_add_named(GTK_STACK(stack), empty, "empty");
    gtk_stack_add_named(GTK_STACK(stack), designer, "designer");
  }
  gtk_paned_set_end_child(GTK_PANED(paned), stack);
  gtk_paned_set_position(GTK_PANED(paned), 200);
  (void)row;
  return paned;
}
