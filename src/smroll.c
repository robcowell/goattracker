//
// SidMonkey: the piano roll for the selected clip
//
// Shows the clip's pattern as notes (groll.c) at the pitch they sound, so a
// transposed clip shows transposed notes. Click to add a note (drag to set
// its length), drag a note to move it, drag its right edge to resize it,
// right-click or Delete to remove it. Each edit writes the pattern back,
// which changes every clip that plays the pattern.
//

#include "sm.h"
#include "groll.h"

#define KEYW 56
#define RULERH 18
#define PITCHH 11
#define NPITCH (LASTNOTE - FIRSTNOTE + 1)
#define EDGE 5

static GtkWidget *stack, *grid, *infolabel;
static GtkDropDown *instrdrop, *lengthdrop, *effectdrop;
static GtkStringList *instrlist;
static GtkSpinButton *rowsspin;
static GtkScrolledWindow *scroller;
static int syncing;

static int rollchn = -1, rollpatt = -1, rolltrans;
static ROLL roll;
static int selnote = -1;
static int colpx = 14;
static int curinstr = 1;
static int lengths[] = {1, 2, 4, 8, 16};
static int notelen = 4;

// Note effects: pattern commands, with their speedtable rows. Slides and
// vibrato use note-independent speeds (left side $80 set), so they sound
// the same at every pitch.
typedef struct
{
  const char *name;
  unsigned char cmd, l, r;   // l = r = 0 with cmd 3: legato (300)
} EFFECT;

static const EFFECT effects[] = {
  {"No effect", 0, 0, 0},
  {"Legato (no new attack)", CMD_TONEPORTA, 0, 0},
  {"Slide in, fast", CMD_TONEPORTA, 0x80, 0x00},
  {"Slide in, medium", CMD_TONEPORTA, 0x80, 0x02},
  {"Slide in, slow", CMD_TONEPORTA, 0x80, 0x04},
  {"Vibrato, gentle", CMD_VIBRATO, 0x84, 0x05},
  {"Vibrato, normal", CMD_VIBRATO, 0x83, 0x03},
  {"Vibrato, wide", CMD_VIBRATO, 0x83, 0x02},
  {"Other command", 0, 0, 0}};

#define EFFECT_OTHER ((int)G_N_ELEMENTS(effects) - 1)

static int effectof(const ROLLNOTE *n)
{
  int i;

  if (!n->cmd) return 0;
  for (i = 1; i < EFFECT_OTHER; i++)
  {
    if (effects[i].cmd != n->cmd) continue;
    if ((!effects[i].l) && (!effects[i].r))
    {
      if (!n->data) return i;
      continue;
    }
    if ((n->data) && (ltable[STBL][n->data - 1] == effects[i].l) && (rtable[STBL][n->data - 1] == effects[i].r))
      return i;
  }
  return EFFECT_OTHER;
}

static void synceffect(void)
{
  int sel = (selnote >= 0) && (selnote < roll.nnotes);

  syncing = 1;
  gtk_widget_set_sensitive(GTK_WIDGET(effectdrop), sel);
  gtk_drop_down_set_selected(effectdrop, sel ? effectof(&roll.notes[selnote]) : 0);
  syncing = 0;
}

// Dragging
enum {DRAG_NONE, DRAG_NEW, DRAG_MOVE, DRAG_RESIZE};
static int dragmode, dragmoved;
static ROLLNOTE dragorig;
static int dragrow0, dragpitch0;
static int previewing = -1;

// Playing notes from the keyboard or MIDI, and step entry: each note played
// is written at the step cursor, which then moves on by the note length
static GtkToggleButton *stepbutton;
static GtkSpinButton *octavespin;
static int cursorrow;
static unsigned held[16];
static int nheld;

static void preview(int note, int instr)
{
  if (isplaying()) return;
  if (!instr) instr = curinstr;
  host_preview(note, instr, rollchn);
  previewing = rollchn;
}

static void stoppreview(void)
{
  if (previewing >= 0) host_release(previewing);
  previewing = -1;
}

// Sounding pitch (0 = the lowest note) of a pattern note in this clip
static int pitchof(unsigned char note)
{
  return note - FIRSTNOTE + rolltrans;
}

static int notefrompitch(int pitch)
{
  int n = pitch - rolltrans + FIRSTNOTE;

  if (n < FIRSTNOTE) n = FIRSTNOTE;
  if (n > LASTNOTE) n = LASTNOTE;
  return n;
}

static double ypitch(int pitch)
{
  return RULERH + (NPITCH - 1 - pitch) * PITCHH;
}

static void setsize(void)
{
  gtk_widget_set_size_request(grid, KEYW + (roll.rows + 8) * colpx, RULERH + NPITCH * PITCHH);
}

static void pitchname(int pitch, char *buf, int size)
{
  if ((pitch < 0) || (pitch >= NPITCH)) snprintf(buf, size, "?");
  else snprintf(buf, size, "%c%s%d", notename[pitch][0], notename[pitch][1] == '#' ? "#" : "", pitch / 12);
}

