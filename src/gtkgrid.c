//
// GOATTRACKER v2 GTK user interface: pattern, orderlist and table editors
//
// Each editor is a GtkDrawingArea that draws the engine's state in tracker
// style with a monospace font. Clicking moves the engine's edit cursor;
// keyboard editing goes through the engine's command handlers (gtkui.c).
//

#include <string.h>
#include <math.h>
#include "gtkui.h"

GtkWidget *patterngrid = NULL;
GtkWidget *ordergrid = NULL;
GtkWidget *tablegrid = NULL;

typedef struct
{
  double r, g, b;
} RGB;

#define HEX(c) {((c) >> 16) / 255.0, (((c) >> 8) & 0xff) / 255.0, ((c) & 0xff) / 255.0}

static const RGB col_bg = HEX(0x14161b);
static const RGB col_bghighlight = HEX(0x1b1e25);
static const RGB col_header = HEX(0x20232b);
static const RGB col_headertext = HEX(0xc9ccd3);
static const RGB col_cursorrow = HEX(0x233148);
static const RGB col_playing = HEX(0x1c3a28);
static const RGB col_mark = HEX(0x3a2b55);
static const RGB col_cursor = HEX(0x4f7cc4);
static const RGB col_cursorunfocused = HEX(0x3a4e6c);
static const RGB col_rownum = HEX(0x5b616e);
static const RGB col_rownumhighlight = HEX(0xa9afba);
static const RGB col_note = HEX(0xe6e6e6);
static const RGB col_empty = HEX(0x434853);
static const RGB col_instr = HEX(0x7fb2ff);
static const RGB col_cmd = HEX(0xffad5e);
static const RGB col_data = HEX(0xf2d272);
static const RGB col_special = HEX(0xff7b72);
static const RGB col_separator = HEX(0x2a2e37);
static const RGB col_start = HEX(0x58c27d);

// Character cell size of the grid font
static int fontscale = 1;
static int cellw = 8;
static int cellh = 16;
static PangoFontDescription *fontdesc = NULL;

// Pattern layout, in characters
#define PATT_ROWNUMW 4
#define PATT_CHWIDTH 13
// Orderlist layout
#define ORD_ROWNUMW 4
#define ORD_CHWIDTH 5
// Table layout. With decoded tables the wave, pulse and filter tables get a
// column describing their rows.
#define TBL_WIDTH 10
#define TBL_DECODEW 15

static int decoded(int table)
{
  return (settings_decodetables) && (table != STBL);
}

static int tblwidth(int table)
{
  return TBL_WIDTH + (decoded(table) ? TBL_DECODEW : 0);
}

// Where table starts, in characters
static int tblstart(int table)
{
  int c, x = 0;

  for (c = 0; c < table; c++) x += tblwidth(c) + 1;
  return x;
}

static const char *tablenames[] = {"Wave", "Pulse", "Filter", "Speed"};

// View positions. While the keyboard or follow play moves the cursor, the
// pattern and orderlist keep it centred; scrolling, clicking and dragging
// with the mouse leave the view where it is.
static int patttop = 0, orderstop = 0;
static int pattcentred = 1, ordercentred = 1, tablefollow = 1;

void grid_followcursor(void)
{
  pattcentred = 1;
  ordercentred = 1;
  tablefollow = 1;
}

static void updatefont(GtkWidget *widget)
{
  PangoLayout *layout;
  int w, h;

  if (fontdesc) return;
  fontdesc = pango_font_description_from_string("Monospace");
  pango_font_description_set_size(fontdesc, (10 + 2 * (fontscale - 1)) * PANGO_SCALE);
  layout = gtk_widget_create_pango_layout(widget, "0");
  pango_layout_set_font_description(layout, fontdesc);
  pango_layout_get_pixel_size(layout, &w, &h);
  g_object_unref(layout);
  cellw = w;
  cellh = h + 2;
}

static int headerheight(void)
{
  return cellh + 8;
}

static void updatesizes(void)
{
  if (!patterngrid) return;
  updatefont(patterngrid);
  gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(patterngrid), (PATT_ROWNUMW + MAX_CHN * PATT_CHWIDTH + 1) * cellw);
  gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(ordergrid), (ORD_ROWNUMW + MAX_CHN * ORD_CHWIDTH + 1) * cellw);
  gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(tablegrid), (tblstart(MAX_TABLES) + 1) * cellw);
  gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(tablegrid), headerheight() + 8 * cellh);
}

void grid_relayout(void)
{
  updatesizes();
  grid_redraw();
}

void grid_setfontscale(int scale)
{
  fontscale = CLAMP(scale, 1, 4);
  if (fontdesc) pango_font_description_free(fontdesc);
  fontdesc = NULL;
  updatesizes();
  monitor_setscale(fontscale);
  grid_redraw();
}

void grid_redraw(void)
{
  if (patterngrid) gtk_widget_queue_draw(patterngrid);
  if (ordergrid) gtk_widget_queue_draw(ordergrid);
  if (tablegrid) gtk_widget_queue_draw(tablegrid);
}

static void fill(cairo_t *cr, double x, double y, double w, double h, RGB c)
{
  cairo_set_source_rgb(cr, c.r, c.g, c.b);
  cairo_rectangle(cr, x, y, w, h);
  cairo_fill(cr);
}

static RGB dim(RGB c)
{
  RGB d = {(c.r + col_bg.r) / 2, (c.g + col_bg.g) / 2, (c.b + col_bg.b) / 2};
  return d;
}

static void text(cairo_t *cr, PangoLayout *layout, double x, double y, RGB c, const char *s)
{
  cairo_set_source_rgb(cr, c.r, c.g, c.b);
  cairo_move_to(cr, x, y + 1);
  pango_layout_set_text(layout, s, -1);
  pango_cairo_show_layout(cr, layout);
}

