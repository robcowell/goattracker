//
// GOATTRACKER v2 GTK user interface: plain-language descriptions
//
// Describes the pattern, orderlist or table entry under the cursor for the
// status bar, following the definitions in readme.txt sections 3.2-3.4.
//

#include <string.h>
#include "gtkui.h"

static void waveformname(unsigned char w, char *buf, int size)
{
  static const char *names[] = {"triangle", "saw", "pulse", "noise"};
  static const char *bits[] = {"gate", "sync", "ring", "test"};
  int c, len = 0;

  buf[0] = 0;
  for (c = 0; c < 4; c++)
    if (w & (0x10 << c)) len += snprintf(buf + len, size - len, "%s%s", len ? "+" : "", names[c]);
  if (!len) len += snprintf(buf + len, size - len, "silent");
  for (c = 0; c < 4; c++)
    if (w & (1 << c)) len += snprintf(buf + len, size - len, ", %s", bits[c]);
}

static void channelmask(unsigned char mask, char *buf, int size)
{
  int c, len = 0;

  buf[0] = 0;
  for (c = 0; c < MAX_CHN; c++)
    if (mask & (1 << c)) len += snprintf(buf + len, size - len, "%s%d", len ? "+" : "channels ", c + 1);
  if (!len) snprintf(buf, size, "no channels");
}

static const char *passband(unsigned char l)
{
  static const char *names[] = {"no passband", "lowpass", "bandpass", "low+bandpass",
    "highpass", "notch", "band+highpass", "all passbands"};
  return names[(l >> 4) & 7];
}

// Describe speedtable row (1-based) as its possible meanings
static void speedrow(int row, char *buf, int size)
{
  unsigned char l, r;

  if ((!row) || (row > MAX_TABLELEN))
  {
    snprintf(buf, size, "no speedtable entry");
    return;
  }
  l = ltable[STBL][row - 1];
  r = rtable[STBL][row - 1];
  snprintf(buf, size, "speedtable %02X = %02X %02X%s", row, l, r,
    (l & 0x80) ? " (note-independent)" : "");
}

// Describe a table row. brief = a short form for narrow columns.
void table_describe(int table, int pos, char *buf, int size, int brief)
{
  unsigned char l = ltable[table][pos];
  unsigned char r = rtable[table][pos];
  char wave[64];

  if ((l == 0xff) && (table != STBL))
  {
    if (!r) snprintf(buf, size, brief ? "STOP" : "Stop table execution");
    else snprintf(buf, size, brief ? "JUMP %02X" : "Jump to row %02X", r);
    return;
  }

  switch (table)
  {
    case WTBL:
    {
      char note[32];

      if (r <= 0x5f) snprintf(note, sizeof note, "%s%d", brief ? "+" : "note +", r);
      else if (r <= 0x7f) snprintf(note, sizeof note, "%s%d", brief ? "" : "note ", r - 0x80);
      else if (r == 0x80) snprintf(note, sizeof note, "%s", brief ? "=" : "pitch unchanged");
      else if (r <= 0xdf) snprintf(note, sizeof note, "%s%s", brief ? "" : "note ", notename[r - 0x80]);
      else snprintf(note, sizeof note, "?");

      if (!l) snprintf(buf, size, brief ? "      %s" : "Waveform unchanged, %s", note);
      else if (l <= 0x0f) snprintf(buf, size, brief ? "DELAY %d" : "Delay %d frames", l);
      else if (l <= 0xdf)
      {
        waveformname(l, wave, sizeof wave);
        if (brief) snprintf(buf, size, "WAVE %02X %s", l, note);
        else snprintf(buf, size, "Waveform %02X (%s), %s", l, wave, note);
      }
      else if (l <= 0xef)
      {
        if (brief) snprintf(buf, size, "MUTE %02X %s", l & 0x0f, note);
        else snprintf(buf, size, "Inaudible waveform %02X, %s", l & 0x0f, note);
      }
      else snprintf(buf, size, brief ? "CMD %X%02X" : "Command %X%02X", l & 0x0f, r);
      break;
    }

    case PTBL:
    if (l & 0x80)
      snprintf(buf, size, brief ? "SET %03X" : "Set pulse width %03X", ((l & 0x0f) << 8) | r);
    else if (l)
      snprintf(buf, size, brief ? "MOD %+d x%d" : "Change pulse width by %+d for %d frames", (signed char)r, l);
    else
      snprintf(buf, size, brief ? "" : "Empty row");
    break;

    case FTBL:
    if (l & 0x80)
    {
      char chns[32];
      channelmask(r & 0x0f, chns, sizeof chns);
      if (brief) snprintf(buf, size, "%s R%X %X", (l & 0x70) ? passband(l) : "OFF", r >> 4, r & 0x0f);
      else snprintf(buf, size, "Filter %s, resonance %X, %s", passband(l), r >> 4, chns);
    }
    else if (l)
      snprintf(buf, size, brief ? "MOD %+d x%d" : "Change cutoff by %+d for %d frames", (signed char)r, l);
    else
      snprintf(buf, size, brief ? "CUTOFF %02X" : "Set cutoff %02X", r);
    break;

    case STBL:
    if (brief) snprintf(buf, size, "%s", (l & 0x80) ? "note-indep." : "");
    else snprintf(buf, size, "Vibrato speed %02X depth %02X, portamento speed %04X, or funktempo %02X/%02X%s",
      l, r, (l << 8) | r, l, r, (l & 0x80) ? " (note-independent)" : "");
    break;
  }
}