static void updateinfo(void)
{
  char buf[256], extra[160] = "";
  int uses;

  if (rollpatt < 0) return;
  uses = arr_patternuses(rollpatt);
  if (rolltrans) snprintf(extra, sizeof extra, ", shown transposed %s %d", rolltrans > 0 ? "up" : "down", abs(rolltrans));
  snprintf(buf, sizeof buf, "<b>Voice %d</b> · pattern %02X%s%s", rollchn + 1, rollpatt, extra,
    uses > 1 ? " · <i>changes here change every clip that plays this pattern</i>" : "");
  gtk_label_set_markup(GTK_LABEL(infolabel), buf);
}

static void syncinstrument(void)
{
  int instr = curinstr;

  if ((selnote >= 0) && (selnote < roll.nnotes))
  {
    int i = roll_instrument(&roll, selnote);
    if (i) instr = i;
  }
  // Selecting a note picks up its instrument for the next notes
  curinstr = instr;
  syncing = 1;
  if (instr >= 1) gtk_drop_down_set_selected(instrdrop, instr - 1);
  syncing = 0;
  sound_select(curinstr);
}

// The sidebar chose an instrument
void roll_setinstrument(int instrnum)
{
  curinstr = instrnum;
  syncing = 1;
  if ((instrnum >= 1) && ((guint)instrnum <= g_list_model_get_n_items(G_LIST_MODEL(instrlist))))
    gtk_drop_down_set_selected(instrdrop, instrnum - 1);
  syncing = 0;
}

static void describenote(void)
{
  char buf[256], name[8], iname[MAX_STR * 2 + 24], fx[48] = "";
  char *effect;
  const ROLLNOTE *n;
  int i;

  synceffect();
  if ((selnote < 0) || (selnote >= roll.nnotes))
  {
    sm_setstatus("Click to add a note (drag to set its length). Drag a note to move it, its right edge to resize it.");
    return;
  }
  n = &roll.notes[selnote];
  i = roll_instrument(&roll, selnote);
  pitchname(pitchof(n->note), name, sizeof name);
  if (i) snprintf(iname, sizeof iname, "instrument %02X %s", i, sm_toutf8(instr[i].name));
  else snprintf(iname, sizeof iname, "the instrument the voice already had");
  if (n->cmd)
  {
    effect = g_ascii_strdown(effectof(n) == EFFECT_OTHER ? "with a pattern command" : effects[effectof(n)].name, -1);
    snprintf(fx, sizeof fx, ", %s", effect);
    g_free(effect);
  }
  snprintf(buf, sizeof buf, "%s at row %d, %d row%s long, %s%s%s", name, n->start, n->len, n->len == 1 ? "" : "s",
    iname, fx, (!n->keyoff) && (n->start + n->len >= roll.rows) ? ". It holds on into the next clip" : "");
  sm_setstatus(buf);
}

void roll_refresh(void)
{
  if (rollpatt < 0) return;
  roll_read(rollpatt, &roll);
  if (selnote >= roll.nnotes) selnote = -1;
  if (cursorrow > roll.rows) cursorrow = roll.rows;
  syncing = 1;
  gtk_spin_button_set_value(rowsspin, roll.rows);
  syncing = 0;
  setsize();
  updateinfo();
  gtk_widget_queue_draw(grid);
}

void roll_refreshinstruments(void)
{
  int c, n = 0;
  const char **names;

  for (c = 1; c < MAX_INSTR; c++)
    if ((instr[c].name[0]) || (c <= highestusedinstr)) n = c;
  if (n < 8) n = 8;
  names = g_new0(const char *, n + 1);
  for (c = 1; c <= n; c++)
    names[c - 1] = g_strdup_printf("%02X %s", c, instr[c].name[0] ? sm_toutf8(instr[c].name) : "(empty)");
  syncing = 1;
  gtk_string_list_splice(instrlist, 0, g_list_model_get_n_items(G_LIST_MODEL(instrlist)), names);
  if (curinstr > n) curinstr = 1;
  gtk_drop_down_set_selected(instrdrop, curinstr - 1);
  syncing = 0;
  for (c = 0; c < n; c++) g_free((char *)names[c]);
  g_free(names);
}

// Centre the view on the clip's notes. The first time the roll is shown it
// has no size yet, so this waits (a few frames at most) until it has.
static int scrolltries;

static gboolean scrolltonotes(gpointer data)
{
  GtkAdjustment *v = gtk_scrolled_window_get_vadjustment(scroller);
  int i, sum = 0, mid = 36;

  if ((gtk_adjustment_get_page_size(v) <= 0) || (gtk_adjustment_get_upper(v) <= gtk_adjustment_get_page_size(v)))
    return ++scrolltries < 20 ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
  for (i = 0; i < roll.nnotes; i++) sum += pitchof(roll.notes[i].note);
  if (roll.nnotes) mid = sum / roll.nnotes;
  gtk_adjustment_set_value(v, ypitch(mid) - gtk_adjustment_get_page_size(v) / 2);
  gtk_adjustment_set_value(gtk_scrolled_window_get_hadjustment(scroller), 0);
  return G_SOURCE_REMOVE;
}

// The arrangement selected a clip (patt < 0: none)
void roll_show(int chnum, int patt, int trans)
{
  int changed = (patt != rollpatt) || (chnum != rollchn) || (trans != rolltrans);

  if (!stack) return;
  stoppreview();
  rollchn = chnum;
  rollpatt = patt;
  rolltrans = trans;
  if (patt < 0)
  {
    gtk_stack_set_visible_child_name(GTK_STACK(stack), "empty");
    return;
  }
  gtk_stack_set_visible_child_name(GTK_STACK(stack), "roll");
  if (changed)
  {
    selnote = -1;
    cursorrow = 0;
  }
  roll_refresh();
  if (changed)
  {
    // After the layout, so the adjustment knows its range
    scrolltries = 0;
    g_timeout_add(20, scrolltonotes, NULL);
  }
}

