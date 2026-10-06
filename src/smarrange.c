//
// SidMonkey: the arrangement, three voice lanes on a timeline
//
// Each pattern a voice's orderlist plays is a clip, laid out in rows from
// the start of the song (garrange.c reads and writes the orderlists).
// Repeats show as one clip per pass, transposes as a label on the clip.
// Clips can be dragged (Ctrl to copy), duplicated, transposed and deleted;
// every edit writes the voice's orderlist back and makes an undo step.
//

#include "sm.h"
#include "garrange.h"

#define RULERH 24
#define LANEH 76
#define HEADERW 112
#define MINROWPX 1
#define MAXROWPX 24
#define DRAGSTART 6

static CLIP clips[MAX_CHN][MAX_CLIPS];
static int clipstart[MAX_CHN][MAX_CLIPS];
static int nclips[MAX_CHN];
static int totalrows;
static int rowpx = 4;

static int selchn = -1, selclip = -1;

// Playhead: the clip and row each voice is playing
static int playclip[MAX_CHN];
static int playrow[MAX_CHN];
static unsigned lastcount[MAX_CHN];
static int lastpos[MAX_CHN];

// Dragging a clip: where it would go
static int dragchn = -1, dragclip, dragging;
static double dragx, dragy, dragoffset;
static int droplane, dropindex;

static GtkWidget *timeline, *headers, *menu;
static GtkScrolledWindow *scroller;
static GSimpleActionGroup *actions;

static const double palette[][3] = {
  {0.55, 0.75, 0.98}, {0.56, 0.86, 0.62}, {0.98, 0.78, 0.45}, {0.93, 0.56, 0.60},
  {0.76, 0.64, 0.95}, {0.48, 0.86, 0.86}, {0.95, 0.66, 0.85}, {0.80, 0.84, 0.52}};

static int cliprows(const CLIP *clip)
{
  return pattlen[clip->patt] > 0 ? pattlen[clip->patt] : 1;
}

static int clipend(int c, int i)
{
  return clipstart[c][i] + cliprows(&clips[c][i]);
}

static int looprow(int c)
{
  int i;

  for (i = 0; i < nclips[c]; i++)
    if (clips[c][i].flags & CLIP_LOOPSTART) return clipstart[c][i];
  return -1;
}

static void layout(void)
{
  int c, i;

  totalrows = 0;
  for (c = 0; c < MAX_CHN; c++)
  {
    int rows = 0;
    nclips[c] = arr_read(sm_subtune(), c, clips[c], MAX_CLIPS);
    for (i = 0; i < nclips[c]; i++)
    {
      clipstart[c][i] = rows;
      rows += cliprows(&clips[c][i]);
    }
    if (rows > totalrows) totalrows = rows;
  }
}

static void setsize(void)
{
  gtk_widget_set_size_request(timeline, (totalrows + 64) * rowpx, RULERH + MAX_CHN * LANEH);
}

static void updateactions(void);

// The song data changed: lay out again, keeping the selection where it was
void arrange_refresh(void)
{
  int c, i;

  layout();
  // Clips may have moved under the playheads
  for (c = 0; c < MAX_CHN; c++)
  {
    if (playclip[c] < 0) continue;
    playclip[c] = -1;
    for (i = 0; i < nclips[c]; i++)
      if (clips[c][i].orderpos == seqpos[c])
      {
        playclip[c] = i;
        break;
      }
  }
  if ((selchn >= 0) && (selclip >= nclips[selchn])) selclip = nclips[selchn] - 1;
  if (selclip < 0) selchn = -1;
  setsize();
  updateactions();
  gtk_widget_queue_draw(timeline);
  gtk_widget_queue_draw(headers);
}

void arrange_songchanged(void)
{
  int c;

  selchn = selclip = -1;
  for (c = 0; c < MAX_CHN; c++)
  {
    playclip[c] = -1;
    lastcount[c] = seqcount[c];
    lastpos[c] = -1;
  }
  arrange_refresh();
}

static int rowsperbar(void)
{
  return (stepsize > 0 ? stepsize : 4) * 4;
}

static void barname(int row, char *buf, int size)
{
  int bar = row / rowsperbar() + 1;
  int beat = (row % rowsperbar()) / (rowsperbar() / 4) + 1;

  if (beat == 1) snprintf(buf, size, "bar %d", bar);
  else snprintf(buf, size, "bar %d beat %d", bar, beat);
}

static void text(cairo_t *cr, PangoLayout *pl, double x, double y, const char *s)
{
  pango_layout_set_text(pl, s, -1);
  cairo_move_to(cr, x, y);
  pango_cairo_show_layout(cr, pl);
}