static PangoLayout *newlayout(GtkWidget *widget)
{
  PangoLayout *layout;

  updatefont(widget);
  layout = gtk_widget_create_pango_layout(widget, NULL);
  pango_layout_set_font_description(layout, fontdesc);
  return layout;
}

static void drawheader(cairo_t *cr, int width, int focused)
{
  fill(cr, 0, 0, width, headerheight(), col_header);
  if (focused) fill(cr, 0, headerheight() - 2, width, 2, col_cursor);
}

static void drawcursor(cairo_t *cr, double x, double y, int chars, int focused)
{
  if (focused)
    fill(cr, x, y, chars * cellw, cellh, col_cursor);
  else
  {
    cairo_set_source_rgb(cr, col_cursorunfocused.r, col_cursorunfocused.g, col_cursorunfocused.b);
    cairo_set_line_width(cr, 1);
    cairo_rectangle(cr, x + 0.5, y + 0.5, chars * cellw - 1, cellh - 1);
    cairo_stroke(cr);
  }
}

static int inmark(int p, int start, int end)
{
  if (start <= end) return (p >= start) && (p <= end);
  return (p <= start) && (p >= end);
}

//
// Pattern editor
//

static int pattcolumnx(int column)
{
  // Character offset of epcolumn within a channel: note, instrument (2),
  // command, command data (2)
  static const int offset[] = {0, 4, 5, 7, 8, 9};
  return offset[column];
}

static void drawpattern(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data)
{
  PangoLayout *layout = newlayout(GTK_WIDGET(area));
  int focused = gtk_widget_has_focus(GTK_WIDGET(area));
  int top, rows, c, d, maxlen = 0;
  int x0 = cellw / 2;
  char buf[32];

  fill(cr, 0, 0, width, height, col_bg);

  // Keep the edit row in the middle, as in most trackers
  rows = (height - headerheight()) / cellh + 1;
  if (pattcentred) patttop = eppos - (height - headerheight()) / cellh / 2;
  top = patttop;
  for (c = 0; c < MAX_CHN; c++)
    if (pattlen[epnum[c]] > maxlen) maxlen = pattlen[epnum[c]];

  for (d = 0; d < rows; d++)
  {
    int p = top + d;
    int y = headerheight() + d * cellh;

    if ((p < 0) || (p > maxlen)) continue;

    if (p == eppos) fill(cr, 0, y, width, cellh, col_cursorrow);
    else if (!(p % stepsize)) fill(cr, 0, y, width, cellh, col_bghighlight);

    if (patterndispmode & 1) sprintf(buf, "%02X", p);
    else sprintf(buf, p < 100 ? "%02d" : "%03d", p);
    text(cr, layout, x0, y, (p % stepsize) ? col_rownum : col_rownumhighlight, buf);

    for (c = 0; c < MAX_CHN; c++)
    {
      unsigned char *row = &pattern[epnum[c]][p * 4];
      int x = x0 + (PATT_ROWNUMW + c * PATT_CHWIDTH) * cellw;
      int muted = chn[c].mute;
      RGB note, ins, cmd, cdata;

      if (p > pattlen[epnum[c]]) continue;

      if ((isplaying()) && (epnum[c] == chn[c].pattnum) && (p != eppos))
      {
        int chnrow = chn[c].pattptr / 4;
        if (chnrow > pattlen[chn[c].pattnum]) chnrow = pattlen[chn[c].pattnum];
        if (chnrow == p) fill(cr, x - cellw / 2, y, (PATT_CHWIDTH - 2) * cellw, cellh, col_playing);
      }
      if ((c == epmarkchn) && (inmark(p, epmarkstart, epmarkend)))
        fill(cr, x - cellw / 2, y, (PATT_CHWIDTH - 2) * cellw, cellh, col_mark);
      if ((p == eppos) && (c == epchn) && (editmode == EDIT_PATTERN))
        drawcursor(cr, x + pattcolumnx(epcolumn) * cellw, y, epcolumn ? 1 : 3, focused);

      if (row[0] == ENDPATT)
      {
        text(cr, layout, x, y, col_rownum, "-- end --");
        continue;
      }

      note = col_note;
      if (row[0] == REST) note = col_empty;
      if ((row[0] == KEYOFF) || (row[0] == KEYON)) note = col_special;
      ins = row[1] ? col_instr : col_empty;
      cmd = (row[2] || row[3]) ? col_cmd : col_empty;
      cdata = (row[2] || row[3]) ? col_data : col_empty;
      if (muted)
      {
        note = dim(note);
        ins = dim(ins);
        cmd = dim(cmd);
        cdata = dim(cdata);
      }

      text(cr, layout, x, y, note, notename[row[0] - FIRSTNOTE]);
      if ((!row[1]) && (patterndispmode & 2)) strcpy(buf, "..");
      else sprintf(buf, "%02X", row[1]);
      text(cr, layout, x + 4 * cellw, y, ins, buf);
      if ((!row[2]) && (!row[3]) && (patterndispmode & 2))
        text(cr, layout, x + 7 * cellw, y, cmd, "...");
      else
      {
        sprintf(buf, "%01X", row[2]);
        text(cr, layout, x + 7 * cellw, y, cmd, buf);
        sprintf(buf, "%02X", row[3]);
        text(cr, layout, x + 8 * cellw, y, cdata, buf);
      }
    }
  }

  // Channel separators and headers
  for (c = 0; c < MAX_CHN; c++)
  {
    int x = x0 + (PATT_ROWNUMW + c * PATT_CHWIDTH) * cellw;
    fill(cr, x - cellw, headerheight(), 1, height, col_separator);
  }
  drawheader(cr, width, focused);
  for (c = 0; c < MAX_CHN; c++)
  {
    int x = x0 + (PATT_ROWNUMW + c * PATT_CHWIDTH) * cellw;
    sprintf(buf, "%d", c + 1);
    text(cr, layout, x, 4, chn[c].mute ? col_special : col_headertext, chn[c].mute ? "Muted" : buf);
    sprintf(buf, "Patt %02X", epnum[c]);
    text(cr, layout, x + 3 * cellw, 4, chn[c].mute ? col_rownum : col_instr, buf);
  }
  g_object_unref(layout);
}