static void commit(void)
{
  int start = ((selnote >= 0) && (selnote < roll.nnotes)) ? roll.notes[selnote].start : -1;
  int i;

  // A note moved onto another one's row replaces it
  for (i = 0; (start >= 0) && (i < roll.nnotes); i++)
    if ((i != selnote) && (roll.notes[i].start == start))
    {
      memmove(&roll.notes[i], &roll.notes[i + 1], (roll.nnotes - i - 1) * sizeof(ROLLNOTE));
      roll.nnotes--;
      if (selnote > i) selnote--;
      i--;
    }
  host_lock();
  roll_write(rollpatt, &roll);
  host_unlock();
  sm_edited();
  roll_read(rollpatt, &roll);
  selnote = -1;
  for (i = 0; i < roll.nnotes; i++)
    if (roll.notes[i].start == start) selnote = i;
  arrange_refresh();
  updateinfo();
  describenote();
  gtk_widget_queue_draw(grid);
}

//
// Drawing
//

static int isblack(int pitch)
{
  int k = pitch % 12;
  return (k == 1) || (k == 3) || (k == 6) || (k == 8) || (k == 10);
}

static void drawgrid(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data)
{
  GdkRGBA fg;
  PangoLayout *pl = gtk_widget_create_pango_layout(GTK_WIDGET(area), NULL);
  PangoFontDescription *small = pango_font_description_from_string("Sans 7");
  double hx = gtk_adjustment_get_value(gtk_scrolled_window_get_hadjustment(scroller));
  double vy = gtk_adjustment_get_value(gtk_scrolled_window_get_vadjustment(scroller));
  int beat = stepsize > 0 ? stepsize : 4, p, row, i, th;
  int gridw = roll.rows * colpx;
  char buf[32];

  gtk_widget_get_color(GTK_WIDGET(area), &fg);
  pango_layout_set_font_description(pl, small);

  // Pitch rows
  for (p = 0; p < NPITCH; p++)
  {
    cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, isblack(p) ? 0.07 : 0.025);
    cairo_rectangle(cr, KEYW, ypitch(p), gridw, PITCHH);
    cairo_fill(cr);
    if (p % 12 == 0)
    {
      cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.15);
      cairo_rectangle(cr, KEYW, ypitch(p) + PITCHH - 1, gridw, 1);
      cairo_fill(cr);
    }
  }
  // Beat and bar lines; the area past the pattern end is outside the clip
  for (row = 0; row <= roll.rows; row += 1)
  {
    double a = (row % (beat * 4) == 0) ? 0.22 : ((row % beat == 0) ? 0.11 : 0.04);
    cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, a);
    cairo_rectangle(cr, KEYW + row * colpx, RULERH, 1, NPITCH * PITCHH);
    cairo_fill(cr);
  }
  cairo_set_source_rgba(cr, 0, 0, 0, 0.18);
  cairo_rectangle(cr, KEYW + gridw + 1, RULERH, width - KEYW - gridw, height);
  cairo_fill(cr);

  // Notes
  for (i = 0; i < roll.nnotes; i++)
  {
    const ROLLNOTE *n = &roll.notes[i];
    int instrnum = roll_instrument(&roll, i);
    const double *col = sm_palette[instrnum % 8];
    double x = KEYW + n->start * colpx + 1, y = ypitch(pitchof(n->note)) + 1;
    double w = n->len * colpx - 2, h = PITCHH - 2;
    int holds = (!n->keyoff) && (n->start + n->len >= roll.rows);

    cairo_set_source_rgba(cr, col[0], col[1], col[2], instrnum ? 0.95 : 0.6);
    cairo_rectangle(cr, x, y, w > 2 ? w : 2, h);
    cairo_fill(cr);
    if (holds)
    {
      cairo_move_to(cr, x + w, y);
      cairo_line_to(cr, x + w + 6, y + h / 2);
      cairo_line_to(cr, x + w, y + h);
      cairo_close_path(cr);
      cairo_fill(cr);
    }
    if (i == selnote)
    {
      cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.95);
      cairo_set_line_width(cr, 2);
      cairo_rectangle(cr, x, y, w > 2 ? w : 2, h);
      cairo_stroke(cr);
    }
    if (n->cmd)
    {
      cairo_set_source_rgba(cr, 0.1, 0.1, 0.15, 0.8);
      cairo_arc(cr, x + 3, y + h / 2, 1.8, 0, 2 * G_PI);
      cairo_fill(cr);
    }
    if (w > 26)
    {
      pitchname(pitchof(n->note), buf, sizeof buf);
      cairo_set_source_rgba(cr, 0.1, 0.1, 0.15, 0.85);
      pango_layout_set_text(pl, buf, -1);
      pango_layout_get_pixel_size(pl, NULL, &th);
      cairo_move_to(cr, x + (n->cmd ? 7 : 3), y + (h - th) / 2);
      pango_cairo_show_layout(cr, pl);
    }
  }

  // Step cursor
  if (cursorrow <= roll.rows)
  {
    int on = gtk_toggle_button_get_active(stepbutton);
    double x = KEYW + cursorrow * colpx;
    cairo_set_source_rgba(cr, 0.35, 0.65, 1.0, on ? 0.9 : 0.35);
    cairo_rectangle(cr, x - 1, RULERH, 2, NPITCH * PITCHH);
    cairo_fill(cr);
    cairo_move_to(cr, x - 5, vy + RULERH);
    cairo_line_to(cr, x + 5, vy + RULERH);
    cairo_line_to(cr, x, vy + RULERH + 6);
    cairo_close_path(cr);
    cairo_fill(cr);
  }

  // Playhead
  if ((isplaying()) && (rollchn >= 0) && (chn[rollchn].pattnum == rollpatt))
  {
    int r = chn[rollchn].pattptr / 4;
    cairo_set_source_rgba(cr, 0.90, 0.20, 0.25, 0.9);
    cairo_rectangle(cr, KEYW + r * colpx, RULERH, 2, NPITCH * PITCHH);
    cairo_fill(cr);
  }

  // The keyboard stays at the left and the ruler at the top
  {
    GdkRGBA bg;
    gtk_widget_get_color(GTK_WIDGET(area), &bg);
    for (p = 0; p < NPITCH; p++)
    {
      double y = ypitch(p);
      if (isblack(p)) cairo_set_source_rgb(cr, 0.16, 0.16, 0.18);
      else cairo_set_source_rgb(cr, 0.93, 0.93, 0.91);
      cairo_rectangle(cr, hx, y, KEYW, PITCHH);
      cairo_fill(cr);
      cairo_set_source_rgba(cr, 0, 0, 0, 0.25);
      cairo_rectangle(cr, hx, y + PITCHH - 1, KEYW, 1);
      cairo_fill(cr);
      if (p % 12 == 0)
      {
        pitchname(p, buf, sizeof buf);
        cairo_set_source_rgb(cr, 0.3, 0.3, 0.32);
        pango_layout_set_text(pl, buf, -1);
        pango_layout_get_pixel_size(pl, NULL, &th);
        cairo_move_to(cr, hx + KEYW - 22, y + (PITCHH - th) / 2);
        pango_cairo_show_layout(cr, pl);
      }
    }
    cairo_set_source_rgba(cr, 0, 0, 0, 0.4);
    cairo_rectangle(cr, hx + KEYW - 1, vy, 1, height);
    cairo_fill(cr);

    cairo_set_source_rgba(cr, 0.13, 0.13, 0.15, 1);
    cairo_rectangle(cr, hx, vy, width, RULERH);
    cairo_fill(cr);
    for (row = 0; row < roll.rows; row += beat)
    {
      if (row % (beat * 4)) continue;
      snprintf(buf, sizeof buf, "%d", row / (beat * 4) + 1);
      cairo_set_source_rgba(cr, 0.85, 0.85, 0.88, 0.8);
      pango_layout_set_text(pl, buf, -1);
      cairo_move_to(cr, KEYW + row * colpx + 3, vy + 2);
      pango_cairo_show_layout(cr, pl);
    }
    cairo_set_source_rgba(cr, 0.13, 0.13, 0.15, 1);
    cairo_rectangle(cr, hx, vy, KEYW, RULERH);
    cairo_fill(cr);
    (void)bg;
  }

  pango_font_description_free(small);
  g_object_unref(pl);
}

