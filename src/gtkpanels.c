//
// GOATTRACKER v2 GTK user interface: instrument list and editor, song info
//

#include <string.h>
#include "gtkui.h"

GtkWidget *instrlist = NULL;
GtkWidget *songnameentry = NULL;

enum
{
  F_ATTACK, F_DECAY, F_SUSTAIN, F_RELEASE,
  F_WAVEPTR, F_PULSEPTR, F_FILTERPTR, F_VIBPARAM,
  F_VIBDELAY, F_GATETIMER, F_FIRSTWAVE,
  NUMFIELDS
};

static GtkWidget *instrlabels[MAX_INSTR];
static GtkWidget *instrcounts[MAX_INSTR];
static GtkWidget *fieldspins[NUMFIELDS];
static GtkWidget *adsrscales[4];
static GtkWidget *instrnameentry;
static GtkWidget *instreditor;
static GtkWidget *instrtitle;
static GtkWidget *songentries[3];
static GtkWidget *envelopearea;
static int syncing = 0;
static GtkWidget *instrroot;
static int textscale = 1;
static int forcesync = 0;

// Undo coalescing keys: repeated edits of one field of one instrument (or
// one song text) form a single undo step
#define KEY_INSTRFIELD(field) ((1 << 16) | ((field) << 8) | einum)
#define KEY_INSTRNAME ((2 << 16) | einum)
#define KEY_SONGTEXT(n) ((3 << 16) | (n))

static int getfield(int field)
{
  INSTR *i = &instr[einum];

  switch (field)
  {
    case F_ATTACK: return i->ad >> 4;
    case F_DECAY: return i->ad & 0xf;
    case F_SUSTAIN: return i->sr >> 4;
    case F_RELEASE: return i->sr & 0xf;
    case F_WAVEPTR: return i->ptr[WTBL];
    case F_PULSEPTR: return i->ptr[PTBL];
    case F_FILTERPTR: return i->ptr[FTBL];
    case F_VIBPARAM: return i->ptr[STBL];
    case F_VIBDELAY: return i->vibdelay;
    case F_GATETIMER: return i->gatetimer;
    case F_FIRSTWAVE: return i->firstwave;
  }
  return 0;
}

static void setfield(int field, int v)
{
  INSTR *i = &instr[einum];

  switch (field)
  {
    case F_ATTACK: i->ad = (i->ad & 0x0f) | (v << 4); break;
    case F_DECAY: i->ad = (i->ad & 0xf0) | v; break;
    case F_SUSTAIN: i->sr = (i->sr & 0x0f) | (v << 4); break;
    case F_RELEASE: i->sr = (i->sr & 0xf0) | v; break;
    case F_WAVEPTR: i->ptr[WTBL] = v; break;
    case F_PULSEPTR: i->ptr[PTBL] = v; break;
    case F_FILTERPTR: i->ptr[FTBL] = v; break;
    case F_VIBPARAM: i->ptr[STBL] = v; break;
    case F_VIBDELAY: i->vibdelay = v; break;
    case F_GATETIMER: i->gatetimer = v; break;
    case F_FIRSTWAVE: i->firstwave = v; break;
  }
  // Same validation as the engine's instrument editor
  if (!(i->gatetimer & 0x3f)) i->gatetimer |= 1;
}

//
// Hexadecimal spin buttons
//

static gboolean onhexoutput(GtkSpinButton *spin, gpointer data)
{
  char buf[8];

  snprintf(buf, sizeof buf, "%0*X", GPOINTER_TO_INT(data), (int)gtk_spin_button_get_value(spin));
  if (strcmp(buf, gtk_editable_get_text(GTK_EDITABLE(spin))))
    gtk_editable_set_text(GTK_EDITABLE(spin), buf);
  return TRUE;
}

static int onhexinput(GtkSpinButton *spin, double *value, gpointer data)
{
  const char *text = gtk_editable_get_text(GTK_EDITABLE(spin));
  char *end;
  long v;

  if (*text == '$') text++;
  v = strtol(text, &end, 16);
  if ((end == text) || (*end)) return GTK_INPUT_ERROR;
  *value = v;
  return TRUE;
}