// A miniature of the notes, scaled to the pattern's own pitch range
static void drawnotes(cairo_t *cr, int patt, double x, double y, double h)
{
  unsigned char *p = pattern[patt];
  int rows = pattlen[patt], lo = 255, hi = -1, row, start = -1, note = 0;

  for (row = 0; row < rows; row++)
  {
    int n = p[row * 4];
    if ((n >= FIRSTNOTE) && (n <= LASTNOTE))
    {
      if (n < lo) lo = n;
      if (n > hi) hi = n;
    }
  }
  if (hi < 0) return;
  if (hi - lo < 12)
  {
    lo -= (12 - (hi - lo)) / 2;
    hi = lo + 12;
  }
  for (row = 0; row <= rows; row++)
  {
    int n = (row < rows) ? p[row * 4] : KEYOFF;
    int ends = ((n >= FIRSTNOTE) && (n <= LASTNOTE)) || (n == KEYOFF);
    if ((ends) && (start >= 0))
    {
      double ny = y + h - 3 - (double)(note - lo) / (hi - lo) * (h - 6);
      double w = (row - start) * rowpx - 1;
      cairo_rectangle(cr, x + start * rowpx, ny - 1.5, w > 1 ? w : 1, 3);
      start = -1;
    }
    if ((n >= FIRSTNOTE) && (n <= LASTNOTE))
    {
      start = row;
      note = n;
    }
  }
  cairo_fill(cr);
}

static void roundrect(cairo_t *cr, double x, double y, double w, double h)
{
  double r = w > 12 ? 5 : 1;

  cairo_new_sub_path(cr);
  cairo_arc(cr, x + w - r, y + r, r, -G_PI / 2, 0);
  cairo_arc(cr, x + w - r, y + h - r, r, 0, G_PI / 2);
  cairo_arc(cr, x + r, y + h - r, r, G_PI / 2, G_PI);
  cairo_arc(cr, x + r, y + r, r, G_PI, 3 * G_PI / 2);
  cairo_close_path(cr);
}

static void drawclip(cairo_t *cr, PangoLayout *pl, const CLIP *clip, double x, double y, double w, double h,
  double alpha, int font)
{
  const double *col = palette[clip->patt % 8];
  char buf[32];

  if (w < 1) w = 1;
  roundrect(cr, x, y, w, h);
  cairo_set_source_rgba(cr, col[0], col[1], col[2], alpha);
  cairo_fill(cr);
  cairo_save(cr);
  cairo_rectangle(cr, x, y, w, h);
  cairo_clip(cr);
  cairo_set_source_rgba(cr, 0.1, 0.1, 0.15, 0.55 * alpha);
  drawnotes(cr, clip->patt, x, y + font * 1.8, h - font * 1.8);
  if (w > 24)
  {
    if (clip->trans) snprintf(buf, sizeof buf, "%02X %+d", clip->patt, clip->trans);
    else snprintf(buf, sizeof buf, "%02X", clip->patt);
    cairo_set_source_rgba(cr, 0.1, 0.1, 0.15, 0.9 * alpha);
    text(cr, pl, x + 5, y + 2, buf);
  }
  cairo_restore(cr);
}