//
// Mouse and keys
//

static int rowat(double x)
{
  int r = (int)((x - KEYW) / colpx);
  if (r < 0) r = 0;
  return r;
}

static int pitchat(double y)
{
  int p = NPITCH - 1 - (int)((y - RULERH) / PITCHH);
  if (p < 0) p = 0;
  if (p >= NPITCH) p = NPITCH - 1;
  return p;
}

static int hitnote(double x, double y, int *edge)
{
  int row = rowat(x), p = pitchat(y), i;

  for (i = 0; i < roll.nnotes; i++)
  {
    const ROLLNOTE *n = &roll.notes[i];
    if ((pitchof(n->note) == p) && (row >= n->start) && (row < n->start + n->len))
    {
      *edge = x > KEYW + (n->start + n->len) * colpx - EDGE;
      return i;
    }
  }
  // The right edge can be a few pixels past the note
  for (i = 0; i < roll.nnotes; i++)
  {
    const ROLLNOTE *n = &roll.notes[i];
    double ex = KEYW + (n->start + n->len) * colpx;
    if ((pitchof(n->note) == p) && (x >= ex - EDGE) && (x < ex + EDGE))
    {
      *edge = 1;
      return i;
    }
  }
  return -1;
}

static int inkeys(double x)
{
  return x - gtk_adjustment_get_value(gtk_scrolled_window_get_hadjustment(scroller)) < KEYW;
}

static int inruler(double y)
{
  return y - gtk_adjustment_get_value(gtk_scrolled_window_get_vadjustment(scroller)) < RULERH;
}