//
// Orderlist editor (positions run down, one column per channel)
//

static int ordermaxlen(void)
{
  int c, maxlen = 0;

  for (c = 0; c < MAX_CHN; c++)
    if (songlen[esnum][c] + 2 > maxlen) maxlen = songlen[esnum][c] + 2;
  return maxlen;
}

static void draworderlist(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data)
{
  PangoLayout *layout = newlayout(GTK_WIDGET(area));
  int focused = gtk_widget_has_focus(GTK_WIDGET(area));
  int x0 = cellw / 2;
  int rows, top, c, d;
  int maxlen = ordermaxlen();
  char buf[16];

  fill(cr, 0, 0, width, height, col_bg);

  rows = (height - headerheight()) / cellh + 1;
  if (ordercentred)
  {
    top = eseditpos - (height - headerheight()) / cellh / 2;
    if (top + rows > maxlen) top = maxlen - rows + 1;
  }
  else top = orderstop;
  if (top > maxlen - 1) top = maxlen - 1;
  if (top < 0) top = 0;
  orderstop = top;

  for (d = 0; d < rows; d++)
  {
    int p = top + d;
    int y = headerheight() + d * cellh;

    if (p >= maxlen) break;
    if (p == eseditpos) fill(cr, 0, y, width, cellh, col_cursorrow);
    sprintf(buf, "%02X", p);
    text(cr, layout, x0, y, col_rownum, buf);

    for (c = 0; c < MAX_CHN; c++)
    {
      int len = songlen[esnum][c];
      int x = x0 + (ORD_ROWNUMW + c * ORD_CHWIDTH) * cellw;
      unsigned char v = songorder[esnum][c][p];
      RGB color = col_note;

      if (p > len + 1) continue;

      if ((isplaying()) && (chn[c].advance) && (p == MAX(chn[c].songptr - 1, 0)) && (p != eseditpos))
        fill(cr, x - cellw / 2, y, (ORD_CHWIDTH - 1) * cellw, cellh, col_playing);
      if ((c == esmarkchn) && (inmark(p, esmarkstart, esmarkend)))
        fill(cr, x - cellw / 2, y, (ORD_CHWIDTH - 1) * cellw, cellh, col_mark);
      // Song start (SPACE) and end (BACKSPACE) positions for F2 playback
      if (p == espos[c]) fill(cr, x - cellw / 2, y + 1, 3, cellh - 2, col_start);
      if ((esend[c]) && (p == esend[c])) fill(cr, x + 3 * cellw - cellw / 2 - 3, y + 1, 3, cellh - 2, col_special);
      if ((c == eschn) && (p == eseditpos) && (editmode == EDIT_ORDERLIST))
        drawcursor(cr, x + escolumn * cellw, y, 1, focused);

      if (p == len)
      {
        strcpy(buf, "RST");
        color = col_special;
      }
      else if (p == len + 1)
      {
        // Restart position
        sprintf(buf, "%02X", v);
        color = col_rownumhighlight;
      }
      else if (v < REPEAT) sprintf(buf, "%02X", v);
      else
      {
        color = col_cmd;
        if (v >= TRANSUP) sprintf(buf, "+%01X", v & 0xf);
        else if (v >= TRANSDOWN) sprintf(buf, "-%01X", 16 - (v & 0x0f));
        else sprintf(buf, "R%01X", (v + 1) & 0x0f);
      }
      text(cr, layout, x, y, color, buf);
    }
  }

  drawheader(cr, width, focused);
  text(cr, layout, x0, 4, col_headertext, "Pos");
  for (c = 0; c < MAX_CHN; c++)
  {
    sprintf(buf, "%d", c + 1);
    text(cr, layout, x0 + (ORD_ROWNUMW + c * ORD_CHWIDTH) * cellw, 4, col_headertext, buf);
  }
  g_object_unref(layout);
}

//
// Table editor (wave, pulse, filter and speed tables side by side)
//

static int tablerows(int height)
{
  return MAX((height - headerheight()) / cellh, 1);
}