static void drawtimeline(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data)
{
  GdkRGBA fg;
  PangoLayout *pl = gtk_widget_create_pango_layout(GTK_WIDGET(area), NULL);
  PangoFontDescription *small = pango_font_description_from_string("Sans 8");
  int bar = rowsperbar(), labelevery = 1, c, i, row;
  int font = pango_font_description_get_size(small) / PANGO_SCALE;
  char buf[64];

  gtk_widget_get_color(GTK_WIDGET(area), &fg);
  pango_layout_set_font_description(pl, small);
  while (bar * labelevery * rowpx < 48) labelevery *= 2;

  // Lanes
  for (c = 0; c < MAX_CHN; c++)
  {
    cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, (c & 1) ? 0.03 : 0.06);
    cairo_rectangle(cr, 0, RULERH + c * LANEH, width, LANEH);
    cairo_fill(cr);
  }

  // Ruler and bar lines
  for (row = 0; row * rowpx < width; row += bar)
  {
    int labelled = !((row / bar) % labelevery);
    cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, labelled ? 0.18 : 0.08);
    cairo_rectangle(cr, row * rowpx, labelled ? 0 : RULERH / 2, 1, height);
    cairo_fill(cr);
    if (labelled)
    {
      snprintf(buf, sizeof buf, "%d", row / bar + 1);
      cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.7);
      text(cr, pl, row * rowpx + 4, (RULERH - font * 1.6) / 2, buf);
    }
  }

  // Clips
  for (c = 0; c < MAX_CHN; c++)
  {
    double y = RULERH + c * LANEH + 6, h = LANEH - 12;
    int loop = looprow(c);

    for (i = 0; i < nclips[c]; i++)
    {
      double x = clipstart[c][i] * rowpx + 1, w = cliprows(&clips[c][i]) * rowpx - 2;
      int selected = (c == selchn) && (i == selclip);

      drawclip(cr, pl, &clips[c][i], x, y, w, h, (dragging && selected) ? 0.35 : (selected ? 1.0 : 0.78), font);
      if (selected)
      {
        roundrect(cr, x, y, w < 1 ? 1 : w, h);
        cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.9);
        cairo_set_line_width(cr, 2);
        cairo_stroke(cr);
      }
    }

    if (nclips[c])
    {
      int end = clipend(c, nclips[c] - 1);

      // Where the voice goes after its last clip
      cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.6);
      text(cr, pl, end * rowpx + 6, y + h / 2 - font, loop >= 0 ? "↺ repeats" : "end");
      if (loop >= 0)
      {
        cairo_move_to(cr, loop * rowpx + 1, y - 4);
        cairo_line_to(cr, loop * rowpx + 7, y - 4);
        cairo_line_to(cr, loop * rowpx + 1, y + 2);
        cairo_close_path(cr);
        cairo_fill(cr);
      }
    }
  }

  // Where a dragged clip would land, and the clip under the pointer
  if (dragging)
  {
    const CLIP *clip = &clips[dragchn][dragclip];
    int x = (dropindex < nclips[droplane]) ? clipstart[droplane][dropindex] :
      (nclips[droplane] ? clipend(droplane, nclips[droplane] - 1) : 0);

    cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.9);
    cairo_rectangle(cr, x * rowpx - 2, RULERH + droplane * LANEH + 2, 3, LANEH - 4);
    cairo_fill(cr);
    drawclip(cr, pl, clip, dragx - dragoffset, dragy - (LANEH - 12) / 2, cliprows(clip) * rowpx - 2, LANEH - 12,
      0.85, font);
  }

  // Playheads, one per voice: voices can drift apart with their own tempos
  if (isplaying())
  {
    for (c = 0; c < MAX_CHN; c++)
    {
      if ((playclip[c] < 0) || (playclip[c] >= nclips[c])) continue;
      cairo_set_source_rgba(cr, 0.90, 0.20, 0.25, 0.95);
      cairo_rectangle(cr, (clipstart[c][playclip[c]] + playrow[c]) * rowpx - 1, RULERH + c * LANEH, 2, LANEH);
      cairo_fill(cr);
    }
  }

  pango_font_description_free(small);
  g_object_unref(pl);
}

static void drawheaders(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data)
{
  GdkRGBA fg;
  PangoLayout *pl = gtk_widget_create_pango_layout(GTK_WIDGET(area), NULL);
  int c, th;
  char buf[32];

  gtk_widget_get_color(GTK_WIDGET(area), &fg);
  for (c = 0; c < MAX_CHN; c++)
  {
    double y = RULERH + c * LANEH;

    cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, (c & 1) ? 0.03 : 0.06);
    cairo_rectangle(cr, 0, y, width, LANEH);
    cairo_fill(cr);
    snprintf(buf, sizeof buf, "Voice %d", c + 1);
    pango_layout_set_text(pl, buf, -1);
    pango_layout_get_pixel_size(pl, NULL, &th);
    cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, chn[c].mute ? 0.35 : 0.95);
    cairo_move_to(cr, 12, y + LANEH / 2 - th);
    pango_cairo_show_layout(cr, pl);
    pango_layout_set_text(pl, chn[c].mute ? "muted" : "playing", -1);
    cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.5);
    cairo_move_to(cr, 12, y + LANEH / 2 + 2);
    pango_cairo_show_layout(cr, pl);
  }
  g_object_unref(pl);
}

static int lanefromy(double y)
{
  int c = ((int)y - RULERH) / LANEH;

  if (y < RULERH) c = 0;
  if (c < 0) c = 0;
  if (c >= MAX_CHN) c = MAX_CHN - 1;
  return c;
}

static int hitclip(double x, double y, int *chnum)
{
  int c = ((int)y - RULERH) / LANEH;
  int row = (int)(x / rowpx), i;

  if ((y < RULERH) || (c < 0) || (c >= MAX_CHN)) return -1;
  *chnum = c;
  for (i = 0; i < nclips[c]; i++)
    if ((row >= clipstart[c][i]) && (row < clipend(c, i))) return i;
  return -1;
}