// The mouse wheel scrolls the panel a field is in instead of changing the
// field's value, so scrolling the instrument editor can't edit it by accident
static gboolean onfieldscroll(GtkEventControllerScroll *controller, double dx, double dy, gpointer data)
{
  GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller));
  GtkWidget *scroll = gtk_widget_get_ancestor(widget, GTK_TYPE_SCROLLED_WINDOW);

  if (scroll)
  {
    GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scroll));
    gtk_adjustment_set_value(adj, gtk_adjustment_get_value(adj) + dy * 40);
  }
  return TRUE;
}

static void wheelscrollspanel(GtkWidget *widget)
{
  GtkEventController *controller = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);

  gtk_event_controller_set_propagation_phase(controller, GTK_PHASE_CAPTURE);
  g_signal_connect(controller, "scroll", G_CALLBACK(onfieldscroll), NULL);
  gtk_widget_add_controller(widget, controller);
}

GtkWidget *hexspin_new(int max, int digits)
{
  GtkWidget *spin = gtk_spin_button_new_with_range(0, max, 1);

  gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(spin), FALSE);
  gtk_editable_set_width_chars(GTK_EDITABLE(spin), digits + 1);
  gtk_widget_add_css_class(spin, "monospace");
  g_signal_connect(spin, "output", G_CALLBACK(onhexoutput), GINT_TO_POINTER(digits));
  g_signal_connect(spin, "input", G_CALLBACK(onhexinput), NULL);
  return spin;
}

//
// Waveform editor: the SID waveform register as toggle buttons
//

typedef struct
{
  unsigned char *value;
  int wavetable;
  int coalesce;
  int updating;
  GtkWidget *toggles[8];
  GtkWidget *label;
} WAVEEDIT;

static const struct
{
  const char *name;
  unsigned char bit;
  const char *tooltip;
} wavebits[8] = {
  {"Noise", 0x80, NULL},
  {"Pulse", 0x40, NULL},
  {"Saw", 0x20, NULL},
  {"Triangle", 0x10, NULL},
  {"Test", 0x08, "Holds the oscillator at zero (used for hard restart)"},
  {"Ring", 0x04, "Ring modulation by the previous channel (with triangle)"},
  {"Sync", 0x02, "Hard sync to the previous channel"},
  {"Gate", 0x01, "Starts the envelope's attack; clearing it starts the release"}
};

static void wavesync(WAVEEDIT *w)
{
  unsigned char v = *w->value;
  char buf[64];
  int c;

  // In the wavetable E0-EF are waveforms with all waves off
  if ((w->wavetable) && (v >= WAVESILENT)) v &= 0x0f;
  w->updating = 1;
  for (c = 0; c < 8; c++)
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->toggles[c]), (v & wavebits[c].bit) != 0);
  w->updating = 0;
  if ((!w->wavetable) && (!*w->value)) snprintf(buf, sizeof buf, "$00: no change on the first frame");
  else if ((w->wavetable) && (!*w->value)) snprintf(buf, sizeof buf, "$00: waveform unchanged");
  else if ((w->wavetable) && (*w->value >= WAVESILENT)) snprintf(buf, sizeof buf, "$%02X: no waveform (silent)", *w->value);
  else snprintf(buf, sizeof buf, "$%02X", *w->value);
  gtk_label_set_text(GTK_LABEL(w->label), buf);
}

static void onwavetoggled(GtkToggleButton *button, WAVEEDIT *w)
{
  unsigned char v = 0;
  int c;

  if (w->updating) return;
  for (c = 0; c < 8; c++)
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->toggles[c]))) v |= wavebits[c].bit;

  if (w->wavetable)
  {
    // E0-FF mean silent waveforms and commands in the wavetable, so no
    // waves is written as E0-EF and noise+pulse+saw isn't possible
    if (!(v & 0xf0)) v |= WAVESILENT;
    else if ((v & 0xf0) >= WAVESILENT)
    {
      ui_toast("Noise, pulse and saw can't be combined in the wavetable");
      wavesync(w);
      return;
    }
  }
  else if (v >= 0xfe)
  {
    ui_toast("$FE and $FF are reserved for gate off and on");
    wavesync(w);
    return;
  }

  *w->value = v;
  undo_checkpoint(w->coalesce);
  ui_edited();
  ui_refresh();
  wavesync(w);
}