static void drawtables(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data)
{
  PangoLayout *layout = newlayout(GTK_WIDGET(area));
  int focused = gtk_widget_has_focus(GTK_WIDGET(area));
  int x0 = cellw / 2;
  int rows = tablerows(height);
  int c, d;
  char buf[64];

  // After keyboard movement, keep the edit row visible even when the pane
  // shows fewer rows than the engine assumes
  if (tablefollow)
  {
    if (etpos - etview[etnum] >= rows) etview[etnum] = etpos - rows + 1;
    if (etpos < etview[etnum]) etview[etnum] = etpos;
    tablefollow = 0;

    // ...and bring the edited table into view when the tables scroll sideways
    {
      GtkWidget *scroll = gtk_widget_get_ancestor(GTK_WIDGET(area), GTK_TYPE_SCROLLED_WINDOW);
      if ((scroll) && (editmode == EDIT_TABLES))
      {
        GtkAdjustment *adj = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(scroll));
        gtk_adjustment_clamp_page(adj, tblstart(etnum) * cellw, (tblstart(etnum) + tblwidth(etnum) + 1) * cellw);
      }
    }
  }

  fill(cr, 0, 0, width, height, col_bg);

  for (c = 0; c < MAX_TABLES; c++)
  {
    int x = x0 + tblstart(c) * cellw;
    int instrstart = instr[einum].ptr[c] - 1;

    if (c) fill(cr, x - cellw, headerheight(), 1, height, col_separator);

    for (d = 0; d < rows; d++)
    {
      int p = etview[c] + d;
      int y = headerheight() + d * cellh;
      unsigned char l, r;
      RGB lcol = col_note, rcol = col_data, dcol = col_rownumhighlight;
      int jump;

      if (p >= MAX_TABLELEN) break;
      l = ltable[c][p];
      r = rtable[c][p];

      if ((c == etnum) && (p == etpos))
        fill(cr, x - cellw / 2, y, tblwidth(c) * cellw, cellh, col_cursorrow);
      if ((c == etmarknum) && (inmark(p, etmarkstart, etmarkend)))
        fill(cr, x + 3 * cellw - cellw / 2, y, 6 * cellw, cellh, col_mark);
      // Where the current instrument's pointer into this table starts
      if ((einum) && (p == instrstart))
        fill(cr, x - cellw / 2, y + 1, 3, cellh - 2, col_start);
      if ((c == etnum) && (p == etpos) && (editmode == EDIT_TABLES))
        drawcursor(cr, x + ((etcolumn < 2) ? 3 + etcolumn : 6 + etcolumn - 2) * cellw, y, 1, focused);

      switch (c)
      {
        case WTBL:
        if (l >= WAVECMD) lcol = col_cmd;
        break;

        case PTBL:
        if (l >= 0x80) lcol = col_cmd;
        break;

        case FTBL:
        if ((l >= 0x80) || ((!l) && (r))) lcol = col_cmd;
        break;
      }
      jump = (c != STBL) && (l == 0xff);
      if (jump) lcol = dcol = col_special;
      if ((!l) && (!r))
      {
        lcol = col_empty;
        rcol = col_empty;
      }
      // Rows that nothing can ever execute are dimmed
      else if (!table_isreachable(c, p))
      {
        lcol = dim(lcol);
        rcol = dim(rcol);
        dcol = dim(dcol);
      }
      // A jump or stop ends a run of rows
      if (jump)
        fill(cr, x - cellw / 2, y + cellh - 1, tblwidth(c) * cellw, 1, col_rownum);

      sprintf(buf, "%02X", p + 1);
      text(cr, layout, x, y, col_rownum, buf);
      sprintf(buf, "%02X", l);
      text(cr, layout, x + 3 * cellw, y, lcol, buf);
      sprintf(buf, "%02X", r);
      text(cr, layout, x + 6 * cellw, y, rcol, buf);
      if ((decoded(c)) && ((l) || (r)))
      {
        table_describe(c, p, buf, sizeof buf, 1);
        buf[TBL_DECODEW - 1] = 0;
        text(cr, layout, x + TBL_WIDTH * cellw, y, dcol, buf);
      }
    }
  }

  drawheader(cr, width, focused);
  for (c = 0; c < MAX_TABLES; c++)
    text(cr, layout, x0 + tblstart(c) * cellw, 4, (c == etnum) ? col_instr : col_headertext, tablenames[c]);
  if (!etlock)
    text(cr, layout, width - 9 * cellw, 4, col_cmd, "Unlocked");
  g_object_unref(layout);
}


//
// Mouse: click to place the cursor, drag (or Shift+click) to select, scroll
// to move the view, right-click for the editor's commands
//

// A context menu command is a classic key, run as if it had been typed
#define GRIDKEY(raw, shift, ascii) ((guint32)(raw) | ((guint32)(shift) << 16) | ((guint32)(ascii) << 24))
// ...or one of these, which have no key
#define GRID_EDITWAVEFORM GRIDKEY(0xffff, 0, 0)
#define GRID_PLAYFROMHERE GRIDKEY(0xfffe, 0, 0)

typedef struct GRIDITEM
{
  const char *label;          // NULL ends a section; an empty section ends the menu
  guint32 key;
  const char *accel;          // shown in the menu only
  const struct GRIDITEM *submenu;
} GRIDITEM;

typedef struct
{
  int mode;                   // EDIT_xxx of this editor
  GtkWidget *area;
  GtkWidget *menu;
  int dragging;
  int dragunit;               // channel or table the drag started in
  int anchor;                 // row where the selection started (-1: none)
  double pointerx;            // last pointer position, for the scroll wheel
} GRID;

static GRID pattgrid = {EDIT_PATTERN};
static GRID ordgrid = {EDIT_ORDERLIST};
static GRID tblgrid = {EDIT_TABLES};

static int columnat(double x)
{
  return (int)floor((x - cellw / 2.0) / cellw);
}

static int rowat(double y, int top)
{
  return top + (int)floor((y - headerheight()) / (double)cellh);
}

static int visiblerows(GtkWidget *area)
{
  return MAX((gtk_widget_get_height(area) - headerheight()) / cellh, 1);
}

static int shiftheld(GtkGesture *gesture)
{
  return (gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(gesture)) & GDK_SHIFT_MASK) != 0;
}

// Pattern editor

static int pattcolumnat(int offset)
{
  if (offset < 4) return 0;
  if (offset < 7) return (offset == 4) ? 1 : 2;
  return MIN(offset - 4, 5);
}

static void markpattern(int c, int start, int end)
{
  int last = pattlen[epnum[c]] - 1;

  if (last < 0) return;
  epmarkchn = c;
  epmarkstart = CLAMP(start, 0, last);
  epmarkend = CLAMP(end, 0, last);
}