static void ondragbegin(GtkGestureDrag *gesture, double x, double y, gpointer data)
{
  int edge = 0, i;
  int button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));

  gtk_widget_grab_focus(grid);
  dragmode = DRAG_NONE;
  dragmoved = 0;
  if (rollpatt < 0) return;
  if (inruler(y))
  {
    if (!inkeys(x))
    {
      cursorrow = rowat(x + colpx / 2);
      if (cursorrow > roll.rows) cursorrow = roll.rows;
      gtk_widget_queue_draw(grid);
    }
    return;
  }
  if (inkeys(x))
  {
    preview(notefrompitch(pitchat(y)), curinstr);
    return;
  }
  i = hitnote(x, y, &edge);
  if (button == GDK_BUTTON_SECONDARY)
  {
    if (i >= 0)
    {
      selnote = i;
      memmove(&roll.notes[i], &roll.notes[i + 1], (roll.nnotes - i - 1) * sizeof(ROLLNOTE));
      roll.nnotes--;
      selnote = -1;
      commit();
    }
    return;
  }
  if (i >= 0)
  {
    selnote = i;
    dragmode = edge ? DRAG_RESIZE : DRAG_MOVE;
    preview(roll.notes[i].note, roll_instrument(&roll, i));
  }
  else
  {
    int row = rowat(x);
    ROLLNOTE *n;

    if ((row >= roll.rows) || (roll.nnotes >= ROLL_MAXNOTES)) return;
    // A voice plays one note at a time: a note already starting on this
    // row is replaced
    for (i = 0; i < roll.nnotes; i++)
      if (roll.notes[i].start == row) break;
    if (i == roll.nnotes) roll.nnotes++;
    n = &roll.notes[i];
    memset(n, 0, sizeof *n);
    n->start = row;
    n->len = notelen;
    if (n->start + n->len > roll.rows) n->len = roll.rows - n->start;
    n->note = notefrompitch(pitchat(y));
    n->instr = curinstr;
    n->keyoff = 1;
    selnote = i;
    dragmode = DRAG_NEW;
    preview(n->note, curinstr);
  }
  dragorig = roll.notes[selnote];
  dragrow0 = rowat(x);
  dragpitch0 = pitchat(y);
  syncinstrument();
  describenote();
  gtk_widget_queue_draw(grid);
}

static void ondragupdate(GtkGestureDrag *gesture, double dx, double dy, gpointer data)
{
  double sx, sy;
  ROLLNOTE *n;
  int drow, dpitch;

  if ((dragmode == DRAG_NONE) || (selnote < 0)) return;
  gtk_gesture_drag_get_start_point(gesture, &sx, &sy);
  n = &roll.notes[selnote];
  drow = rowat(sx + dx) - dragrow0;
  dpitch = pitchat(sy + dy) - dragpitch0;
  if (dragmode == DRAG_MOVE)
  {
    int start = dragorig.start + drow;
    unsigned char note = notefrompitch(pitchof(dragorig.note) + dpitch);
    if (start < 0) start = 0;
    if (start >= roll.rows) start = roll.rows - 1;
    if ((start != n->start) || (note != n->note)) dragmoved = 1;
    if (note != n->note) preview(note, roll_instrument(&roll, selnote));
    n->start = start;
    n->note = note;
    n->len = dragorig.len;
    if (n->start + n->len > roll.rows) n->len = roll.rows - n->start;
  }
  else
  {
    int len = dragorig.len + drow;
    if (len < 1) len = 1;
    if (n->start + len > roll.rows) len = roll.rows - n->start;
    if (len != n->len) dragmoved = 1;
    n->len = len;
    n->keyoff = 1;
  }
  describenote();
  gtk_widget_queue_draw(grid);
}

static void ondragend(GtkGestureDrag *gesture, double dx, double dy, gpointer data)
{
  int mode = dragmode;

  stoppreview();
  dragmode = DRAG_NONE;
  if ((mode == DRAG_NEW) || (((mode == DRAG_MOVE) || (mode == DRAG_RESIZE)) && (dragmoved)))
  {
    // Remember the length of drawn or resized notes for the next ones
    if ((mode != DRAG_MOVE) && (selnote >= 0)) notelen = roll.notes[selnote].len;
    commit();
  }
  if ((selnote >= 0) && (selnote < roll.nnotes))
  {
    cursorrow = roll.notes[selnote].start + roll.notes[selnote].len;
    gtk_widget_queue_draw(grid);
  }
}

// A note played from the keyboard or MIDI (FIRSTNOTE-based, as heard). It
// sounds on a free voice; with step entry on it is also written at the step
// cursor, unless another played note is still held (a chord)
void roll_noteon(unsigned id, int note)
{
  int i, len;

  for (i = 0; i < nheld; i++)
    if (held[i] == id) return;    // key repeat
  if (nheld < (int)G_N_ELEMENTS(held)) held[nheld++] = id;
  jam_noteon(id, note, curinstr, rollchn >= 0 ? rollchn : 0);

  if ((rollpatt < 0) || (!gtk_toggle_button_get_active(stepbutton)) || (nheld > 1)) return;
  if (cursorrow >= roll.rows)
  {
    sm_toast("The step cursor is at the end of the clip: click the ruler to move it");
    return;
  }
  // The new note replaces whatever starts during it
  len = notelen;
  if (cursorrow + len > roll.rows) len = roll.rows - cursorrow;
  for (i = 0; i < roll.nnotes; i++)
    if ((roll.notes[i].start >= cursorrow) && (roll.notes[i].start < cursorrow + len))
    {
      memmove(&roll.notes[i], &roll.notes[i + 1], (roll.nnotes - i - 1) * sizeof(ROLLNOTE));
      roll.nnotes--;
      i--;
    }
  if (roll.nnotes >= ROLL_MAXNOTES) return;
  i = roll.nnotes++;
  memset(&roll.notes[i], 0, sizeof roll.notes[i]);
  roll.notes[i].start = cursorrow;
  roll.notes[i].len = len;
  roll.notes[i].note = notefrompitch(note - FIRSTNOTE);
  roll.notes[i].instr = curinstr;
  roll.notes[i].keyoff = 1;
  selnote = i;
  cursorrow += roll.notes[i].len;
  commit();
}