static gboolean unparentpopover(gpointer data)
{
  gtk_widget_unparent(GTK_WIDGET(data));
  return G_SOURCE_REMOVE;
}

static void onwaveclosed(GtkPopover *popover, gpointer data)
{
  g_idle_add(unparentpopover, popover);
}

void ui_waveformeditor(GtkWidget *parent, const GdkRectangle *where, unsigned char *value, int wavetable, int coalesce)
{
  GtkWidget *popover = gtk_popover_new();
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  GtkWidget *row = NULL;
  WAVEEDIT *w = g_new0(WAVEEDIT, 1);
  int c;

  w->value = value;
  w->wavetable = wavetable;
  w->coalesce = coalesce;
  for (c = 0; c < 8; c++)
  {
    if (!(c & 3))
    {
      row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
      gtk_widget_add_css_class(row, "linked");
      gtk_box_set_homogeneous(GTK_BOX(row), TRUE);
      gtk_box_append(GTK_BOX(box), row);
    }
    w->toggles[c] = gtk_toggle_button_new_with_label(wavebits[c].name);
    if (wavebits[c].tooltip) gtk_widget_set_tooltip_text(w->toggles[c], wavebits[c].tooltip);
    g_signal_connect(w->toggles[c], "toggled", G_CALLBACK(onwavetoggled), w);
    gtk_box_append(GTK_BOX(row), w->toggles[c]);
  }
  w->label = gtk_label_new(NULL);
  gtk_widget_add_css_class(w->label, "dim-label");
  gtk_widget_add_css_class(w->label, "monospace");
  gtk_box_append(GTK_BOX(box), w->label);
  wavesync(w);

  gtk_popover_set_child(GTK_POPOVER(popover), box);
  g_object_set_data_full(G_OBJECT(popover), "waveedit", w, g_free);
  g_signal_connect(popover, "closed", G_CALLBACK(onwaveclosed), NULL);
  gtk_widget_set_parent(popover, parent);
  if (where) gtk_popover_set_pointing_to(GTK_POPOVER(popover), where);
  gtk_popover_popup(GTK_POPOVER(popover));
}

//
// Instrument editor
//

static void onfieldchanged(GtkSpinButton *spin, gpointer data)
{
  if ((syncing) || (!einum)) return;
  setfield(GPOINTER_TO_INT(data), (int)gtk_spin_button_get_value(spin));
  undo_checkpoint(KEY_INSTRFIELD(GPOINTER_TO_INT(data)));
  ui_edited();
  gtk_widget_queue_draw(envelopearea);
  grid_redraw();
}

static void onadsrchanged(GtkRange *range, gpointer data)
{
  if ((syncing) || (!einum)) return;
  setfield(GPOINTER_TO_INT(data), (int)gtk_range_get_value(range));
  undo_checkpoint(KEY_INSTRFIELD(GPOINTER_TO_INT(data)));
  ui_edited();
  gtk_widget_queue_draw(envelopearea);
}

static char *formathexnybble(GtkScale *scale, double value, gpointer data)
{
  return g_strdup_printf("%X", (int)value);
}

static void oninstrnamechanged(GtkEditable *editable, gpointer data)
{
  if ((syncing) || (!einum)) return;
  ui_fromutf8(instr[einum].name, gtk_editable_get_text(editable), MAX_INSTRNAMELEN);
  undo_checkpoint(KEY_INSTRNAME);
  ui_edited();
  panels_sync();
}

static void ongototable(GtkButton *button, gpointer data)
{
  int t = GPOINTER_TO_INT(data);
  int pos;

  if (!einum) return;
  // As ENTER on a table pointer in the classic editor: go to the instrument's
  // table data, or to the first free row when it has none yet
  if (instr[einum].ptr[t]) pos = instr[einum].ptr[t] - 1;
  else
  {
    pos = gettablelen(t);
    if (pos >= MAX_TABLELEN - 1) pos = MAX_TABLELEN - 1;
  }
  ui_pushplace();
  gototable(t, pos);
  ui_focuseditmode();
  ui_refresh();
}