// Find the channel and row under the pointer; returns 0 outside the channels
static int pattcellat(double x, double y, int *c, int *p, int *column)
{
  int col = columnat(x) - PATT_ROWNUMW;

  if (col < 0) return 0;
  *c = col / PATT_CHWIDTH;
  if (*c >= MAX_CHN) return 0;
  *column = pattcolumnat(col % PATT_CHWIDTH);
  *p = CLAMP(rowat(y, patttop), 0, pattlen[epnum[*c]]);
  return 1;
}

static void pattpress(GtkGesture *gesture, double x, double y, GRID *g)
{
  int c, p, column;

  if (!pattcellat(x, y, &c, &p, &column)) return;
  // Clicking a channel header toggles its mute
  if (y < headerheight())
  {
    mutechannel(c);
    return;
  }
  // Shift+click extends the selection from the cursor
  if ((shiftheld(gesture)) && (c == epchn))
  {
    g->anchor = (epmarkchn == c) ? epmarkstart : MIN(eppos, pattlen[epnum[c]] - 1);
    markpattern(c, g->anchor, p);
  }
  else
  {
    epmarkchn = -1;
    g->anchor = p;
  }
  epchn = c;
  eppos = p;
  epcolumn = column;
  g->dragunit = c;
  g->dragging = 1;
}

static void pattdrag(double y, GRID *g)
{
  int c = g->dragunit;
  int p;

  // Dragging past the top or bottom edge scrolls
  if (y < headerheight()) patttop--;
  else if (y > gtk_widget_get_height(patterngrid)) patttop++;
  p = CLAMP(rowat(y, patttop), 0, pattlen[epnum[c]] - 1);
  if ((p != g->anchor) || (epmarkchn == c))
  {
    markpattern(c, g->anchor, p);
    eppos = p;
  }
}

static void pattcontext(double x, double y)
{
  int c, p, column;

  if ((y < headerheight()) || (!pattcellat(x, y, &c, &p, &column))) return;
  // Keep a selection that was right-clicked, so the commands act on it
  if ((c == epmarkchn) && (inmark(p, epmarkstart, epmarkend))) return;
  epmarkchn = -1;
  epchn = c;
  eppos = p;
  epcolumn = column;
}

static void pattscroll(int rows)
{
  int visible = visiblerows(patterngrid);
  int c, maxlen = 0;

  for (c = 0; c < MAX_CHN; c++)
    if (pattlen[epnum[c]] > maxlen) maxlen = pattlen[epnum[c]];
  patttop = CLAMP(patttop + rows, -visible / 2, maxlen - visible / 2);
  pattcentred = 0;
}

// Orderlist editor

static void markorder(int c, int start, int end)
{
  int last = songlen[esnum][c] - 1;

  if (last < 0) return;
  esmarkchn = c;
  esmarkstart = CLAMP(start, 0, last);
  esmarkend = CLAMP(end, 0, last);
}

static int ordercellat(double x, double y, int *c, int *p, int *column)
{
  int col = columnat(x) - ORD_ROWNUMW;

  if ((col < 0) || (y < headerheight())) return 0;
  *c = col / ORD_CHWIDTH;
  if (*c >= MAX_CHN) return 0;
  *column = ((col % ORD_CHWIDTH) >= 1) ? 1 : 0;
  *p = rowat(y, orderstop);
  // The RST marker itself is not editable; its value follows it
  if (*p == songlen[esnum][*c]) (*p)++;
  *p = CLAMP(*p, 0, songlen[esnum][*c] + 1);
  return 1;
}

static void orderpress(GtkGesture *gesture, double x, double y, GRID *g)
{
  int c, p, column;

  if (!ordercellat(x, y, &c, &p, &column)) return;
  if ((shiftheld(gesture)) && (c == eschn) && (eseditpos < songlen[esnum][c]))
  {
    g->anchor = (esmarkchn == c) ? esmarkstart : eseditpos;
    markorder(c, g->anchor, p);
  }
  else
  {
    esmarkchn = -1;
    // Only real orderlist positions can be selected
    g->anchor = (p < songlen[esnum][c]) ? p : -1;
  }
  eschn = c;
  eseditpos = p;
  escolumn = column;
  g->dragunit = c;
  g->dragging = 1;
}

static void orderdrag(double y, GRID *g)
{
  int c = g->dragunit;
  int p;

  if (g->anchor < 0) return;
  if (y < headerheight()) orderstop = MAX(orderstop - 1, 0);
  else if (y > gtk_widget_get_height(ordergrid)) orderstop++;
  p = CLAMP(rowat(y, orderstop), 0, songlen[esnum][c] - 1);
  if ((p != g->anchor) || (esmarkchn == c))
  {
    markorder(c, g->anchor, p);
    eseditpos = p;
  }
}

static void ordercontext(double x, double y)
{
  int c, p, column;

  if (!ordercellat(x, y, &c, &p, &column)) return;
  if ((c == esmarkchn) && (inmark(p, esmarkstart, esmarkend))) return;
  esmarkchn = -1;
  eschn = c;
  eseditpos = p;
  escolumn = column;
}

// Table editor

static void marktable(int c, int start, int end)
{
  etmarknum = c;
  etmarkstart = CLAMP(start, 0, MAX_TABLELEN - 1);
  etmarkend = CLAMP(end, 0, MAX_TABLELEN - 1);
}