static void describeclip(void)
{
  char buf[320], at[48], trans[48] = "";
  const CLIP *clip;
  int uses;

  if (selclip < 0)
  {
    sm_setstatus("");
    return;
  }
  clip = &clips[selchn][selclip];
  uses = arr_patternuses(clip->patt);
  barname(clipstart[selchn][selclip], at, sizeof at);
  if (clip->trans)
    snprintf(trans, sizeof trans, ", transposed %s %d semitone%s", clip->trans > 0 ? "up" : "down",
      abs(clip->trans), abs(clip->trans) == 1 ? "" : "s");
  snprintf(buf, sizeof buf, "Voice %d, %s: pattern %02X, %d rows%s%s%s",
    selchn + 1, at, clip->patt, cliprows(clip), trans,
    (clip->flags & CLIP_LOOPSTART) ? ", the voice repeats from here" : "",
    uses > 1 ? ". Its pattern plays in other places too, and changing its notes changes them all" : "");
  sm_setstatus(buf);
}

static void select_(int c, int i)
{
  selchn = (i >= 0) ? c : -1;
  selclip = i;
  describeclip();
  updateactions();
  gtk_widget_queue_draw(timeline);
}

//
// Editing
//

static CLIP work[MAX_CLIPS];

// A clip as newly placed: not part of a repeat, no loop start
static CLIP freshclip(const CLIP *clip)
{
  CLIP copy = *clip;

  copy.orderpos = -1;
  copy.rep = 0;
  copy.flags = 0;
  return copy;
}

// Write a voice's clips back. On failure the orderlist is left as it was.
static int commit(int c, const CLIP *list, int n)
{
  int ok;

  host_lock();
  ok = arr_write(sm_subtune(), c, list, n);
  host_unlock();
  if (!ok) sm_toast(n ? "That voice's arrangement is full" : "Each voice needs at least one clip");
  return ok;
}

static void edited(int c, int i)
{
  sm_edited();
  arrange_refresh();
  if ((c >= 0) && (i >= 0) && (i < nclips[c])) select_(c, i);
  else describeclip();
}

// Remove clip i from a list, passing a loop start on to the clip that
// takes its place
static int removeclip(CLIP *list, int n, int i)
{
  int loop = list[i].flags & CLIP_LOOPSTART;

  memmove(&list[i], &list[i + 1], (n - i - 1) * sizeof(CLIP));
  n--;
  if ((loop) && (n)) list[i < n ? i : n - 1].flags |= CLIP_LOOPSTART;
  return n;
}

static int insertclip(CLIP *list, int n, int i, CLIP clip)
{
  if (n >= MAX_CLIPS) return n;
  memmove(&list[i + 1], &list[i], (n - i) * sizeof(CLIP));
  list[i] = clip;
  return n + 1;
}

// Move (or copy) clip i of voice c to before index j of voice c2
static void moveclip(int c, int i, int c2, int j, int copy)
{
  CLIP clip = freshclip(&clips[c][i]);
  int n;

  if ((c == c2) && (!copy))
  {
    if ((j == i) || (j == i + 1)) return;
    memcpy(work, clips[c], nclips[c] * sizeof(CLIP));
    n = removeclip(work, nclips[c], i);
    if (j > i) j--;
    n = insertclip(work, n, j, clip);
    if (commit(c, work, n)) edited(c, j);
    return;
  }
  if (copy)
  {
    memcpy(work, clips[c2], nclips[c2] * sizeof(CLIP));
    n = insertclip(work, nclips[c2], j, clip);
    if (commit(c2, work, n)) edited(c2, j);
    return;
  }
  // To another voice: take it out of this one first
  if (nclips[c] < 2)
  {
    sm_toast("Each voice needs at least one clip");
    return;
  }
  {
    unsigned char saved[MAX_SONGLEN + 2];
    int savedlen = songlen[sm_subtune()][c];

    memcpy(saved, songorder[sm_subtune()][c], sizeof saved);
    memcpy(work, clips[c], nclips[c] * sizeof(CLIP));
    n = removeclip(work, nclips[c], i);
    if (!commit(c, work, n)) return;
    memcpy(work, clips[c2], nclips[c2] * sizeof(CLIP));
    n = insertclip(work, nclips[c2], j, clip);
    if (!commit(c2, work, n))
    {
      host_lock();
      memcpy(songorder[sm_subtune()][c], saved, sizeof saved);
      songlen[sm_subtune()][c] = savedlen;
      host_unlock();
      return;
    }
    edited(c2, j);
  }
}

static void deleteclip(void)
{
  int n;

  if (selclip < 0) return;
  if (nclips[selchn] < 2)
  {
    sm_toast("Each voice needs at least one clip");
    return;
  }
  memcpy(work, clips[selchn], nclips[selchn] * sizeof(CLIP));
  n = removeclip(work, nclips[selchn], selclip);
  if (commit(selchn, work, n)) edited(selchn, selclip < n ? selclip : n - 1);
}

// Linked copy: plays the same pattern
static void duplicate(void)
{
  if (selclip >= 0) moveclip(selchn, selclip, selchn, selclip + 1, 1);
}