void roll_noteoff(unsigned id)
{
  int i;

  for (i = 0; i < nheld; i++)
    if (held[i] == id)
    {
      held[i] = held[--nheld];
      break;
    }
  jam_noteoff(id);
}

void roll_releaseall(void)
{
  nheld = 0;
  jam_releaseall();
}

// Raw key codes identify the physical key: the keyval without Shift, so
// note keys work the same with Shift held and on any layout level
static unsigned rawkey_(GtkEventControllerKey *controller, guint keyval, guint keycode)
{
  GdkEvent *event = gtk_event_controller_get_current_event(GTK_EVENT_CONTROLLER(controller));
  GdkKeymapKey *keys;
  guint *keyvals;
  int n, c;
  guint layout = event ? gdk_key_event_get_layout(event) : 0;

  if (gdk_display_map_keycode(gtk_widget_get_display(grid), keycode, &keys, &keyvals, &n))
  {
    for (c = 0; c < n; c++)
      if (((guint)keys[c].group == layout) && (keys[c].level == 0))
      {
        keyval = keyvals[c];
        break;
      }
    g_free(keys);
    g_free(keyvals);
  }
  keyval = gdk_keyval_to_lower(keyval);
  return keyval < 256 ? keyval : 0;
}

static void onkeyreleased(GtkEventControllerKey *controller, guint keyval, guint keycode, GdkModifierType state,
  gpointer data)
{
  unsigned raw = rawkey_(controller, keyval, keycode);
  if (raw) roll_noteoff(raw);
}

static void deleteselected(void)
{
  if ((selnote < 0) || (selnote >= roll.nnotes)) return;
  memmove(&roll.notes[selnote], &roll.notes[selnote + 1], (roll.nnotes - selnote - 1) * sizeof(ROLLNOTE));
  roll.nnotes--;
  selnote = -1;
  commit();
}

static gboolean onkey(GtkEventControllerKey *controller, guint keyval, guint keycode, GdkModifierType state,
  gpointer data)
{
  int shift = (state & GDK_SHIFT_MASK) != 0, ctrl = (state & GDK_CONTROL_MASK) != 0;
  ROLLNOTE *n;

  // Note keys play notes (two rows, as in trackers)
  if (!(state & (GDK_CONTROL_MASK | GDK_ALT_MASK)))
  {
    unsigned raw = rawkey_(controller, keyval, keycode);
    int note = raw ? pattern_notekey(raw) : -1;
    if (note >= 0)
    {
      if (note <= LASTNOTE) roll_noteon(raw, note);
      return TRUE;
    }
  }
  switch (keyval)
  {
    // The window's Space shortcut doesn't reach past the roll's scrolled
    // window, so the roll starts and stops playback itself
    case GDK_KEY_space:
    if (state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SHIFT_MASK)) return FALSE;
    sm_togglepause();
    return TRUE;

    case GDK_KEY_Page_Up:
    case GDK_KEY_Page_Down:
    gtk_spin_button_set_value(octavespin, epoctave + (keyval == GDK_KEY_Page_Up ? 1 : -1));
    return TRUE;

    case GDK_KEY_Escape:
    if (selnote < 0) return FALSE;
    selnote = -1;
    describenote();
    gtk_widget_queue_draw(grid);
    return TRUE;
  }
  if ((selnote < 0) || (selnote >= roll.nnotes))
  {
    // Without a selected note, the arrows move the step cursor
    if ((keyval == GDK_KEY_Left) && (cursorrow > 0)) cursorrow--;
    else if ((keyval == GDK_KEY_Right) && (cursorrow < roll.rows)) cursorrow++;
    else if (keyval == GDK_KEY_Home) cursorrow = 0;
    else return FALSE;
    gtk_widget_queue_draw(grid);
    return TRUE;
  }
  n = &roll.notes[selnote];
  switch (keyval)
  {
    case GDK_KEY_Delete:
    case GDK_KEY_BackSpace: deleteselected(); return TRUE;

    case GDK_KEY_Up:
    case GDK_KEY_Down:
    {
      int p = pitchof(n->note) + ((keyval == GDK_KEY_Up) ? 1 : -1) * (shift ? 12 : 1);
      if ((p < 0) || (p >= NPITCH)) return TRUE;
      n->note = notefrompitch(p);
      preview(n->note, roll_instrument(&roll, selnote));
      commit();
      stoppreview();
      return TRUE;
    }

    case GDK_KEY_Left:
    case GDK_KEY_Right:
    {
      int d = (keyval == GDK_KEY_Right) ? 1 : -1;
      if (ctrl)
      {
        if ((n->len + d < 1) || (n->start + n->len + d > roll.rows)) return TRUE;
        n->len += d;
        n->keyoff = 1;
      }
      else
      {
        if ((n->start + d < 0) || (n->start + d >= roll.rows)) return TRUE;
        n->start += d;
        if (n->start + n->len > roll.rows) n->len = roll.rows - n->start;
      }
      commit();
      return TRUE;
    }
  }
  return FALSE;
}