static void describecommand(unsigned char cmd, unsigned char data, char *buf, int size)
{
  char extra[96];

  switch (cmd)
  {
    case 0x0: snprintf(buf, size, "No command"); break;
    case 0x1: speedrow(data, extra, sizeof extra); snprintf(buf, size, "1%02X Portamento up, %s", data, extra); break;
    case 0x2: speedrow(data, extra, sizeof extra); snprintf(buf, size, "2%02X Portamento down, %s", data, extra); break;
    case 0x3:
    if (!data) snprintf(buf, size, "300 Tie note (legato)");
    else
    {
      speedrow(data, extra, sizeof extra);
      snprintf(buf, size, "3%02X Toneportamento to the note, %s", data, extra);
    }
    break;
    case 0x4: speedrow(data, extra, sizeof extra); snprintf(buf, size, "4%02X Vibrato, %s", data, extra); break;
    case 0x5: snprintf(buf, size, "5%02X Set attack %X, decay %X", data, data >> 4, data & 0xf); break;
    case 0x6: snprintf(buf, size, "6%02X Set sustain %X, release %X", data, data >> 4, data & 0xf); break;
    case 0x7: waveformname(data, extra, sizeof extra); snprintf(buf, size, "7%02X Set waveform: %s", data, extra); break;
    case 0x8: snprintf(buf, size, data ? "8%02X Wavetable from row %02X" : "800 Stop the wavetable", data, data); break;
    case 0x9: snprintf(buf, size, data ? "9%02X Pulsetable from row %02X" : "900 Stop the pulsetable", data, data); break;
    case 0xa: snprintf(buf, size, data ? "A%02X Filtertable from row %02X" : "A00 Stop the filtertable", data, data); break;
    case 0xb:
    if (!data) snprintf(buf, size, "B00 Filter off (stops the filtertable)");
    else
    {
      channelmask(data & 0x0f, extra, sizeof extra);
      snprintf(buf, size, "B%02X Filter resonance %X, %s", data, data >> 4, extra);
    }
    break;
    case 0xc: snprintf(buf, size, "C%02X Set filter cutoff %02X", data, data); break;
    case 0xd:
    if (!(data & 0xf0)) snprintf(buf, size, "D%02X Set master volume %X", data, data & 0xf);
    else snprintf(buf, size, "D%02X Write %02X to the timing mark (player address + $3F)", data, data);
    break;
    case 0xe: speedrow(data, extra, sizeof extra); snprintf(buf, size, "E%02X Funktempo, %s", data, extra); break;
    case 0xf:
    if (data < 0x02) snprintf(buf, size, "F%02X Recall the funktempo set by EXY", data);
    else if (data < 0x80) snprintf(buf, size, "F%02X Tempo %d on all channels", data, data);
    else snprintf(buf, size, "F%02X Tempo %d on this channel", data, data - 0x80);
    break;
  }
}

static void describepattern(char *buf, int size)
{
  unsigned char *row = &pattern[epnum[epchn]][eppos * 4];

  if ((eppos >= pattlen[epnum[epchn]]) || (row[0] == ENDPATT))
  {
    snprintf(buf, size, "End of pattern %02X (%d rows). INS/DEL here changes its length",
      epnum[epchn], pattlen[epnum[epchn]]);
    return;
  }
  switch (epcolumn)
  {
    case 0:
    if (row[0] == REST) snprintf(buf, size, "Rest (no new note)");
    else if (row[0] == KEYOFF) snprintf(buf, size, "Key off: release the note");
    else if (row[0] == KEYON) snprintf(buf, size, "Key on: retrigger the gate");
    else snprintf(buf, size, "Note %s", notename[row[0] - FIRSTNOTE]);
    break;

    case 1:
    case 2:
    if (!row[1]) snprintf(buf, size, "No instrument change");
    else snprintf(buf, size, "Instrument %02X: %s", row[1], ui_toutf8(instr[row[1]].name));
    break;

    default:
    describecommand(row[2], row[3], buf, size);
    break;
  }
}

static void describeorderlist(char *buf, int size)
{
  int len = songlen[esnum][eschn];
  unsigned char v = songorder[esnum][eschn][eseditpos];

  if (eseditpos == len) snprintf(buf, size, "End of channel %d's orderlist", eschn + 1);
  else if (eseditpos == len + 1) snprintf(buf, size, "Restart position: the song loops back to %02X", v);
  else if (v < REPEAT) snprintf(buf, size, "Pattern %02X (%d rows)", v, pattlen[v]);
  else if (v >= TRANSUP) snprintf(buf, size, "Transpose the following patterns up %d semitones", v & 0x0f);
  else if (v >= TRANSDOWN) snprintf(buf, size, "Transpose the following patterns down %d semitones", 16 - (v & 0x0f));
  else snprintf(buf, size, "Repeat the next pattern %d times", ((v + 1) & 0x0f) ? ((v + 1) & 0x0f) : 16);
}

void info_describe(char *buf, int size)
{
  buf[0] = 0;
  switch (editmode)
  {
    case EDIT_PATTERN: describepattern(buf, size); break;
    case EDIT_ORDERLIST: describeorderlist(buf, size); break;
    case EDIT_TABLES: table_describe(etnum, etpos, buf, size, 0); break;
  }
}
