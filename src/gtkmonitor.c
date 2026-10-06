//
// GOATTRACKER v2 GTK user interface: live views of the SID
//
// A piano keyboard showing the note each channel is sounding, and a panel of
// the SID's voice and filter registers. Both read the register shadow
// sidreg[] that the playroutine writes every frame, so they follow vibrato,
// portamento and test notes as well as the song.
//

#include <string.h>
#include "gtkui.h"

#define NUMKEYS (LASTNOTE - FIRSTNOTE + 1)

int settings_showpiano = 1;
int settings_showsidstate = 0;

static GtkWidget *piano;
static GtkWidget *sidview;
static GtkWidget *sidlabels[MAX_CHN + 1][7];
static int drawnnotes[MAX_CHN];
static unsigned char shownregs[NUMSIDREGS];
static int heldnote = -1;
static int pianoscale = 1;     // follows the grids' text size (View → Larger Text)

static const unsigned chncolors[MAX_CHN] = {0x7fb2ff, 0xffad5e, 0x58c27d};

static void setcolor(cairo_t *cr, unsigned rgb)
{
  cairo_set_source_rgb(cr, ((rgb >> 16) & 0xff) / 255.0, ((rgb >> 8) & 0xff) / 255.0, (rgb & 0xff) / 255.0);
}

static unsigned notefreq(int n)
{
  return freqtbllo[n] | (freqtblhi[n] << 8);
}

// The note nearest to what a channel is sounding, or -1 if it is silent
static int soundingnote(int c)
{
  unsigned freq = sidreg[7 * c] | (sidreg[7 * c + 1] << 8);
  unsigned char ctrl = sidreg[7 * c + 4];
  int n;

  if ((chn[c].mute) || (!(ctrl & 0x01)) || (!(ctrl & 0xf0)) || (ctrl & 0x08) || (!freq)) return -1;
  for (n = 0; (n < NUMKEYS - 1) && (notefreq(n + 1) <= freq); n++);
  // Between two notes, pick the nearer in pitch (compare with their
  // geometric mean)
  if ((n < NUMKEYS - 1) && (freq >= notefreq(n)) &&
    ((unsigned long long)freq * freq > (unsigned long long)notefreq(n) * notefreq(n + 1))) n++;
  return n;
}

//
// Piano keyboard
//

static int isblack(int n)
{
  int s = n % 12;
  return (s == 1) || (s == 3) || (s == 6) || (s == 8) || (s == 10);
}

// Left edge of a key, in white key widths
static double keyx(int n)
{
  static const double offsets[12] = {0, 0.7, 1, 1.7, 2, 3, 3.7, 4, 4.7, 5, 5.7, 6};
  return (n / 12) * 7 + offsets[n % 12];
}

static void fillkey(cairo_t *cr, int n, double ww, double height, int black)
{
  double x = keyx(n) * ww;
  double w = black ? ww * 0.6 : ww;
  double h = black ? height * 0.6 : height;
  int c, count = 0, done = 0;

  for (c = 0; c < MAX_CHN; c++)
    if (drawnnotes[c] == n) count++;
  if (!count)
  {
    setcolor(cr, black ? 0x20232b : 0xc9ccd3);
    cairo_rectangle(cr, x + 0.5, 0, w - 1, h);
    cairo_fill(cr);
    return;
  }
  // One stripe per channel playing this key
  for (c = 0; c < MAX_CHN; c++)
  {
    if (drawnnotes[c] != n) continue;
    setcolor(cr, chncolors[c]);
    cairo_rectangle(cr, x + 0.5 + (w - 1) * done / count, 0, (w - 1) / count, h);
    cairo_fill(cr);
    done++;
  }
}

static int whitekeys(void)
{
  int n, count = 0;

  for (n = 0; n < NUMKEYS; n++)
    if (!isblack(n)) count++;
  return count;
}

static void drawpiano(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data)
{
  double ww = (double)width / whitekeys();
  PangoLayout *layout = gtk_widget_create_pango_layout(GTK_WIDGET(area), NULL);
  PangoFontDescription *font = pango_font_description_from_string("Monospace");
  int n;

  setcolor(cr, 0x14161b);
  cairo_paint(cr);
  for (n = 0; n < NUMKEYS; n++)
    if (!isblack(n)) fillkey(cr, n, ww, height, 0);
  pango_font_description_set_size(font, (7 + 2 * (pianoscale - 1)) * PANGO_SCALE);
  pango_layout_set_font_description(layout, font);
  for (n = 0; n < NUMKEYS; n += 12)
  {
    char buf[8];
    int tw, th;

    snprintf(buf, sizeof buf, "C%d", n / 12);
    pango_layout_set_text(layout, buf, -1);
    pango_layout_get_pixel_size(layout, &tw, &th);
    if (tw > ww) continue;
    setcolor(cr, 0x5b616e);
    cairo_move_to(cr, keyx(n) * ww + (ww - tw) / 2, height - th - 2);
    pango_cairo_show_layout(cr, layout);
  }
  for (n = 0; n < NUMKEYS; n++)
    if (isblack(n)) fillkey(cr, n, ww, height, 1);
  pango_font_description_free(font);
  g_object_unref(layout);
}