// Insert a clip with its own pattern after the selection
static void insertpatternclip(int patt, int trans)
{
  CLIP clip = {0};
  int n;

  clip.patt = patt;
  clip.trans = trans;
  clip.orderpos = -1;
  memcpy(work, clips[selchn], nclips[selchn] * sizeof(CLIP));
  n = insertclip(work, nclips[selchn], selclip + 1, clip);
  if (commit(selchn, work, n)) edited(selchn, selclip + 1);
}

static void duplicatenew(void)
{
  int p;

  if (selclip < 0) return;
  if ((p = arr_copypattern(clips[selchn][selclip].patt)) < 0)
  {
    sm_toast("There is no room for another pattern");
    return;
  }
  insertpatternclip(p, clips[selchn][selclip].trans);
}

static void insertempty(void)
{
  int p;

  if (selclip < 0) return;
  if ((p = arr_newpattern(cliprows(&clips[selchn][selclip]))) < 0)
  {
    sm_toast("There is no room for another pattern");
    return;
  }
  insertpatternclip(p, 0);
}

// Give this clip a pattern of its own, so editing it leaves the other
// places that played the pattern alone
static void makeunique(void)
{
  int p;

  if (selclip < 0) return;
  if ((p = arr_copypattern(clips[selchn][selclip].patt)) < 0)
  {
    sm_toast("There is no room for another pattern");
    return;
  }
  memcpy(work, clips[selchn], nclips[selchn] * sizeof(CLIP));
  work[selclip].patt = p;
  work[selclip].orderpos = -1;
  work[selclip].rep = 0;
  if (commit(selchn, work, nclips[selchn])) edited(selchn, selclip);
}

static void transpose(int semitones)
{
  int t;

  if (selclip < 0) return;
  t = clips[selchn][selclip].trans + semitones;
  if ((t < MINTRANS) || (t > MAXTRANS))
  {
    sm_toast("Clips can be transposed from 16 semitones down to 14 up");
    return;
  }
  memcpy(work, clips[selchn], nclips[selchn] * sizeof(CLIP));
  work[selclip].trans = t;
  // A transposed pass of a repeat is a clip of its own
  work[selclip].orderpos = -1;
  work[selclip].rep = 0;
  if (selclip + 1 < nclips[selchn]) work[selclip + 1].orderpos = -1;
  if (commit(selchn, work, nclips[selchn])) edited(selchn, selclip);
}

static void setloop(void)
{
  int i;

  if (selclip < 0) return;
  memcpy(work, clips[selchn], nclips[selchn] * sizeof(CLIP));
  for (i = 0; i < nclips[selchn]; i++) work[i].flags &= ~CLIP_LOOPSTART;
  work[selclip].flags |= CLIP_LOOPSTART;
  if (commit(selchn, work, nclips[selchn])) edited(selchn, selclip);
}

static void playhere(void)
{
  if (selclip < 0) return;
  if (!host_playfrom(sm_subtune(), selchn, clips[selchn][selclip].orderpos))
    sm_toast("That part of the song isn't reached when it plays");
}

//
// Menu and keys
//

static void onaction(GSimpleAction *action, GVariant *parameter, gpointer data)
{
  const char *name = g_action_get_name(G_ACTION(action));

  if (!strcmp(name, "play")) playhere();
  else if (!strcmp(name, "duplicate")) duplicate();
  else if (!strcmp(name, "duplicate-new")) duplicatenew();
  else if (!strcmp(name, "insert-empty")) insertempty();
  else if (!strcmp(name, "unique")) makeunique();
  else if (!strcmp(name, "up")) transpose(1);
  else if (!strcmp(name, "down")) transpose(-1);
  else if (!strcmp(name, "octave-up")) transpose(12);
  else if (!strcmp(name, "octave-down")) transpose(-12);
  else if (!strcmp(name, "loop")) setloop();
  else if (!strcmp(name, "delete")) deleteclip();
}

static const char *actionnames[] = {"play", "duplicate", "duplicate-new", "insert-empty", "unique", "up", "down",
  "octave-up", "octave-down", "loop", "delete"};

static void updateactions(void)
{
  int i, sel = selclip >= 0;

  if (!actions) return;
  for (i = 0; i < (int)G_N_ELEMENTS(actionnames); i++)
  {
    GAction *a = g_action_map_lookup_action(G_ACTION_MAP(actions), actionnames[i]);
    int on = sel;
    if ((sel) && (!strcmp(actionnames[i], "unique"))) on = arr_patternuses(clips[selchn][selclip].patt) > 1;
    if ((sel) && (!strcmp(actionnames[i], "delete"))) on = nclips[selchn] > 1;
    if ((sel) && (!strcmp(actionnames[i], "loop"))) on = !(clips[selchn][selclip].flags & CLIP_LOOPSTART);
    g_simple_action_set_enabled(G_SIMPLE_ACTION(a), on);
  }
}