static int tableat(double x, int *column)
{
  int col = columnat(x);
  int c, offset;

  if (col < 0) return -1;
  for (c = 0; (c < MAX_TABLES) && (col >= tblstart(c + 1)); c++);
  if (c >= MAX_TABLES) return -1;
  offset = col - tblstart(c);
  if (column)
  {
    if (offset <= 3) *column = 0;
    else if (offset == 4) *column = 1;
    else if (offset <= 6) *column = 2;
    else *column = 3;
  }
  return c;
}

static int tablecellat(double x, double y, int *c, int *p, int *column)
{
  *c = tableat(x, column);
  if ((*c < 0) || (y < headerheight())) return 0;
  *p = CLAMP(rowat(y, etview[*c]), 0, MAX_TABLELEN - 1);
  return 1;
}

static void tablepress(GtkGesture *gesture, double x, double y, GRID *g)
{
  int c, p, column;

  if (!tablecellat(x, y, &c, &p, &column)) return;
  if ((shiftheld(gesture)) && (c == etnum))
  {
    g->anchor = (etmarknum == c) ? etmarkstart : etpos;
    marktable(c, g->anchor, p);
  }
  else
  {
    etmarknum = -1;
    g->anchor = p;
  }
  etnum = c;
  etpos = p;
  etcolumn = column;
  g->dragunit = c;
  g->dragging = 1;
}

static void tabledrag(double y, GRID *g)
{
  int c = g->dragunit;
  int p;

  if (y < headerheight()) etview[c] = MAX(etview[c] - 1, 0);
  else if (y > gtk_widget_get_height(tablegrid))
    etview[c] = MIN(etview[c] + 1, MAX_TABLELEN - visiblerows(tablegrid));
  p = CLAMP(rowat(y, etview[c]), 0, MAX_TABLELEN - 1);
  if ((p != g->anchor) || (etmarknum == c))
  {
    marktable(c, g->anchor, p);
    etpos = p;
  }
}

static void tablecontext(double x, double y)
{
  int c, p, column;

  if (!tablecellat(x, y, &c, &p, &column)) return;
  if ((c == etmarknum) && (inmark(p, etmarkstart, etmarkend))) return;
  etmarknum = -1;
  etnum = c;
  etpos = p;
  etcolumn = column;
}

// Gesture handlers shared by the three editors

static void ondragbegin(GtkGestureDrag *gesture, double x, double y, GRID *g)
{
  gtk_widget_grab_focus(g->area);
  editmode = g->mode;
  g->dragging = 0;
  switch (g->mode)
  {
    case EDIT_PATTERN:
    pattcentred = 0;
    pattpress(GTK_GESTURE(gesture), x, y, g);
    break;

    case EDIT_ORDERLIST:
    ordercentred = 0;
    orderpress(GTK_GESTURE(gesture), x, y, g);
    break;

    case EDIT_TABLES:
    tablepress(GTK_GESTURE(gesture), x, y, g);
    break;
  }
  ui_refresh();
}

static void ondragupdate(GtkGestureDrag *gesture, double dx, double dy, GRID *g)
{
  double sx, sy;

  if (!g->dragging) return;
  gtk_gesture_drag_get_start_point(gesture, &sx, &sy);
  switch (g->mode)
  {
    case EDIT_PATTERN: pattdrag(sy + dy, g); break;
    case EDIT_ORDERLIST: orderdrag(sy + dy, g); break;
    case EDIT_TABLES: tabledrag(sy + dy, g); break;
  }
  grid_redraw();
}

static void ondragend(GtkGestureDrag *gesture, double dx, double dy, GRID *g)
{
  g->dragging = 0;
  ui_refresh();
}

static gboolean openorderpattern(gpointer data)
{
  editmode = EDIT_ORDERLIST;
  if (eseditpos < songlen[esnum][eschn]) ui_runkey(KEY_ENTER, 13, 0);
  return G_SOURCE_REMOVE;
}

static void onclick(GtkGestureClick *gesture, int n, double x, double y, GRID *g)
{
  // Double-clicking an orderlist entry opens that pattern (as RETURN). The
  // drag gesture handles this same press after us, so wait until it has.
  if ((n == 2) && (g->mode == EDIT_ORDERLIST) && (y >= headerheight()))
    g_idle_add(openorderpattern, NULL);
}

static void oncontextmenu(GtkGestureClick *gesture, int n, double x, double y, GRID *g)
{
  GdkRectangle where = {(int)x, (int)y, 1, 1};

  gtk_widget_grab_focus(g->area);
  editmode = g->mode;
  switch (g->mode)
  {
    case EDIT_PATTERN: pattcontext(x, y); break;
    case EDIT_ORDERLIST: ordercontext(x, y); break;
    case EDIT_TABLES: tablecontext(x, y); break;
  }
  ui_refresh();
  gtk_popover_set_pointing_to(GTK_POPOVER(g->menu), &where);
  gtk_popover_popup(GTK_POPOVER(g->menu));
}

static void onmotion(GtkEventControllerMotion *controller, double x, double y, GRID *g)
{
  g->pointerx = x;
}

static gboolean onscroll(GtkEventControllerScroll *controller, double dx, double dy, GRID *g)
{
  int rows = (dy > 0) ? 3 : -3;
  int c;

  switch (g->mode)
  {
    case EDIT_PATTERN:
    pattscroll(rows);
    break;

    case EDIT_ORDERLIST:
    orderstop = MAX(orderstop + rows, 0);
    ordercentred = 0;
    break;

    case EDIT_TABLES:
    c = tableat(g->pointerx, NULL);
    if (c >= 0) etview[c] = CLAMP(etview[c] + rows, 0, MAX_TABLELEN - visiblerows(tablegrid));
    tablefollow = 0;
    break;
  }
  grid_redraw();
  return TRUE;
}