static void ontestpressed(GtkGestureClick *gesture, int n, double x, double y, gpointer data)
{
  playtestnote(FIRSTNOTE + epoctave * 12, einum, epchn);
}

static void ontestreleased(GtkGestureClick *gesture, int n, double x, double y, gpointer data)
{
  releasenote(epchn);
}

static void oninstrcommand(GtkButton *button, gpointer data)
{
  // Run the classic Shift+key instrument command on the selected instrument
  editmode = EDIT_INSTRUMENT;
  eipos = 0;
  ui_runkey(GPOINTER_TO_INT(data), 0, 1);
}

static void drawenvelope(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data)
{
  // SID envelope: attack, decay to sustain level, hold, release. Widths
  // follow the rate nybbles loosely so the shape reads at a glance.
  double a = (instr[einum].ad >> 4) + 1;
  double d = (instr[einum].ad & 0xf) + 1;
  double s = (instr[einum].sr >> 4) / 15.0;
  double r = (instr[einum].sr & 0xf) + 1;
  double hold = 8;
  double total = a + d + hold + r;
  double sx = (width - 4) / total;
  double top = 4, bottom = height - 4;
  double x = 2;
  GdkRGBA accent;

  gtk_widget_get_color(GTK_WIDGET(area), &accent);
  cairo_set_line_width(cr, 2);
  cairo_set_source_rgba(cr, accent.red, accent.green, accent.blue, einum ? 0.9 : 0.3);
  cairo_move_to(cr, x, bottom);
  x += a * sx;
  cairo_line_to(cr, x, top);
  x += d * sx;
  cairo_line_to(cr, x, bottom - s * (bottom - top));
  x += hold * sx;
  cairo_line_to(cr, x, bottom - s * (bottom - top));
  x += r * sx;
  cairo_line_to(cr, x, bottom);
  cairo_stroke(cr);
}

static void addrow(GtkWidget *grid, int row, const char *label, const char *tooltip, GtkWidget *widget, GtkWidget *extra)
{
  GtkWidget *l = gtk_label_new(label);

  gtk_label_set_xalign(GTK_LABEL(l), 0);
  gtk_widget_set_hexpand(l, TRUE);
  if (tooltip)
  {
    gtk_widget_set_tooltip_text(l, tooltip);
    gtk_widget_set_tooltip_text(widget, tooltip);
  }
  gtk_grid_attach(GTK_GRID(grid), l, 0, row, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), widget, 1, row, 1, 1);
  if (extra) gtk_grid_attach(GTK_GRID(grid), extra, 2, row, 1, 1);
}

static GtkWidget *fieldspin(int field, int max, int digits)
{
  GtkWidget *spin = hexspin_new(max, digits);

  fieldspins[field] = spin;
  wheelscrollspanel(spin);
  g_signal_connect(spin, "value-changed", G_CALLBACK(onfieldchanged), GINT_TO_POINTER(field));
  return spin;
}

static GtkWidget *tablebutton(int table)
{
  GtkWidget *button = gtk_button_new_from_icon_name("go-next-symbolic");

  gtk_widget_add_css_class(button, "flat");
  gtk_widget_set_tooltip_text(button, "Go to the table data");
  g_signal_connect(button, "clicked", G_CALLBACK(ongototable), GINT_TO_POINTER(table));
  return button;
}

static void onfirstwave(GtkButton *button, gpointer data)
{
  if (!einum) return;
  ui_waveformeditor(GTK_WIDGET(button), NULL, &instr[einum].firstwave, 0, KEY_INSTRFIELD(F_FIRSTWAVE));
}

static GtkWidget *iconbutton(const char *icon, const char *tooltip, unsigned rawkey)
{
  GtkWidget *button = gtk_button_new_from_icon_name(icon);

  gtk_widget_add_css_class(button, "flat");
  gtk_widget_set_tooltip_text(button, tooltip);
  gtk_widget_set_focusable(button, FALSE);
  g_signal_connect(button, "clicked", G_CALLBACK(oninstrcommand), GINT_TO_POINTER(rawkey));
  return button;
}