static GMenuModel *buildmenu(void)
{
  GMenu *m = g_menu_new(), *s;

  s = g_menu_new();
  g_menu_append(s, "Play from Here", "clip.play");
  g_menu_append_section(m, NULL, G_MENU_MODEL(s));
  g_object_unref(s);
  s = g_menu_new();
  g_menu_append(s, "Duplicate", "clip.duplicate");
  g_menu_append(s, "Duplicate as New Pattern", "clip.duplicate-new");
  g_menu_append(s, "Insert Empty Clip After", "clip.insert-empty");
  g_menu_append(s, "Make Pattern Unique", "clip.unique");
  g_menu_append_section(m, NULL, G_MENU_MODEL(s));
  g_object_unref(s);
  s = g_menu_new();
  g_menu_append(s, "Transpose Up", "clip.up");
  g_menu_append(s, "Transpose Down", "clip.down");
  g_menu_append(s, "Octave Up", "clip.octave-up");
  g_menu_append(s, "Octave Down", "clip.octave-down");
  g_menu_append_section(m, NULL, G_MENU_MODEL(s));
  g_object_unref(s);
  s = g_menu_new();
  g_menu_append(s, "Voice Repeats from Here", "clip.loop");
  g_menu_append(s, "Delete", "clip.delete");
  g_menu_append_section(m, NULL, G_MENU_MODEL(s));
  g_object_unref(s);
  return G_MENU_MODEL(m);
}

static void ontimelinepressed(GtkGestureClick *gesture, int n, double x, double y, gpointer data)
{
  int c = -1, i = hitclip(x, y, &c);
  int button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));

  gtk_widget_grab_focus(timeline);
  select_(c, i);
  if ((button == GDK_BUTTON_SECONDARY) && (i >= 0))
  {
    GdkRectangle where = {(int)x, (int)y, 1, 1};
    gtk_popover_set_pointing_to(GTK_POPOVER(menu), &where);
    gtk_popover_popup(GTK_POPOVER(menu));
  }
  else if ((n == 2) && (i >= 0)) playhere();
}

static void ondragbegin(GtkGestureDrag *gesture, double x, double y, gpointer data)
{
  int c = -1, i = hitclip(x, y, &c);

  dragging = 0;
  dragchn = (i >= 0) ? c : -1;
  dragclip = i;
  if (i >= 0) dragoffset = x - clipstart[c][i] * rowpx;
}

// The insertion point nearest the pointer
static void finddrop(double x, double y)
{
  int c = lanefromy(y), i, best = 0;
  double row = (x - dragoffset) / rowpx, bestd = 1e9;

  droplane = c;
  for (i = 0; i <= nclips[c]; i++)
  {
    int b = (i < nclips[c]) ? clipstart[c][i] : (nclips[c] ? clipend(c, nclips[c] - 1) : 0);
    double d = fabs(b - row);
    if (d < bestd)
    {
      bestd = d;
      best = i;
    }
  }
  dropindex = best;
}

static void ondragupdate(GtkGestureDrag *gesture, double dx, double dy, gpointer data)
{
  double sx, sy;

  if (dragchn < 0) return;
  if ((!dragging) && (fabs(dx) + fabs(dy) < DRAGSTART)) return;
  dragging = 1;
  gtk_gesture_drag_get_start_point(gesture, &sx, &sy);
  dragx = sx + dx;
  dragy = sy + dy;
  finddrop(dragx, dragy);
  gtk_widget_queue_draw(timeline);
}

static void ondragend(GtkGestureDrag *gesture, double dx, double dy, gpointer data)
{
  GdkModifierType mods = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(gesture));
  int was = dragging;

  dragging = 0;
  if ((was) && (dragchn >= 0)) moveclip(dragchn, dragclip, droplane, dropindex, (mods & GDK_CONTROL_MASK) != 0);
  dragchn = -1;
  gtk_widget_queue_draw(timeline);
}

static void selectnear(int c, int row)
{
  int i;

  for (i = 0; i < nclips[c]; i++)
    if (row < clipend(c, i)) break;
  if (i >= nclips[c]) i = nclips[c] - 1;
  select_(c, i);
}

static void scrolltoselection(void)
{
  GtkAdjustment *adj = gtk_scrolled_window_get_hadjustment(scroller);
  double x0, x1, left, page;

  if (selclip < 0) return;
  x0 = clipstart[selchn][selclip] * rowpx;
  x1 = clipend(selchn, selclip) * rowpx;
  left = gtk_adjustment_get_value(adj);
  page = gtk_adjustment_get_page_size(adj);
  if (x0 < left) gtk_adjustment_set_value(adj, x0 - 16);
  else if (x1 > left + page) gtk_adjustment_set_value(adj, x1 - page + 16 > x0 - 16 ? x0 - 16 : x1 - page + 16);
}

