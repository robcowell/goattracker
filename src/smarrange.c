//
// SidMonkey: the arrangement, three voice lanes on a timeline
//
// Each pattern a voice's orderlist plays is a clip, laid out in rows from
// the start of the song. Repeats show as one clip per pass, transposes as a
// label on the clip. The layout follows the playroutine's sequencer
// (gplay.c sequencer()), so the playhead lines up with what is heard.
//

#include "sm.h"

#define MAX_CLIPS (MAX_SONGLEN * 16)
#define RULERH 24
#define LANEH 76
#define HEADERW 112
#define MINROWPX 1
#define MAXROWPX 24

typedef struct
{
  int orderpos;   // orderlist index of the pattern number
  int patt;
  int trans;      // semitones
  int rep;        // pass of a repeated pattern (0 = first)
  int start;      // first row on the timeline
  int rows;
} CLIP;

static CLIP clips[MAX_CHN][MAX_CLIPS];
static int nclips[MAX_CHN];
static int looprow[MAX_CHN];     // where the voice restarts after its last clip
static int totalrows;
static int rowpx = 4;

static int selchn = -1, selclip = -1;

// Playhead: the clip and row each voice is playing
static int playclip[MAX_CHN];
static int playrow[MAX_CHN];
static unsigned lastcount[MAX_CHN];
static int lastpos[MAX_CHN];

static GtkWidget *timeline, *headers;
static GtkScrolledWindow *scroller;

static const double palette[][3] = {
  {0.55, 0.75, 0.98}, {0.56, 0.86, 0.62}, {0.98, 0.78, 0.45}, {0.93, 0.56, 0.60},
  {0.76, 0.64, 0.95}, {0.48, 0.86, 0.86}, {0.95, 0.66, 0.85}, {0.80, 0.84, 0.52}};

static void layout(void)
{
  int s = sm_subtune();
  int c;

  totalrows = 0;
  for (c = 0; c < MAX_CHN; c++)
  {
    int len = songlen[s][c];
    int pos = 0, trans = 0, rows = 0, n = 0;
    int restart = songorder[s][c][len + 1];

    looprow[c] = -1;
    while ((pos < len) && (n < MAX_CLIPS))
    {
      unsigned char v = songorder[s][c][pos];
      int repeat = 0, r;

      if ((v >= TRANSDOWN) && (v < LOOPSONG))
      {
        trans = (signed char)(v - TRANSUP);
        if (++pos >= len) break;
        v = songorder[s][c][pos];
      }
      if ((v >= REPEAT) && (v < TRANSDOWN))
      {
        repeat = v - REPEAT;
        if (++pos >= len) break;
        v = songorder[s][c][pos];
      }
      if (v >= MAX_PATT)
      {
        pos++;
        continue;
      }
      for (r = 0; (r <= repeat) && (n < MAX_CLIPS); r++)
      {
        CLIP *clip = &clips[c][n++];
        clip->orderpos = pos;
        clip->patt = v;
        clip->trans = trans;
        clip->rep = r;
        clip->start = rows;
        clip->rows = pattlen[v] > 0 ? pattlen[v] : 1;
        rows += clip->rows;
        if ((looprow[c] < 0) && (pos >= restart)) looprow[c] = clip->start;
      }
      pos++;
    }
    nclips[c] = n;
    if (rows > totalrows) totalrows = rows;
  }
}

static void setsize(void)
{
  gtk_widget_set_size_request(timeline, (totalrows + 64) * rowpx, RULERH + MAX_CHN * LANEH);
}