static void editwaveform(void)
{
  unsigned char l = ltable[WTBL][etpos];
  int y = headerheight() + (etpos - etview[WTBL]) * cellh;
  GdkRectangle where = {cellw / 2 + 3 * cellw, y, 2 * cellw, cellh};

  if ((etnum != WTBL) || ((l) && (l < 0x10)) || (l >= WAVECMD))
  {
    ui_toast("Waveforms are set on wavetable rows with a left value of 00 or 10–EF");
    return;
  }
  ui_waveformeditor(tablegrid, &where, &ltable[WTBL][etpos], 1, (4 << 16) | etpos);
}

static void ongridkey(GSimpleAction *action, GVariant *parameter, GRID *g)
{
  guint32 key = g_variant_get_uint32(parameter);

  editmode = g->mode;
  gtk_widget_grab_focus(g->area);
  if (key == GRID_EDITWAVEFORM) editwaveform();
  else if (key == GRID_PLAYFROMHERE) ui_playfromhere();
  else ui_runkey(key & 0xffff, key >> 24, (key >> 16) & 1);
}

//
// Context menus
//

static const GRIDITEM transposemenu[] = {
  {"Semitone Up", GRIDKEY(KEY_Q, 1, 0), "<Shift>q"},
  {"Semitone Down", GRIDKEY(KEY_A, 1, 0), "<Shift>a"},
  {"Octave Up", GRIDKEY(KEY_W, 1, 0), "<Shift>w"},
  {"Octave Down", GRIDKEY(KEY_S, 1, 0), "<Shift>s"},
  {NULL}, {NULL}
};

static const GRIDITEM patternmenu2[] = {
  {"Reverse Rows", GRIDKEY(KEY_I, 1, 0), "<Shift>i"},
  {"Shrink to Half Length", GRIDKEY(KEY_O, 1, 0), "<Shift>o"},
  {"Expand to Double Length", GRIDKEY(KEY_P, 1, 0), "<Shift>p"},
  {NULL},
  {"Split at Cursor", GRIDKEY(KEY_K, 1, 0), "<Shift>k"},
  {"Join with Next in Orderlist", GRIDKEY(KEY_J, 1, 0), "<Shift>j"},
  {NULL}, {NULL}
};

static const GRIDITEM patternmenu[] = {
  {"Cut", GRIDKEY(KEY_X, 1, 0), "<Shift>x"},
  {"Copy", GRIDKEY(KEY_C, 1, 0), "<Shift>c"},
  {"Paste", GRIDKEY(KEY_V, 1, 0), "<Shift>v"},
  {"Select Whole Pattern", GRIDKEY(KEY_L, 1, 0), "<Shift>l"},
  {NULL},
  {"Copy Commands", GRIDKEY(KEY_E, 1, 0), "<Shift>e"},
  {"Paste Commands", GRIDKEY(KEY_R, 1, 0), "<Shift>r"},
  {NULL},
  {"Transpose", 0, NULL, transposemenu},
  {"Insert Row", GRIDKEY(KEY_INS, 0, 0), "Insert"},
  {"Delete Row", GRIDKEY(KEY_DEL, 0, 127), "Delete"},
  {"Pattern", 0, NULL, patternmenu2},
  {"Portamento to Next Note", GRIDKEY(KEY_Y, 1, 0), "<Shift>y"},
  {NULL},
  {"Mute Channel", GRIDKEY(KEY_F4, 1, 0), "<Shift>F4"},
  {NULL}, {NULL}
};

static const GRIDITEM ordermenu[] = {
  {"Cut", GRIDKEY(KEY_X, 1, 0), "<Shift>x"},
  {"Copy", GRIDKEY(KEY_C, 1, 0), "<Shift>c"},
  {"Paste", GRIDKEY(KEY_V, 1, 0), "<Shift>v"},
  {"Select Whole Orderlist", GRIDKEY(KEY_L, 1, 0), "<Shift>l"},
  {NULL},
  {"Insert Position", GRIDKEY(KEY_INS, 0, 0), "Insert"},
  {"Delete Position", GRIDKEY(KEY_DEL, 0, 127), "Delete"},
  {"Change to Repeat", GRIDKEY(KEY_R, 1, 'R'), "<Shift>r"},
  {"Change to Transpose Up", GRIDKEY(0, 0, '+'), "plus"},
  {"Change to Transpose Down", GRIDKEY(0, 0, '-'), "minus"},
  {NULL},
  {"Set Start Position", GRIDKEY(KEY_SPACE, 0, ' '), "space"},
  {"Set End Position", GRIDKEY(KEY_BACKSPACE, 0, 8), "BackSpace"},
  {"Open Pattern", GRIDKEY(KEY_ENTER, 0, 13), "Return"},
  {"Play from Here", GRID_PLAYFROMHERE, "<Control>Return"},
  {NULL}, {NULL}
};

static const GRIDITEM tablemenu[] = {
  {"Cut", GRIDKEY(KEY_X, 1, 0), "<Shift>x"},
  {"Copy", GRIDKEY(KEY_C, 1, 0), "<Shift>c"},
  {"Paste", GRIDKEY(KEY_V, 1, 0), "<Shift>v"},
  {NULL},
  {"Insert Row", GRIDKEY(KEY_INS, 0, 0), "Insert"},
  {"Delete Row", GRIDKEY(KEY_DEL, 0, 127), "Delete"},
  {NULL},
  {"Negate Value", GRIDKEY(KEY_N, 1, 0), "<Shift>n"},
  {"Convert Absolute/Relative Note", GRIDKEY(KEY_R, 1, 0), "<Shift>r"},
  {"Convert Pulse/Filter Limit", GRIDKEY(KEY_L, 1, 0), "<Shift>l"},
  {"Remove Unused Rows", GRIDKEY(KEY_O, 1, 0), "<Shift>o"},
  {NULL},
  {"Edit Waveform…", GRID_EDITWAVEFORM, NULL},
  {NULL}, {NULL}
};