static gboolean onkey(GtkEventControllerKey *controller, guint keyval, guint keycode, GdkModifierType state,
  gpointer data)
{
  int shift = (state & GDK_SHIFT_MASK) != 0, ctrl = (state & GDK_CONTROL_MASK) != 0;

  if (selclip < 0)
  {
    if ((keyval == GDK_KEY_Left) || (keyval == GDK_KEY_Right) || (keyval == GDK_KEY_Home))
    {
      select_(0, 0);
      return TRUE;
    }
    return FALSE;
  }
  switch (keyval)
  {
    case GDK_KEY_Left:
    if (selclip > 0) select_(selchn, selclip - 1);
    break;
    case GDK_KEY_Right:
    if (selclip + 1 < nclips[selchn]) select_(selchn, selclip + 1);
    break;
    case GDK_KEY_Home: select_(selchn, 0); break;
    case GDK_KEY_End: select_(selchn, nclips[selchn] - 1); break;
    case GDK_KEY_Up:
    if (shift) transpose(ctrl ? 12 : 1);
    else if (selchn > 0) selectnear(selchn - 1, clipstart[selchn][selclip]);
    break;
    case GDK_KEY_Down:
    if (shift) transpose(ctrl ? -12 : -1);
    else if (selchn < MAX_CHN - 1) selectnear(selchn + 1, clipstart[selchn][selclip]);
    break;
    case GDK_KEY_Delete:
    case GDK_KEY_BackSpace: deleteclip(); break;
    case GDK_KEY_d:
    case GDK_KEY_D:
    if (!ctrl) return FALSE;
    if (shift) duplicatenew();
    else duplicate();
    break;
    case GDK_KEY_Return:
    case GDK_KEY_KP_Enter: playhere(); break;
    case GDK_KEY_Menu:
    {
      GdkRectangle where = {(int)(clipstart[selchn][selclip] * rowpx + 8), RULERH + selchn * LANEH + LANEH / 2, 1, 1};
      gtk_popover_set_pointing_to(GTK_POPOVER(menu), &where);
      gtk_popover_popup(GTK_POPOVER(menu));
    }
    break;
    default: return FALSE;
  }
  scrolltoselection();
  return TRUE;
}

static void onheaderpressed(GtkGestureClick *gesture, int n, double x, double y, gpointer data)
{
  int c = ((int)y - RULERH) / LANEH;

  if ((y < RULERH) || (c >= MAX_CHN)) return;
  mutechannel(c);
  gtk_widget_queue_draw(headers);
}

// The wheel scrolls along the song; Ctrl+wheel zooms around the pointer
static double pointerx;

static void onmotion(GtkEventControllerMotion *motion, double x, double y, gpointer data)
{
  pointerx = x;
}

static gboolean onscroll(GtkEventControllerScroll *controller, double dx, double dy, gpointer data)
{
  GtkAdjustment *adj = gtk_scrolled_window_get_hadjustment(scroller);
  GdkModifierType mods = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(controller));

  if (mods & GDK_CONTROL_MASK)
  {
    int old = rowpx;
    double row = pointerx / old;
    double screenx = pointerx - gtk_adjustment_get_value(adj);

    rowpx = (dy < 0) ? old * 2 : old / 2;
    if (rowpx < MINROWPX) rowpx = MINROWPX;
    if (rowpx > MAXROWPX) rowpx = MAXROWPX;
    if (rowpx == old) return TRUE;
    setsize();
    gtk_adjustment_set_upper(adj, (totalrows + 64) * rowpx);
    gtk_adjustment_set_value(adj, row * rowpx - screenx);
    gtk_widget_queue_draw(timeline);
    return TRUE;
  }
  gtk_adjustment_set_value(adj, gtk_adjustment_get_value(adj) + (dx + dy) * 48);
  return TRUE;
}