void arrange_songchanged(void)
{
  int c;

  layout();
  selchn = selclip = -1;
  for (c = 0; c < MAX_CHN; c++)
  {
    playclip[c] = -1;
    lastcount[c] = seqcount[c];
    lastpos[c] = -1;
  }
  setsize();
  gtk_widget_queue_draw(timeline);
  gtk_widget_queue_draw(headers);
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
static void drawnotes(cairo_t *cr, const CLIP *clip, double x, double y, double h)
{
  unsigned char *p = pattern[clip->patt];
  int lo = 255, hi = -1, row, start = -1, note = 0;

  for (row = 0; row < clip->rows; row++)
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
  for (row = 0; row <= clip->rows; row++)
  {
    int n = (row < clip->rows) ? p[row * 4] : KEYOFF;
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

    for (i = 0; i < nclips[c]; i++)
    {
      const CLIP *clip = &clips[c][i];
      const double *col = palette[clip->patt % 8];
      double x = clip->start * rowpx + 1, w = clip->rows * rowpx - 2;
      double r = w > 12 ? 5 : 1;

      if (w < 1) w = 1;
      cairo_new_sub_path(cr);
      cairo_arc(cr, x + w - r, y + r, r, -G_PI / 2, 0);
      cairo_arc(cr, x + w - r, y + h - r, r, 0, G_PI / 2);
      cairo_arc(cr, x + r, y + h - r, r, G_PI / 2, G_PI);
      cairo_arc(cr, x + r, y + r, r, G_PI, 3 * G_PI / 2);
      cairo_close_path(cr);
      cairo_set_source_rgba(cr, col[0], col[1], col[2], ((c == selchn) && (i == selclip)) ? 1.0 : 0.78);
      cairo_fill_preserve(cr);
      if ((c == selchn) && (i == selclip))
      {
        cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.9);
        cairo_set_line_width(cr, 2);
        cairo_stroke(cr);
      }
      else cairo_new_path(cr);

      cairo_save(cr);
      cairo_rectangle(cr, x, y, w, h);
      cairo_clip(cr);
      cairo_set_source_rgba(cr, 0.1, 0.1, 0.15, 0.55);
      drawnotes(cr, clip, x, y + font * 1.8, h - font * 1.8);
      if (w > 24)
      {
        if (clip->trans) snprintf(buf, sizeof buf, "%02X %+d", clip->patt, clip->trans);
        else snprintf(buf, sizeof buf, "%02X", clip->patt);
        cairo_set_source_rgba(cr, 0.1, 0.1, 0.15, 0.9);
        text(cr, pl, x + 5, y + 2, buf);
      }
      cairo_restore(cr);
    }

    // Where the voice goes after its last clip
    if (nclips[c])
    {
      int end = clips[c][nclips[c] - 1].start + clips[c][nclips[c] - 1].rows;
      if (looprow[c] >= 0) snprintf(buf, sizeof buf, "↺ %s", "repeat");
      else snprintf(buf, sizeof buf, "end");
      cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.6);
      text(cr, pl, end * rowpx + 6, y + h / 2 - font, buf);
    }
  }

  // Playheads, one per voice: voices can drift apart with their own tempos
  if (isplaying())
  {
    for (c = 0; c < MAX_CHN; c++)
    {
      if (playclip[c] < 0) continue;
      cairo_set_source_rgba(cr, 0.90, 0.20, 0.25, 0.95);
      cairo_rectangle(cr, (clips[c][playclip[c]].start + playrow[c]) * rowpx - 1, RULERH + c * LANEH, 2, LANEH);
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

static int hitclip(double x, double y, int *chnum)
{
  int c = ((int)y - RULERH) / LANEH;
  int row = (int)(x / rowpx), i;

  if ((y < RULERH) || (c < 0) || (c >= MAX_CHN)) return -1;
  *chnum = c;
  for (i = 0; i < nclips[c]; i++)
    if ((row >= clips[c][i].start) && (row < clips[c][i].start + clips[c][i].rows)) return i;
  return -1;
}

static void describeclip(void)
{
  char buf[256], at[48];
  const CLIP *clip;
  int uses = 0, c, i;

  if (selclip < 0)
  {
    sm_setstatus("");
    return;
  }
  clip = &clips[selchn][selclip];
  for (c = 0; c < MAX_CHN; c++)
    for (i = 0; i < nclips[c]; i++)
      if ((clips[c][i].patt == clip->patt) && (!clips[c][i].rep)) uses++;
  barname(clip->start, at, sizeof at);
  snprintf(buf, sizeof buf, "Voice %d, %s: pattern %02X, %d rows%s%s%s. Double-click to play from here.",
    selchn + 1, at, clip->patt, clip->rows,
    clip->trans ? (clip->trans > 0 ? ", transposed up" : ", transposed down") : "",
    clip->rep ? ", repeated" : "",
    uses > 1 ? ", also used elsewhere (editing one changes all)" : "");
  sm_setstatus(buf);
}

static void ontimelinepressed(GtkGestureClick *gesture, int n, double x, double y, gpointer data)
{
  int c = -1, i = hitclip(x, y, &c);

  selchn = (i >= 0) ? c : -1;
  selclip = i;
  describeclip();
  gtk_widget_queue_draw(timeline);
  if ((n == 2) && (i >= 0))
  {
    if (!host_playfrom(sm_subtune(), c, clips[c][i].orderpos))
      sm_toast("That part of the song isn't reached when it plays");
  }
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
    if (playclip[c] >= 0)
    {
      int row = chn[c].pattptr / 4;
      if (row > clips[c][playclip[c]].rows) row = clips[c][playclip[c]].rows;
      playrow[c] = row;
    }
  }

  // Keep voice 1's playhead in view
  if (playclip[0] >= 0)
  {
    GtkAdjustment *adj = gtk_scrolled_window_get_hadjustment(scroller);
    double x = (clips[0][playclip[0]].start + playrow[0]) * rowpx;
    double left = gtk_adjustment_get_value(adj);
    double page = gtk_adjustment_get_page_size(adj);
    if ((x < left) || (x > left + page - 32)) gtk_adjustment_set_value(adj, x - 32);
  }
  gtk_widget_queue_draw(timeline);
}

GtkWidget *arrange_new(void)
{
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  GtkGesture *click;
  GtkEventController *controller;

  headers = gtk_drawing_area_new();
  gtk_widget_set_size_request(headers, HEADERW, RULERH + MAX_CHN * LANEH);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(headers), drawheaders, NULL, NULL);
  gtk_widget_set_tooltip_text(headers, "Click a voice to mute or unmute it");
  click = gtk_gesture_click_new();
  g_signal_connect(click, "pressed", G_CALLBACK(onheaderpressed), NULL);
  gtk_widget_add_controller(headers, GTK_EVENT_CONTROLLER(click));
  gtk_box_append(GTK_BOX(box), headers);
  gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_VERTICAL));

  timeline = gtk_drawing_area_new();
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(timeline), drawtimeline, NULL, NULL);
  click = gtk_gesture_click_new();
  g_signal_connect(click, "pressed", G_CALLBACK(ontimelinepressed), NULL);
  gtk_widget_add_controller(timeline, GTK_EVENT_CONTROLLER(click));
  controller = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
  g_signal_connect(controller, "scroll", G_CALLBACK(onscroll), NULL);
  gtk_widget_add_controller(timeline, controller);
  controller = gtk_event_controller_motion_new();
  g_signal_connect(controller, "motion", G_CALLBACK(onmotion), NULL);
  gtk_widget_add_controller(timeline, controller);

  scroller = GTK_SCROLLED_WINDOW(gtk_scrolled_window_new());
  gtk_scrolled_window_set_policy(scroller, GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
  gtk_scrolled_window_set_child(scroller, timeline);
  gtk_widget_set_hexpand(GTK_WIDGET(scroller), TRUE);
  gtk_box_append(GTK_BOX(box), GTK_WIDGET(scroller));

  arrange_songchanged();
  return box;
}