static gboolean onscroll(GtkEventControllerScroll *controller, double dx, double dy, gpointer data)
{
  GdkModifierType mods = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(controller));

  if (!(mods & GDK_CONTROL_MASK)) return FALSE;
  colpx = (dy < 0) ? colpx + 2 : colpx - 2;
  if (colpx < 4) colpx = 4;
  if (colpx > 40) colpx = 40;
  setsize();
  gtk_widget_queue_draw(grid);
  return TRUE;
}

static void onscrolled(GtkAdjustment *adj, gpointer data)
{
  gtk_widget_queue_draw(grid);
}

static void oninstrument(GObject *drop, GParamSpec *pspec, gpointer data)
{
  if (syncing) return;
  curinstr = gtk_drop_down_get_selected(instrdrop) + 1;
  sound_select(curinstr);
  // Picking an instrument with a note selected gives it that instrument
  if ((selnote >= 0) && (selnote < roll.nnotes) && (roll_instrument(&roll, selnote) != curinstr))
  {
    roll.notes[selnote].instr = curinstr;
    commit();
  }
  preview(FIRSTNOTE + 36, curinstr);
  g_timeout_add_once(250, (GSourceOnceFunc)stoppreview, NULL);
}

static void oneffect(GObject *drop, GParamSpec *pspec, gpointer data)
{
  int e = gtk_drop_down_get_selected(effectdrop);
  ROLLNOTE *n;

  if ((syncing) || (selnote < 0) || (selnote >= roll.nnotes)) return;
  n = &roll.notes[selnote];
  if ((e == EFFECT_OTHER) || (e == effectof(n))) return;
  if ((effects[e].l) || (effects[e].r))
  {
    int row = roll_speedentry(effects[e].l, effects[e].r);
    if (!row)
    {
      sm_toast("The speed table is full");
      synceffect();
      return;
    }
    n->data = row;
  }
  else n->data = 0;
  n->cmd = effects[e].cmd;
  commit();
}

// The step cursor is drawn brighter while step entry is on
static void onsteptoggled(GtkToggleButton *button, gpointer data)
{
  if (grid) gtk_widget_queue_draw(grid);
}

static void onoctave(GtkSpinButton *spin, gpointer data)
{
  epoctave = (int)gtk_spin_button_get_value(spin);
}

static void onlength(GObject *drop, GParamSpec *pspec, gpointer data)
{
  notelen = lengths[gtk_drop_down_get_selected(lengthdrop)];
}

static void onrows(GtkSpinButton *spin, gpointer data)
{
  int rows = (int)gtk_spin_button_get_value(spin);

  if ((syncing) || (rollpatt < 0) || (rows == pattlen[rollpatt])) return;
  host_lock();
  roll_setlength(rollpatt, rows);
  host_unlock();
  roll_read(rollpatt, &roll);
  selnote = -1;
  sm_edited();
  arrange_refresh();
  setsize();
  gtk_widget_queue_draw(grid);
}

// A note is sounding from the roll (a preview or a played note)
int roll_busy(void)
{
  return (previewing >= 0) || (nheld > 0);
}

void roll_tick(void)
{
  static int wasplaying;
  int playing = isplaying();

  // Once more after playback stops, to take the playhead away
  if ((rollpatt >= 0) && ((playing) || (wasplaying))) gtk_widget_queue_draw(grid);
  wasplaying = playing;
}

