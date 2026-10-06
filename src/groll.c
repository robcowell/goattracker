//
// GOATTRACKER v2: a pattern as notes with lengths (for piano roll editing)
//

#include <stdlib.h>
#include "goattrk2.h"
#include "groll.h"
#include "ginfo.h"

static int isnote(unsigned char n)
{
  return (n >= FIRSTNOTE) && (n <= LASTNOTE);
}

void roll_read(int patt, ROLL *roll)
{
  int row, rows = pattlen[patt];
  ROLLNOTE *open = NULL;

  memset(roll, 0, sizeof *roll);
  if (rows > MAX_PATTROWS) rows = MAX_PATTROWS;
  roll->rows = rows;
  for (row = 0; row < rows; row++)
  {
    unsigned char *p = &pattern[patt][row * 4];

    if (isnote(p[0]))
    {
      if (open) open->len = row - open->start;
      open = &roll->notes[roll->nnotes++];
      open->start = row;
      open->note = p[0];
      open->instr = p[1];
      open->cmd = p[2];
      open->data = p[3];
    }
    else if ((p[0] == KEYOFF) && (open))
    {
      open->len = row - open->start;
      open->keyoff = 1;
      open->offinstr = p[1];
      open->offcmd = p[2];
      open->offdata = p[3];
      open = NULL;
    }
    else
    {
      memcpy(roll->other[row], p, 4);
      roll->hasother[row] = 1;
    }
  }
  if (open) open->len = rows - open->start;
}

static int bystart(const void *a, const void *b)
{
  return ((const ROLLNOTE *)a)->start - ((const ROLLNOTE *)b)->start;
}

void roll_write(int patt, ROLL *roll)
{
  int row, i, rows = roll->rows;

  qsort(roll->notes, roll->nnotes, sizeof(ROLLNOTE), bystart);
  for (row = 0; row < rows; row++)
  {
    unsigned char *p = &pattern[patt][row * 4];
    if (roll->hasother[row]) memcpy(p, roll->other[row], 4);
    else
    {
      p[0] = REST;
      p[1] = p[2] = p[3] = 0;
    }
  }
  for (i = 0; i < roll->nnotes; i++)
  {
    ROLLNOTE *n = &roll->notes[i];
    int next = (i + 1 < roll->nnotes) ? roll->notes[i + 1].start : rows;
    int end;
    unsigned char *p = &pattern[patt][n->start * 4];

    if (n->len < 1) n->len = 1;
    if (n->start + n->len > next) n->len = next - n->start;
    end = n->start + n->len;
    p[0] = n->note;
    p[1] = n->instr;
    p[2] = n->cmd;
    p[3] = n->data;
    // Released before the next note (or the pattern end) starts
    if ((end < next) || ((end < rows) && (n->keyoff)))
    {
      if (end < rows)
      {
        unsigned char *q = &pattern[patt][end * 4];
        n->keyoff = 1;
        q[0] = KEYOFF;
        if ((n->offinstr) || (n->offcmd) || (n->offdata) || (!roll->hasother[end]))
        {
          q[1] = n->offinstr;
          q[2] = n->offcmd;
          q[3] = n->offdata;
        }
      }
    }
    else n->keyoff = 0;
  }
}

int roll_instrument(const ROLL *roll, int index)
{
  int row = roll->notes[index].start, i = index;

  // Instruments set on the note rows and on the other rows, whichever is
  // latest at or before this note
  for (; row >= 0; row--)
  {
    if ((i >= 0) && (roll->notes[i].start == row))
    {
      if (roll->notes[i].instr) return roll->notes[i].instr;
      i--;
      continue;
    }
    if ((roll->hasother[row]) && (roll->other[row][1])) return roll->other[row][1];
    if ((i >= 0) && (roll->notes[i].keyoff) && (roll->notes[i].start + roll->notes[i].len == row) &&
      (roll->notes[i].offinstr)) return roll->notes[i].offinstr;
  }
  return 0;
}

void roll_setlength(int patt, int rows)
{
  int row, old = pattlen[patt];

  if (rows < 1) rows = 1;
  if (rows > MAX_PATTROWS) rows = MAX_PATTROWS;
  for (row = old; row < rows; row++)
  {
    unsigned char *p = &pattern[patt][row * 4];
    p[0] = REST;
    p[1] = p[2] = p[3] = 0;
  }
  for (row = rows; row <= MAX_PATTROWS; row++)
  {
    unsigned char *p = &pattern[patt][row * 4];
    p[0] = ENDPATT;
    p[1] = p[2] = p[3] = 0;
  }
  pattlen[patt] = rows;
}

int roll_speedentry(unsigned char l, unsigned char r)
{
  int i;

  for (i = 0; i < MAX_TABLELEN; i++)
    if ((ltable[STBL][i] == l) && (rtable[STBL][i] == r) && ((l) || (r))) return i + 1;
  for (i = 0; i < MAX_TABLELEN; i++)
    if ((!ltable[STBL][i]) && (!rtable[STBL][i]) && (!table_isreachable(STBL, i)))
    {
      ltable[STBL][i] = l;
      rtable[STBL][i] = r;
      return i + 1;
    }
  return 0;
}