// Follow playback: find each voice's clip from the orderlist entry it last
// started, counting passes of repeated patterns
void arrange_tick(void)
{
  int c, changed = 0;

  if (!isplaying())
  {
    for (c = 0; c < MAX_CHN; c++)
    {
      if (playclip[c] >= 0) changed = 1;
      playclip[c] = -1;
      lastpos[c] = -1;
    }
    if (changed) gtk_widget_queue_draw(timeline);
    return;
  }
  for (c = 0; c < MAX_CHN; c++)
  {
    int i;

    if (seqcount[c] != lastcount[c])
    {
      int samepass = (seqpos[c] == lastpos[c]) && (playclip[c] >= 0);
      lastcount[c] = seqcount[c];
      lastpos[c] = seqpos[c];
      if ((samepass) && (playclip[c] + 1 < nclips[c]) && (clips[c][playclip[c] + 1].orderpos == seqpos[c]))
        playclip[c]++;
      else
      {
        playclip[c] = -1;
        for (i = 0; i < nclips[c]; i++)
          if (clips[c][i].orderpos == seqpos[c])
          {
            playclip[c] = i;
            break;
          }
      }
    }
    if ((playclip[c] >= 0) && (playclip[c] < nclips[c]))
    {
      int row = chn[c].pattptr / 4;
      if (row > cliprows(&clips[c][playclip[c]])) row = cliprows(&clips[c][playclip[c]]);
      playrow[c] = row;
    }
  }

  // Keep voice 1's playhead in view, unless a clip is being dragged
  if ((playclip[0] >= 0) && (playclip[0] < nclips[0]) && (!dragging))
  {
    GtkAdjustment *adj = gtk_scrolled_window_get_hadjustment(scroller);
    double x = (clipstart[0][playclip[0]] + playrow[0]) * rowpx;
    double left = gtk_adjustment_get_value(adj);
    double page = gtk_adjustment_get_page_size(adj);
    if ((x < left) || (x > left + page - 32)) gtk_adjustment_set_value(adj, x - 32);
  }
  gtk_widget_queue_draw(timeline);
}

GtkWidget *arrange_new(void)
{
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  GtkGesture *gesture;
  GtkEventController *controller;
  GMenuModel *model;
  int i;

  headers = gtk_drawing_area_new();
  gtk_widget_set_size_request(headers, HEADERW, RULERH + MAX_CHN * LANEH);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(headers), drawheaders, NULL, NULL);
  gtk_widget_set_tooltip_text(headers, "Click a voice to mute or unmute it");
  gesture = gtk_gesture_click_new();
  g_signal_connect(gesture, "pressed", G_CALLBACK(onheaderpressed), NULL);
  gtk_widget_add_controller(headers, GTK_EVENT_CONTROLLER(gesture));
  gtk_box_append(GTK_BOX(box), headers);
  gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_VERTICAL));

  timeline = gtk_drawing_area_new();
  gtk_widget_set_focusable(timeline, TRUE);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(timeline), drawtimeline, NULL, NULL);
  gesture = gtk_gesture_click_new();
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), 0);
  g_signal_connect(gesture, "pressed", G_CALLBACK(ontimelinepressed), NULL);
  gtk_widget_add_controller(timeline, GTK_EVENT_CONTROLLER(gesture));
  gesture = gtk_gesture_drag_new();
  g_signal_connect(gesture, "drag-begin", G_CALLBACK(ondragbegin), NULL);
  g_signal_connect(gesture, "drag-update", G_CALLBACK(ondragupdate), NULL);
  g_signal_connect(gesture, "drag-end", G_CALLBACK(ondragend), NULL);
  gtk_widget_add_controller(timeline, GTK_EVENT_CONTROLLER(gesture));
  controller = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
  g_signal_connect(controller, "scroll", G_CALLBACK(onscroll), NULL);
  gtk_widget_add_controller(timeline, controller);
  controller = gtk_event_controller_motion_new();
  g_signal_connect(controller, "motion", G_CALLBACK(onmotion), NULL);
  gtk_widget_add_controller(timeline, controller);
  controller = gtk_event_controller_key_new();
  g_signal_connect(controller, "key-pressed", G_CALLBACK(onkey), NULL);
  gtk_widget_add_controller(timeline, controller);

  actions = g_simple_action_group_new();
  for (i = 0; i < (int)G_N_ELEMENTS(actionnames); i++)
  {
    GSimpleAction *a = g_simple_action_new(actionnames[i], NULL);
    g_signal_connect(a, "activate", G_CALLBACK(onaction), NULL);
    g_action_map_add_action(G_ACTION_MAP(actions), G_ACTION(a));
    g_object_unref(a);
  }
  gtk_widget_insert_action_group(timeline, "clip", G_ACTION_GROUP(actions));
  model = buildmenu();
  menu = gtk_popover_menu_new_from_model(model);
  g_object_unref(model);
  gtk_widget_set_parent(menu, timeline);
  gtk_popover_set_has_arrow(GTK_POPOVER(menu), FALSE);
  gtk_widget_set_halign(menu, GTK_ALIGN_START);

  scroller = GTK_SCROLLED_WINDOW(gtk_scrolled_window_new());
  gtk_scrolled_window_set_policy(scroller, GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
  gtk_scrolled_window_set_child(scroller, timeline);
  gtk_widget_set_hexpand(GTK_WIDGET(scroller), TRUE);
  gtk_box_append(GTK_BOX(box), GTK_WIDGET(scroller));

  arrange_songchanged();
  return box;
}