static GMenuModel *buildgridmenu(const GRIDITEM *items)
{
  GMenu *menu = g_menu_new();

  while (items->label)
  {
    GMenu *section = g_menu_new();

    for (; items->label; items++)
    {
      if (items->submenu)
      {
        GMenuModel *submenu = buildgridmenu(items->submenu);
        g_menu_append_submenu(section, items->label, submenu);
        g_object_unref(submenu);
      }
      else
      {
        GMenuItem *item = g_menu_item_new(items->label, NULL);
        g_menu_item_set_action_and_target_value(item, "grid.key", g_variant_new_uint32(items->key));
        if (items->accel) g_menu_item_set_attribute(item, "accel", "s", items->accel);
        g_menu_append_item(section, item);
        g_object_unref(item);
      }
    }
    g_menu_append_section(menu, NULL, G_MENU_MODEL(section));
    g_object_unref(section);
    items++;    // past the section end
  }
  return G_MENU_MODEL(menu);
}

//
// Construction
//

static void onfocusenter(GtkEventControllerFocus *controller, gpointer data)
{
  editmode = GPOINTER_TO_INT(data);
  ui_refresh();
}

static void onfocusleave(GtkEventControllerFocus *controller, gpointer data)
{
  grid_redraw();
}

static void ondestroy(GtkWidget *area, GRID *g)
{
  gtk_widget_unparent(g->menu);
}

static GtkWidget *newgrid(GRID *g, GtkDrawingAreaDrawFunc draw, const GRIDITEM *menuitems, const char *name)
{
  GtkWidget *area = gtk_drawing_area_new();
  GtkGesture *gesture;
  GtkEventController *controller;
  GSimpleActionGroup *actions = g_simple_action_group_new();
  GSimpleAction *action = g_simple_action_new("key", G_VARIANT_TYPE_UINT32);
  GMenuModel *menu = buildgridmenu(menuitems);

  g->area = area;
  gtk_widget_set_focusable(area, TRUE);
  gtk_widget_set_hexpand(area, TRUE);
  gtk_widget_set_vexpand(area, TRUE);
  gtk_widget_set_overflow(area, GTK_OVERFLOW_HIDDEN);
  gtk_accessible_update_property(GTK_ACCESSIBLE(area), GTK_ACCESSIBLE_PROPERTY_LABEL, name, -1);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), draw, NULL, NULL);

  gesture = gtk_gesture_drag_new();
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), GDK_BUTTON_PRIMARY);
  g_signal_connect(gesture, "drag-begin", G_CALLBACK(ondragbegin), g);
  g_signal_connect(gesture, "drag-update", G_CALLBACK(ondragupdate), g);
  g_signal_connect(gesture, "drag-end", G_CALLBACK(ondragend), g);
  gtk_widget_add_controller(area, GTK_EVENT_CONTROLLER(gesture));

  gesture = gtk_gesture_click_new();
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), GDK_BUTTON_PRIMARY);
  g_signal_connect(gesture, "pressed", G_CALLBACK(onclick), g);
  gtk_widget_add_controller(area, GTK_EVENT_CONTROLLER(gesture));

  gesture = gtk_gesture_click_new();
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), GDK_BUTTON_SECONDARY);
  g_signal_connect(gesture, "pressed", G_CALLBACK(oncontextmenu), g);
  gtk_widget_add_controller(area, GTK_EVENT_CONTROLLER(gesture));

  controller = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL | GTK_EVENT_CONTROLLER_SCROLL_DISCRETE);
  g_signal_connect(controller, "scroll", G_CALLBACK(onscroll), g);
  gtk_widget_add_controller(area, controller);

  controller = gtk_event_controller_motion_new();
  g_signal_connect(controller, "motion", G_CALLBACK(onmotion), g);
  gtk_widget_add_controller(area, controller);

  controller = gtk_event_controller_focus_new();
  g_signal_connect(controller, "enter", G_CALLBACK(onfocusenter), GINT_TO_POINTER(g->mode));
  g_signal_connect(controller, "leave", G_CALLBACK(onfocusleave), NULL);
  gtk_widget_add_controller(area, controller);

  g_signal_connect(action, "activate", G_CALLBACK(ongridkey), g);
  g_action_map_add_action(G_ACTION_MAP(actions), G_ACTION(action));
  gtk_widget_insert_action_group(area, "grid", G_ACTION_GROUP(actions));
  g_object_unref(action);
  g_object_unref(actions);

  g->menu = gtk_popover_menu_new_from_model_full(menu, GTK_POPOVER_MENU_NESTED);
  gtk_popover_set_has_arrow(GTK_POPOVER(g->menu), FALSE);
  gtk_widget_set_halign(g->menu, GTK_ALIGN_START);
  gtk_widget_set_parent(g->menu, area);
  g_signal_connect(area, "destroy", G_CALLBACK(ondestroy), g);
  g_object_unref(menu);
  return area;
}

GtkWidget *grid_pattern_new(void)
{
  patterngrid = newgrid(&pattgrid, drawpattern, patternmenu, "Pattern editor");
  return patterngrid;
}

GtkWidget *grid_orderlist_new(void)
{
  ordergrid = newgrid(&ordgrid, draworderlist, ordermenu, "Orderlist editor");
  return ordergrid;
}

GtkWidget *grid_tables_new(void)
{
  tablegrid = newgrid(&tblgrid, drawtables, tablemenu, "Table editor");
  grid_setfontscale(bigwindow);
  return tablegrid;
}