static void oninstrselected(GtkListBox *box, GtkListBoxRow *row, gpointer data)
{
  if ((syncing) || (!row)) return;
  einum = gtk_list_box_row_get_index(row);
  showinstrtable();
  ui_refresh();
}

static GtkWidget *heading(const char *text)
{
  GtkWidget *label = gtk_label_new(text);

  gtk_label_set_xalign(GTK_LABEL(label), 0);
  gtk_widget_add_css_class(label, "heading");
  return label;
}

// The instrument column: the instrument list above the editor for the
// selected instrument, with a movable divider between them
// The instrument column follows the grids' text size (View → Larger Text):
// a CSS class sets its font size, which every widget inside inherits, by
// the same factor the grids use (12/10, 14/10, 16/10)
void panels_setscale(int scale)
{
  static GtkCssProvider *provider = NULL;
  static const char *classes[] = {NULL, NULL, "gt-textsize-2", "gt-textsize-3", "gt-textsize-4"};
  int c;

  textscale = CLAMP(scale, 1, 4);
  if (!instrroot) return;
  if (!provider)
  {
    // libadwaita gives headings and captions fixed sizes, so restate
    // them relative to the scaled text
    static const char *css =
      ".gt-textsize-2 { font-size: 120%; } .gt-textsize-3 { font-size: 140%; } "
      ".gt-textsize-4 { font-size: 160%; } "
      ".gt-textsize-2 .heading, .gt-textsize-3 .heading, .gt-textsize-4 .heading { font-size: 100%; } "
      ".gt-textsize-2 .caption, .gt-textsize-3 .caption, .gt-textsize-4 .caption { font-size: 82%; }";

    provider = gtk_css_provider_new();
#if GTK_CHECK_VERSION(4, 12, 0)
    gtk_css_provider_load_from_string(provider, css);
#else
    gtk_css_provider_load_from_data(provider, css, -1);
#endif
    gtk_style_context_add_provider_for_display(gtk_widget_get_display(instrroot),
      GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  }
  for (c = 2; c <= 4; c++) gtk_widget_remove_css_class(instrroot, classes[c]);
  if (textscale > 1) gtk_widget_add_css_class(instrroot, classes[textscale]);
  gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(envelopearea), 36 + 8 * (textscale - 1));
}

