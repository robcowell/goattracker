#ifndef GARRANGE_H
#define GARRANGE_H

// Arrangement editing: a voice's orderlist as a list of clips, one per pass
// of a pattern, which can be edited freely and written back. Engine side
// (no GTK).
//
// Reading follows the playroutine's sequencer (gplay.c sequencer()): a
// transpose byte, then a repeat byte, then the pattern number. Writing puts
// a transpose byte where the transpose changes, and passes of a repeated
// pattern that are still together back into one repeat. Clips remember the
// transpose bytes they were read with, so writing back an unedited
// orderlist gives the same bytes.

#define MAX_CLIPS (MAX_SONGLEN * 16)

#define CLIP_LOOPSTART 1   // the voice restarts here after its last clip (with
                           // none, the voice and the song stop at its end)
#define CLIP_TRANSBYTE 2   // read with a transpose byte before it

typedef struct
{
  unsigned char patt;
  signed char trans;     // semitones, -16 to +14
  unsigned char flags;
  int orderpos;          // orderlist index of the pattern number when read,
                         // -1 for a new clip
  int rep;               // pass of a repeated pattern (0 = first)
  int loopofs;           // loop start: how far into the entry (its transpose
                         // and repeat bytes) the restart position points
} CLIP;

#define MINTRANS -16
#define MAXTRANS 14

// Read a voice's orderlist. Returns the number of clips.
int arr_read(int subtune, int chnum, CLIP *clips, int max);

// Write clips back as the voice's orderlist. Returns 0, leaving the
// orderlist unchanged, if they don't fit (MAX_SONGLEN bytes) or there are
// none (a voice with an empty orderlist would stop the whole song).
int arr_write(int subtune, int chnum, const CLIP *clips, int n);

// How many passes of the song's orderlists (every subtune, every voice)
// play a pattern
int arr_patternuses(int patt);

// A pattern nothing plays and with nothing in it, or -1
int arr_freepattern(void);

// Copy a pattern to a free one; returns the new pattern number or -1
int arr_copypattern(int src);

// Make a free pattern empty, rows long; returns its number or -1
int arr_newpattern(int rows);

// Empty a pattern again (a copy that couldn't be placed), so that it is free
void arr_discardpattern(int patt);

#endif