GtkWidget *roll_new(void)
{
  GtkWidget *box, *top, *bar, *label, *empty;
  GtkGesture *gesture;
  GtkEventController *controller;
  static const char *lengthnames[] = {"¼ beat", "½ beat", "1 beat", "2 beats", "1 bar", NULL};

  box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_margin_start(top, 12);
  gtk_widget_set_margin_end(top, 12);
  gtk_widget_set_margin_top(top, 6);
  infolabel = gtk_label_new("");
  gtk_label_set_xalign(GTK_LABEL(infolabel), 0);
  gtk_label_set_ellipsize(GTK_LABEL(infolabel), PANGO_ELLIPSIZE_END);
  gtk_widget_set_hexpand(infolabel, TRUE);
  gtk_box_append(GTK_BOX(top), infolabel);
  gtk_box_append(GTK_BOX(box), top);
  bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_margin_start(bar, 12);
  gtk_widget_set_margin_end(bar, 12);
  gtk_widget_set_margin_top(bar, 6);
  gtk_widget_set_margin_bottom(bar, 6);

  stepbutton = GTK_TOGGLE_BUTTON(gtk_toggle_button_new());
  gtk_button_set_icon_name(GTK_BUTTON(stepbutton), "media-record-symbolic");
  gtk_widget_set_tooltip_text(GTK_WIDGET(stepbutton), "Step entry: notes you play on the keyboard or a MIDI "
    "controller are written at the blue cursor, which then moves on by the note length");
  g_signal_connect(stepbutton, "toggled", G_CALLBACK(onsteptoggled), NULL);
  gtk_box_append(GTK_BOX(bar), GTK_WIDGET(stepbutton));
  gtk_box_append(GTK_BOX(bar), gtk_label_new("Octave"));
  octavespin = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(0, 6, 1));
  gtk_spin_button_set_value(octavespin, epoctave);
  gtk_widget_set_tooltip_text(GTK_WIDGET(octavespin), "The octave the keyboard plays: Z to M and Q to U are two "
    "octaves of piano keys (Page Up and Page Down change it)");
  g_signal_connect(octavespin, "value-changed", G_CALLBACK(onoctave), NULL);
  gtk_box_append(GTK_BOX(bar), GTK_WIDGET(octavespin));

  gtk_box_append(GTK_BOX(bar), gtk_label_new("Instrument"));
  instrlist = gtk_string_list_new(NULL);
  instrdrop = GTK_DROP_DOWN(gtk_drop_down_new(G_LIST_MODEL(instrlist), NULL));
  gtk_widget_set_tooltip_text(GTK_WIDGET(instrdrop), "The instrument for new notes, or for the selected note");
  g_signal_connect(instrdrop, "notify::selected", G_CALLBACK(oninstrument), NULL);
  gtk_box_append(GTK_BOX(bar), GTK_WIDGET(instrdrop));

  gtk_box_append(GTK_BOX(bar), gtk_label_new("Note length"));
  lengthdrop = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(lengthnames));
  gtk_drop_down_set_selected(lengthdrop, 2);
  g_signal_connect(lengthdrop, "notify::selected", G_CALLBACK(onlength), NULL);
  gtk_box_append(GTK_BOX(bar), GTK_WIDGET(lengthdrop));

  {
    const char *names[G_N_ELEMENTS(effects) + 1];
    int i;
    for (i = 0; i < (int)G_N_ELEMENTS(effects); i++) names[i] = effects[i].name;
    names[i] = NULL;
    gtk_box_append(GTK_BOX(bar), gtk_label_new("Effect"));
    effectdrop = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(names));
    gtk_widget_set_tooltip_text(GTK_WIDGET(effectdrop), "What the selected note does as it plays");
    gtk_widget_set_sensitive(GTK_WIDGET(effectdrop), FALSE);
    g_signal_connect(effectdrop, "notify::selected", G_CALLBACK(oneffect), NULL);
    gtk_box_append(GTK_BOX(bar), GTK_WIDGET(effectdrop));
  }

  label = gtk_label_new("Rows");
  gtk_box_append(GTK_BOX(top), label);
  rowsspin = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(1, MAX_PATTROWS, 1));
  gtk_widget_set_tooltip_text(GTK_WIDGET(rowsspin), "How long this pattern is, in rows");
  g_signal_connect(rowsspin, "value-changed", G_CALLBACK(onrows), NULL);
  gtk_box_append(GTK_BOX(top), GTK_WIDGET(rowsspin));
  gtk_box_append(GTK_BOX(box), bar);

  grid = gtk_drawing_area_new();
  gtk_widget_set_focusable(grid, TRUE);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(grid), drawgrid, NULL, NULL);
  gesture = gtk_gesture_drag_new();
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), 0);
  g_signal_connect(gesture, "drag-begin", G_CALLBACK(ondragbegin), NULL);
  g_signal_connect(gesture, "drag-update", G_CALLBACK(ondragupdate), NULL);
  g_signal_connect(gesture, "drag-end", G_CALLBACK(ondragend), NULL);
  gtk_widget_add_controller(grid, GTK_EVENT_CONTROLLER(gesture));
  controller = gtk_event_controller_key_new();
  g_signal_connect(controller, "key-pressed", G_CALLBACK(onkey), NULL);
  g_signal_connect(controller, "key-released", G_CALLBACK(onkeyreleased), NULL);
  gtk_widget_add_controller(grid, controller);
  controller = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
  g_signal_connect(controller, "scroll", G_CALLBACK(onscroll), NULL);
  gtk_widget_add_controller(grid, controller);

  scroller = GTK_SCROLLED_WINDOW(gtk_scrolled_window_new());
  gtk_scrolled_window_set_child(scroller, grid);
  // Focusing the area for its keys must not scroll the view (and move it
  // under a drag)
  gtk_viewport_set_scroll_to_focus(GTK_VIEWPORT(gtk_scrolled_window_get_child(scroller)), FALSE);
  gtk_widget_set_vexpand(GTK_WIDGET(scroller), TRUE);
  g_signal_connect(gtk_scrolled_window_get_hadjustment(scroller), "value-changed", G_CALLBACK(onscrolled), NULL);
  g_signal_connect(gtk_scrolled_window_get_vadjustment(scroller), "value-changed", G_CALLBACK(onscrolled), NULL);
  gtk_box_append(GTK_BOX(box), GTK_WIDGET(scroller));

  empty = adw_status_page_new();
  adw_status_page_set_icon_name(ADW_STATUS_PAGE(empty), "input-keyboard-symbolic");
  adw_status_page_set_title(ADW_STATUS_PAGE(empty), "No Clip Selected");
  adw_status_page_set_description(ADW_STATUS_PAGE(empty), "Click a clip above to see and edit its notes here.");
  gtk_widget_add_css_class(empty, "compact");

  stack = gtk_stack_new();
  gtk_stack_add_named(GTK_STACK(stack), empty, "empty");
  gtk_stack_add_named(GTK_STACK(stack), box, "roll");
  gtk_widget_set_vexpand(stack, TRUE);
  roll_refreshinstruments();
  return stack;
}