GtkWidget *panel_instruments_new(void)
{
  static const char *adsrnames[] = {"Attack", "Decay", "Sustain", "Release"};
  GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  GtkWidget *scroll = gtk_scrolled_window_new();
  GtkWidget *editorscroll = gtk_scrolled_window_new();
  GtkWidget *grid = gtk_grid_new();
  GtkWidget *test = gtk_button_new_with_label("Test Note");
  GtkWidget *wavebutton;
  GtkGesture *gesture;
  int c, row = 0;

  gtk_widget_set_margin_start(box, 8);
  gtk_widget_set_margin_end(box, 8);
  gtk_widget_set_margin_top(box, 6);

  // Instrument list with the classic copy/paste commands
  gtk_box_append(GTK_BOX(header), heading("Instruments"));
  gtk_widget_set_hexpand(gtk_widget_get_first_child(header), TRUE);
  gtk_box_append(GTK_BOX(header), iconbutton("edit-cut-symbolic", "Cut instrument (Shift+X)", KEY_X));
  gtk_box_append(GTK_BOX(header), iconbutton("edit-copy-symbolic", "Copy instrument (Shift+C)", KEY_C));
  gtk_box_append(GTK_BOX(header), iconbutton("edit-paste-symbolic", "Paste instrument (Shift+V)", KEY_V));
  gtk_box_append(GTK_BOX(header), iconbutton("edit-find-replace-symbolic",
    "Paste and point the cut instrument's notes here (Shift+S)", KEY_S));
  gtk_box_append(GTK_BOX(header), iconbutton("edit-delete-symbolic", "Delete instrument and its table data (Shift+Del)", KEY_DEL));
  gtk_box_append(GTK_BOX(box), header);

  instrlist = gtk_list_box_new();
  gtk_list_box_set_selection_mode(GTK_LIST_BOX(instrlist), GTK_SELECTION_BROWSE);
  gtk_widget_add_css_class(instrlist, "navigation-sidebar");
  for (c = 0; c < MAX_INSTR; c++)
  {
    GtkWidget *rowbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

    instrlabels[c] = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(instrlabels[c]), 0);
    gtk_widget_set_hexpand(instrlabels[c], TRUE);
    gtk_widget_add_css_class(instrlabels[c], "monospace");
    // How many patterns use the instrument
    instrcounts[c] = gtk_label_new("");
    gtk_widget_add_css_class(instrcounts[c], "dim-label");
    gtk_widget_add_css_class(instrcounts[c], "caption");
    gtk_box_append(GTK_BOX(rowbox), instrlabels[c]);
    gtk_box_append(GTK_BOX(rowbox), instrcounts[c]);
    gtk_list_box_append(GTK_LIST_BOX(instrlist), rowbox);
  }
  g_signal_connect(instrlist, "row-selected", G_CALLBACK(oninstrselected), NULL);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), instrlist);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_vexpand(scroll, TRUE);
  gtk_widget_set_size_request(scroll, -1, 120);
  gtk_box_append(GTK_BOX(box), scroll);
  gtk_paned_set_start_child(GTK_PANED(paned), box);
  gtk_paned_set_shrink_start_child(GTK_PANED(paned), FALSE);

  // Parameters of the selected instrument
  instreditor = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  gtk_widget_set_margin_start(instreditor, 8);
  gtk_widget_set_margin_end(instreditor, 8);
  gtk_widget_set_margin_top(instreditor, 6);
  gtk_widget_set_margin_bottom(instreditor, 6);
  instrtitle = heading("");
  gtk_box_append(GTK_BOX(instreditor), instrtitle);

  gtk_grid_set_row_spacing(GTK_GRID(grid), 4);
  gtk_grid_set_column_spacing(GTK_GRID(grid), 8);

  instrnameentry = gtk_entry_new();
  gtk_entry_set_max_length(GTK_ENTRY(instrnameentry), MAX_INSTRNAMELEN - 1);
  gtk_editable_set_enable_undo(GTK_EDITABLE(instrnameentry), FALSE);
  g_signal_connect(instrnameentry, "changed", G_CALLBACK(oninstrnamechanged), NULL);
  gtk_grid_attach(GTK_GRID(grid), instrnameentry, 0, row++, 3, 1);

  for (c = 0; c < 4; c++)
  {
    adsrscales[c] = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 15, 1);
    wheelscrollspanel(adsrscales[c]);
    gtk_scale_set_draw_value(GTK_SCALE(adsrscales[c]), TRUE);
    gtk_scale_set_value_pos(GTK_SCALE(adsrscales[c]), GTK_POS_RIGHT);
    gtk_scale_set_format_value_func(GTK_SCALE(adsrscales[c]), formathexnybble, NULL, NULL);
    gtk_widget_set_hexpand(adsrscales[c], TRUE);
    g_signal_connect(adsrscales[c], "value-changed", G_CALLBACK(onadsrchanged), GINT_TO_POINTER(F_ATTACK + c));
    addrow(grid, row++, adsrnames[c], NULL, adsrscales[c], NULL);
  }

  envelopearea = gtk_drawing_area_new();
  instrroot = paned;
  panels_setscale(textscale);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(envelopearea), drawenvelope, NULL, NULL);
  gtk_grid_attach(GTK_GRID(grid), envelopearea, 0, row++, 3, 1);

  wavebutton = gtk_button_new_from_icon_name("document-edit-symbolic");
  gtk_widget_add_css_class(wavebutton, "flat");
  gtk_widget_set_tooltip_text(wavebutton, "Choose the waveform bits");
  g_signal_connect(wavebutton, "clicked", G_CALLBACK(onfirstwave), NULL);
  addrow(grid, row++, "Wavetable", "Wavetable start position (0 = none)", fieldspin(F_WAVEPTR, 255, 2), tablebutton(WTBL));
  addrow(grid, row++, "Pulsetable", "Pulsetable start position (0 = none)", fieldspin(F_PULSEPTR, 255, 2), tablebutton(PTBL));
  addrow(grid, row++, "Filtertable", "Filtertable start position (0 = none)", fieldspin(F_FILTERPTR, 255, 2), tablebutton(FTBL));
  addrow(grid, row++, "Vibrato", "Vibrato parameter: speedtable position (0 = none)", fieldspin(F_VIBPARAM, 255, 2), tablebutton(STBL));
  addrow(grid, row++, "Vibrato delay", "Frames before instrument vibrato starts", fieldspin(F_VIBDELAY, 255, 2), NULL);
  addrow(grid, row++, "HR / gate timer", "Frames before the note to do gateoff and hard restart. "
    "$80 disables hard restart, $40 disables gateoff.", fieldspin(F_GATETIMER, 255, 2), NULL);
  addrow(grid, row++, "1st frame wave", "Waveform on the first frame of a note ($00 = no change, "
    "$FE/$FF = gate off/on without waveform change)", fieldspin(F_FIRSTWAVE, 255, 2), wavebutton);
  gtk_box_append(GTK_BOX(instreditor), grid);

  gesture = gtk_gesture_click_new();
  gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(gesture), GTK_PHASE_CAPTURE);
  g_signal_connect(gesture, "pressed", G_CALLBACK(ontestpressed), NULL);
  g_signal_connect(gesture, "released", G_CALLBACK(ontestreleased), NULL);
  gtk_widget_add_controller(test, GTK_EVENT_CONTROLLER(gesture));
  gtk_widget_set_focusable(test, FALSE);
  gtk_widget_set_tooltip_text(test, "Play the instrument while held (Space in the instrument list)");
  gtk_box_append(GTK_BOX(instreditor), test);

  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(editorscroll), instreditor);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(editorscroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_paned_set_end_child(GTK_PANED(paned), editorscroll);
  gtk_paned_set_shrink_end_child(GTK_PANED(paned), FALSE);
  gtk_paned_set_position(GTK_PANED(paned), 260);
  gtk_widget_set_size_request(paned, 320, -1);
  return paned;
}

