//
// GOATTRACKER v2 GTK user interface: pattern, orderlist and table editors
//
// Each editor is a GtkDrawingArea that draws the engine's state in tracker
// style with a monospace font. Clicking moves the engine's edit cursor;
// keyboard editing goes through the engine's command handlers (gtkui.c).
//

#include <string.h>
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
// Table layout
#define TBL_WIDTH 10

static const char *tablenames[] = {"Wave", "Pulse", "Filter", "Speed"};

// Geometry of the last draw, for mapping mouse clicks
static int patttop, orderstop;

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
  gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(tablegrid), (MAX_TABLES * (TBL_WIDTH + 1) + 1) * cellw);
  gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(tablegrid), headerheight() + 8 * cellh);
}

void grid_setfontscale(int scale)
{
  fontscale = CLAMP(scale, 1, 4);
  if (fontdesc) pango_font_description_free(fontdesc);
  fontdesc = NULL;
  updatesizes();
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
  top = eppos - (height - headerheight()) / cellh / 2;
  patttop = top;
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

static void onpatternclick(GtkGestureClick *gesture, int n, double x, double y, gpointer data)
{
  int col = (int)((x - cellw / 2) / cellw) - PATT_ROWNUMW;
  int c, offset, p;

  gtk_widget_grab_focus(patterngrid);
  editmode = EDIT_PATTERN;
  if (col < 0) return;
  c = col / PATT_CHWIDTH;
  offset = col % PATT_CHWIDTH;
  if (c >= MAX_CHN) return;

  // Clicking a channel header toggles its mute
  if (y < headerheight())
  {
    mutechannel(c);
    ui_refresh();
    return;
  }

  p = patttop + (int)((y - headerheight()) / cellh);
  epchn = c;
  eppos = CLAMP(p, 0, pattlen[epnum[c]]);
  if (offset < 4) epcolumn = 0;
  else if (offset < 7) epcolumn = (offset == 4) ? 1 : 2;
  else epcolumn = MIN(offset - 4, 5);
  ui_refresh();
}

static gboolean onpatternscroll(GtkEventControllerScroll *controller, double dx, double dy, gpointer data)
{
  int rows = (dy > 0) ? 2 : -2;

  if (followplay && isplaying()) return TRUE;
  eppos = CLAMP(eppos + rows, 0, pattlen[epnum[epchn]]);
  ui_refresh();
  return TRUE;
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
  top = eseditpos - (height - headerheight()) / cellh / 2;
  if (top + rows > maxlen) top = maxlen - rows + 1;
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

static void onorderclick(GtkGestureClick *gesture, int n, double x, double y, gpointer data)
{
  int col = (int)((x - cellw / 2) / cellw) - ORD_ROWNUMW;
  int c, p;

  gtk_widget_grab_focus(ordergrid);
  editmode = EDIT_ORDERLIST;
  if ((col < 0) || (y < headerheight())) return;
  c = col / ORD_CHWIDTH;
  if (c >= MAX_CHN) return;
  p = orderstop + (int)((y - headerheight()) / cellh);
  // The RST marker itself is not editable; its value follows it
  if (p == songlen[esnum][c]) p++;
  eschn = c;
  eseditpos = CLAMP(p, 0, songlen[esnum][c] + 1);
  escolumn = ((col % ORD_CHWIDTH) >= 1) ? 1 : 0;
  esmarkchn = -1;
  ui_refresh();
}

static gboolean onorderscroll(GtkEventControllerScroll *controller, double dx, double dy, gpointer data)
{
  int oldshift = shiftpressed;

  shiftpressed = 0;
  if (dy > 0) { orderright(); orderright(); }
  else { orderleft(); orderleft(); }
  shiftpressed = oldshift;
  ui_refresh();
  return TRUE;
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
  char buf[16];

  // Keep the edit row visible when the pane shows fewer rows than the engine assumes
  if (etpos - etview[etnum] >= rows) etview[etnum] = etpos - rows + 1;
  if (etpos < etview[etnum]) etview[etnum] = etpos;

  fill(cr, 0, 0, width, height, col_bg);

  for (c = 0; c < MAX_TABLES; c++)
  {
    int x = x0 + c * (TBL_WIDTH + 1) * cellw;
    int instrstart = instr[einum].ptr[c] - 1;

    if (c) fill(cr, x - cellw, headerheight(), 1, height, col_separator);

    for (d = 0; d < rows; d++)
    {
      int p = etview[c] + d;
      int y = headerheight() + d * cellh;
      unsigned char l, r;
      RGB lcol = col_note, rcol = col_data;

      if (p >= MAX_TABLELEN) break;
      l = ltable[c][p];
      r = rtable[c][p];

      if ((c == etnum) && (p == etpos))
        fill(cr, x - cellw / 2, y, TBL_WIDTH * cellw, cellh, col_cursorrow);
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
      if ((c != STBL) && (l == 0xff)) lcol = col_special;
      if ((!l) && (!r))
      {
        lcol = col_empty;
        rcol = col_empty;
      }

      sprintf(buf, "%02X", p + 1);
      text(cr, layout, x, y, col_rownum, buf);
      sprintf(buf, "%02X", l);
      text(cr, layout, x + 3 * cellw, y, lcol, buf);
      sprintf(buf, "%02X", r);
      text(cr, layout, x + 6 * cellw, y, rcol, buf);
    }
  }

  drawheader(cr, width, focused);
  for (c = 0; c < MAX_TABLES; c++)
    text(cr, layout, x0 + c * (TBL_WIDTH + 1) * cellw, 4, (c == etnum) ? col_instr : col_headertext, tablenames[c]);
  if (!etlock)
    text(cr, layout, width - 9 * cellw, 4, col_cmd, "Unlocked");
  g_object_unref(layout);
}

static int tableatx(double x)
{
  int col = (int)((x - cellw / 2) / cellw);
  return CLAMP(col / (TBL_WIDTH + 1), 0, MAX_TABLES - 1);
}

static void ontableclick(GtkGestureClick *gesture, int n, double x, double y, gpointer data)
{
  int c = tableatx(x);
  int col = (int)((x - cellw / 2) / cellw) - c * (TBL_WIDTH + 1);
  int p;

  gtk_widget_grab_focus(tablegrid);
  editmode = EDIT_TABLES;
  if (y < headerheight()) return;
  p = etview[c] + (int)((y - headerheight()) / cellh);
  etnum = c;
  etpos = CLAMP(p, 0, MAX_TABLELEN - 1);
  if (col <= 3) etcolumn = 0;
  else if (col == 4) etcolumn = 1;
  else if (col <= 6) etcolumn = 2;
  else etcolumn = 3;
  etmarknum = -1;
  ui_refresh();
}

static gboolean ontablescroll(GtkEventControllerScroll *controller, double dx, double dy, gpointer data)
{
  int rows = (dy > 0) ? 2 : -2;

  etpos = CLAMP(etpos + rows, 0, MAX_TABLELEN - 1);
  validatetableview();
  ui_refresh();
  return TRUE;
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

static GtkWidget *newgrid(GtkDrawingAreaDrawFunc draw, GCallback click, GCallback scroll, int mode, const char *name)
{
  GtkWidget *area = gtk_drawing_area_new();
  GtkGesture *gesture = gtk_gesture_click_new();
  GtkEventController *controller;

  gtk_widget_set_focusable(area, TRUE);
  gtk_widget_set_hexpand(area, TRUE);
  gtk_widget_set_vexpand(area, TRUE);
  gtk_widget_set_overflow(area, GTK_OVERFLOW_HIDDEN);
  gtk_accessible_update_property(GTK_ACCESSIBLE(area), GTK_ACCESSIBLE_PROPERTY_LABEL, name, -1);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), draw, NULL, NULL);

  g_signal_connect(gesture, "pressed", click, NULL);
  gtk_widget_add_controller(area, GTK_EVENT_CONTROLLER(gesture));

  controller = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL | GTK_EVENT_CONTROLLER_SCROLL_DISCRETE);
  g_signal_connect(controller, "scroll", scroll, NULL);
  gtk_widget_add_controller(area, controller);

  controller = gtk_event_controller_focus_new();
  g_signal_connect(controller, "enter", G_CALLBACK(onfocusenter), GINT_TO_POINTER(mode));
  g_signal_connect(controller, "leave", G_CALLBACK(onfocusleave), NULL);
  gtk_widget_add_controller(area, controller);
  return area;
}

GtkWidget *grid_pattern_new(void)
{
  patterngrid = newgrid(drawpattern, G_CALLBACK(onpatternclick), G_CALLBACK(onpatternscroll), EDIT_PATTERN, "Pattern editor");
  return patterngrid;
}

GtkWidget *grid_orderlist_new(void)
{
  ordergrid = newgrid(draworderlist, G_CALLBACK(onorderclick), G_CALLBACK(onorderscroll), EDIT_ORDERLIST, "Orderlist editor");
  return ordergrid;
}

GtkWidget *grid_tables_new(void)
{
  tablegrid = newgrid(drawtables, G_CALLBACK(ontableclick), G_CALLBACK(ontablescroll), EDIT_TABLES, "Table editor");
  grid_setfontscale(bigwindow);
  return tablegrid;
}
