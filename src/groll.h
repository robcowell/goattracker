#ifndef GROLL_H
#define GROLL_H

// A pattern as notes with lengths, for piano roll editing. Engine side (no
// GTK).
//
// A voice plays one note at a time: a note sounds from its row until the
// next note starts or a key-off releases it. A note that is still sounding
// at the end of the pattern holds on into whatever plays next. Everything
// else in the pattern (commands and instrument changes on rows without a
// note, key-ons, key-offs that release nothing) is kept as it is, so
// writing back an unedited pattern gives the same bytes.

#define ROLL_MAXNOTES MAX_PATTROWS

typedef struct
{
  int start;              // row
  int len;                // rows until the key-off, the next note or the pattern end
  unsigned char note;     // FIRSTNOTE-based, as in patterns
  unsigned char instr;    // 0 = keep the voice's current instrument
  unsigned char cmd, data;
  int keyoff;             // released by a key-off (otherwise it is cut by the
                          // next note, or holds past the end)
  unsigned char offinstr, offcmd, offdata;  // the rest of the key-off row
} ROLLNOTE;

typedef struct
{
  int rows;
  int nnotes;
  ROLLNOTE notes[ROLL_MAXNOTES];
  unsigned char other[MAX_PATTROWS][4];   // rows that aren't part of a note
  unsigned char hasother[MAX_PATTROWS];
} ROLL;

void roll_read(int patt, ROLL *roll);

// Write a roll back to its pattern: notes are sorted, and each is cut short
// where the next one starts
void roll_write(int patt, ROLL *roll);

// The instrument a note plays with: its own, or the last one set before it
// in the pattern (0 = set before the pattern started)
int roll_instrument(const ROLL *roll, int index);

// Change a pattern's length (1 to MAX_PATTROWS rows); rows past the end are
// dropped, new rows are empty
void roll_setlength(int patt, int rows);

// A speedtable row (1-based, as pattern commands give it) holding l/r:
// one that already does, or a free row nothing uses. 0 if the table is full.
int roll_speedentry(unsigned char l, unsigned char r);

#endif