//
// Song information
//

static void onsongtextchanged(GtkEditable *editable, gpointer data)
{
  static char *fields[] = {songname, authorname, copyrightname};

  if (syncing) return;
  ui_fromutf8(fields[GPOINTER_TO_INT(data)], gtk_editable_get_text(editable), MAX_STR);
  undo_checkpoint(KEY_SONGTEXT(GPOINTER_TO_INT(data)));
  ui_edited();
}

GtkWidget *panel_songinfo_new(void)
{
  static const char *labels[] = {"Name", "Author", "Copyright"};
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  GtkWidget *grid = gtk_grid_new();
  int c;

  gtk_widget_set_margin_start(box, 8);
  gtk_widget_set_margin_end(box, 8);
  gtk_widget_set_margin_top(box, 6);
  gtk_box_append(GTK_BOX(box), heading("Song"));
  gtk_grid_set_row_spacing(GTK_GRID(grid), 4);
  gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
  for (c = 0; c < 3; c++)
  {
    GtkWidget *label = gtk_label_new(labels[c]);

    gtk_label_set_xalign(GTK_LABEL(label), 0);
    songentries[c] = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(songentries[c]), MAX_STR - 1);
    gtk_editable_set_enable_undo(GTK_EDITABLE(songentries[c]), FALSE);
    gtk_widget_set_hexpand(songentries[c], TRUE);
    g_signal_connect(songentries[c], "changed", G_CALLBACK(onsongtextchanged), GINT_TO_POINTER(c));
    gtk_grid_attach(GTK_GRID(grid), label, 0, c, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), songentries[c], 1, c, 1, 1);
  }
  songnameentry = songentries[0];
  gtk_box_append(GTK_BOX(box), grid);
  gtk_widget_set_size_request(box, 260, -1);
  return box;
}