static int keyat(double x, double y, int width, int height)
{
  double ww = (double)width / whitekeys();
  int n;

  if (y < height * 0.6)
  {
    for (n = 0; n < NUMKEYS; n++)
      if ((isblack(n)) && (x >= keyx(n) * ww) && (x < (keyx(n) + 0.6) * ww)) return n;
  }
  for (n = 0; n < NUMKEYS; n++)
    if ((!isblack(n)) && (x >= keyx(n) * ww) && (x < (keyx(n) + 1) * ww)) return n;
  return -1;
}

static void onpianopressed(GtkGestureClick *gesture, int npress, double x, double y, gpointer data)
{
  int n = keyat(x, y, gtk_widget_get_width(piano), gtk_widget_get_height(piano));

  if (n < 0) return;
  heldnote = n;
  playtestnote(FIRSTNOTE + n, einum, epchn);
}

static void onpianoreleased(GtkGestureClick *gesture, int npress, double x, double y, gpointer data)
{
  if (heldnote < 0) return;
  heldnote = -1;
  releasenote(epchn);
}

static int pianoheight(void)
{
  return 44 + 10 * (pianoscale - 1);
}

void monitor_setscale(int scale)
{
  pianoscale = CLAMP(scale, 1, 4);
  if (!piano) return;
  gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(piano), pianoheight());
  gtk_widget_queue_draw(piano);
}

GtkWidget *monitor_piano_new(void)
{
  GtkGesture *gesture = gtk_gesture_click_new();
  int c;

  for (c = 0; c < MAX_CHN; c++) drawnnotes[c] = -1;
  piano = gtk_drawing_area_new();
  monitor_setscale(pianoscale);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(piano), drawpiano, NULL, NULL);
  gtk_widget_set_tooltip_text(piano, "The notes the channels are playing. Click a key to play it with the current instrument.");
  g_signal_connect(gesture, "pressed", G_CALLBACK(onpianopressed), NULL);
  g_signal_connect(gesture, "released", G_CALLBACK(onpianoreleased), NULL);
  gtk_widget_add_controller(piano, GTK_EVENT_CONTROLLER(gesture));
  gtk_widget_set_visible(piano, settings_showpiano);
  return piano;
}

//
// SID registers
//

static const char *colheads[7] = {"", "Note", "Waveform", "Attack/decay", "Sustain/release", "Pulse", "Frequency"};

static void waveformtext(unsigned char ctrl, char *buf, int size)
{
  static const char *waves[] = {"tri", "saw", "pulse", "noise"};
  static const char *bits[] = {"gate", "sync", "ring", "test"};
  int c, len = 0;

  buf[0] = 0;
  for (c = 0; c < 4; c++)
    if (ctrl & (0x10 << c)) len += snprintf(buf + len, size - len, "%s%s", len ? "+" : "", waves[c]);
  if (!len) len += snprintf(buf + len, size - len, "none");
  for (c = 0; c < 4; c++)
    if (ctrl & (1 << c)) len += snprintf(buf + len, size - len, " %s", bits[c]);
}

static void setlabel(GtkWidget *label, const char *text)
{
  if (strcmp(gtk_label_get_text(GTK_LABEL(label)), text)) gtk_label_set_text(GTK_LABEL(label), text);
}

static void updatesidview(void)
{
  static const char *types[8] = {"off", "lowpass", "bandpass", "low+band", "highpass", "notch", "band+high", "all"};
  char buf[64];
  int c;

  for (c = 0; c < MAX_CHN; c++)
  {
    unsigned char *r = &sidreg[7 * c];
    int n = soundingnote(c);

    setlabel(sidlabels[c][1], (n >= 0) ? notename[n] : (chn[c].mute ? "mute" : "..."));
    waveformtext(r[4], buf, sizeof buf);
    setlabel(sidlabels[c][2], buf);
    snprintf(buf, sizeof buf, "%X %X", r[5] >> 4, r[5] & 0xf);
    setlabel(sidlabels[c][3], buf);
    snprintf(buf, sizeof buf, "%X %X", r[6] >> 4, r[6] & 0xf);
    setlabel(sidlabels[c][4], buf);
    snprintf(buf, sizeof buf, "%03X", (r[2] | (r[3] << 8)) & 0xfff);
    setlabel(sidlabels[c][5], buf);
    snprintf(buf, sizeof buf, "%04X", r[0] | (r[1] << 8));
    setlabel(sidlabels[c][6], buf);
  }

  snprintf(buf, sizeof buf, "%s", types[(sidreg[0x18] >> 4) & 7]);
  setlabel(sidlabels[MAX_CHN][1], (sidreg[0x18] & 0x80) ? "3 off" : "");
  setlabel(sidlabels[MAX_CHN][2], buf);
  snprintf(buf, sizeof buf, "res %X", sidreg[0x17] >> 4);
  setlabel(sidlabels[MAX_CHN][3], buf);
  buf[0] = 0;
  for (c = 0; c < MAX_CHN; c++)
    if (sidreg[0x17] & (1 << c)) snprintf(buf + strlen(buf), sizeof buf - strlen(buf), "%s%d", buf[0] ? "+" : "ch ", c + 1);
  setlabel(sidlabels[MAX_CHN][4], buf[0] ? buf : "no channels");
  snprintf(buf, sizeof buf, "vol %X", sidreg[0x18] & 0xf);
  setlabel(sidlabels[MAX_CHN][5], buf);
  snprintf(buf, sizeof buf, "%03X", (sidreg[0x15] & 7) | (sidreg[0x16] << 3));
  setlabel(sidlabels[MAX_CHN][6], buf);
}

GtkWidget *monitor_sidview_new(void)
{
  GtkWidget *grid = gtk_grid_new();
  int r, c;

  sidview = grid;
  gtk_grid_set_column_spacing(GTK_GRID(grid), 18);
  gtk_widget_set_margin_start(grid, 10);
  gtk_widget_set_margin_end(grid, 10);
  gtk_widget_set_margin_top(grid, 4);
  gtk_widget_set_margin_bottom(grid, 4);
  gtk_widget_add_css_class(grid, "monospace");
  for (c = 0; c < 7; c++)
  {
    GtkWidget *l = gtk_label_new(colheads[c]);
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    gtk_widget_add_css_class(l, "dim-label");
    gtk_widget_add_css_class(l, "caption");
    gtk_grid_attach(GTK_GRID(grid), l, c, 0, 1, 1);
  }
  for (r = 0; r <= MAX_CHN; r++)
  {
    for (c = 0; c < 7; c++)
    {
      char buf[64];

      if (!c)
      {
        if (r < MAX_CHN) snprintf(buf, sizeof buf, "<span foreground=\"#%06x\">%d</span>", chncolors[r], r + 1);
        else snprintf(buf, sizeof buf, "Filter");
        sidlabels[r][c] = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(sidlabels[r][c]), buf);
      }
      else sidlabels[r][c] = gtk_label_new("");
      gtk_label_set_xalign(GTK_LABEL(sidlabels[r][c]), 0);
      // Keep the columns from jumping around as the text changes
      if (c == 2) gtk_label_set_width_chars(GTK_LABEL(sidlabels[r][c]), 22);
      gtk_grid_attach(GTK_GRID(grid), sidlabels[r][c], c, r + 1, 1, 1);
    }
  }
  gtk_widget_set_tooltip_text(grid, "The SID's registers as the playroutine last wrote them");
  gtk_widget_set_visible(grid, settings_showsidstate);
  updatesidview();
  return grid;
}

//
// Called from the UI tick: redraw whatever changed
//

void monitor_update(void)
{
  if ((piano) && (gtk_widget_get_visible(piano)))
  {
    int c, changed = 0;

    for (c = 0; c < MAX_CHN; c++)
    {
      int n = soundingnote(c);
      if (n != drawnnotes[c])
      {
        drawnnotes[c] = n;
        changed = 1;
      }
    }
    if (changed) gtk_widget_queue_draw(piano);
  }
  if ((sidview) && (gtk_widget_get_visible(sidview)) && (memcmp(shownregs, sidreg, sizeof shownregs)))
  {
    memcpy(shownregs, sidreg, sizeof shownregs);
    updatesidview();
  }
}

void monitor_setvisible(int showpiano, int showsidstate)
{
  settings_showpiano = showpiano;
  settings_showsidstate = showsidstate;
  if (piano) gtk_widget_set_visible(piano, showpiano);
  if (sidview) gtk_widget_set_visible(sidview, showsidstate);
}