//
// Synchronization with the engine state
//

static void settext(GtkWidget *editable, const char *text)
{
  // Leave a field alone while the user is typing in it, unless the song
  // changed underneath it (undo)
  if ((!forcesync) && (gtk_widget_has_focus(editable) || (gtk_widget_get_focus_child(editable)))) return;
  if (strcmp(gtk_editable_get_text(GTK_EDITABLE(editable)), text))
    gtk_editable_set_text(GTK_EDITABLE(editable), text);
}

// Number of patterns that use each instrument, recounted when the song changes
static int instrusecount[MAX_INSTR];

static void countinstruments(void)
{
  static unsigned countedversion = (unsigned)-1;
  unsigned char used[MAX_INSTR];
  int p, r;

  if (undo_version() == countedversion) return;
  countedversion = undo_version();
  memset(instrusecount, 0, sizeof instrusecount);
  for (p = 0; p < MAX_PATT; p++)
  {
    memset(used, 0, sizeof used);
    for (r = 0; r < pattlen[p]; r++)
    {
      unsigned char i = pattern[p][r * 4 + 1];
      if ((i) && (i < MAX_INSTR)) used[i] = 1;
    }
    for (r = 1; r < MAX_INSTR; r++) instrusecount[r] += used[r];
  }
}

void panels_sync(void)
{
  static char *fields[] = {songname, authorname, copyrightname};
  GtkListBoxRow *row;
  char buf[64];
  int c;

  if (!instrlist) return;
  syncing = 1;

  countinstruments();
  for (c = 0; c < MAX_INSTR; c++)
  {
    if (c) snprintf(buf, sizeof buf, "%02X  %s", c, ui_toutf8(instr[c].name));
    else strcpy(buf, "00  (no instrument)");
    if (strcmp(gtk_label_get_text(GTK_LABEL(instrlabels[c])), buf))
      gtk_label_set_text(GTK_LABEL(instrlabels[c]), buf);

    if ((c) && (instrusecount[c])) snprintf(buf, sizeof buf, "%d", instrusecount[c]);
    else buf[0] = 0;
    if (strcmp(gtk_label_get_text(GTK_LABEL(instrcounts[c])), buf))
    {
      gtk_label_set_text(GTK_LABEL(instrcounts[c]), buf);
      gtk_widget_set_tooltip_text(instrcounts[c], buf[0] ? "Patterns that use this instrument" : NULL);
    }
    // Unused instruments are dimmed
    if ((c) && (!instrusecount[c])) gtk_widget_add_css_class(instrlabels[c], "dim-label");
    else gtk_widget_remove_css_class(instrlabels[c], "dim-label");
  }
  row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(instrlist), einum);
  if (gtk_list_box_get_selected_row(GTK_LIST_BOX(instrlist)) != row)
    gtk_list_box_select_row(GTK_LIST_BOX(instrlist), row);

  snprintf(buf, sizeof buf, "Instrument %02X", einum);
  gtk_label_set_text(GTK_LABEL(instrtitle), buf);
  gtk_widget_set_sensitive(instreditor, einum != 0);
  settext(instrnameentry, einum ? ui_toutf8(instr[einum].name) : "");
  for (c = 0; c < 4; c++)
  {
    if ((int)gtk_range_get_value(GTK_RANGE(adsrscales[c])) != getfield(F_ATTACK + c))
      gtk_range_set_value(GTK_RANGE(adsrscales[c]), getfield(F_ATTACK + c));
  }
  for (c = F_WAVEPTR; c < NUMFIELDS; c++)
  {
    if ((int)gtk_spin_button_get_value(GTK_SPIN_BUTTON(fieldspins[c])) != getfield(c))
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(fieldspins[c]), getfield(c));
  }
  gtk_widget_queue_draw(envelopearea);

  for (c = 0; c < 3; c++)
    settext(songentries[c], ui_toutf8(fields[c]));

  syncing = 0;
}

// Update every field, including the one being typed in (after undo/redo)
void panels_syncall(void)
{
  forcesync = 1;
  panels_sync();
  forcesync = 0;
}
