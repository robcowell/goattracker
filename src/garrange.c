//
// GOATTRACKER v2: arrangement editing (orderlists as lists of clips)
//

#include "goattrk2.h"
#include "garrange.h"

int arr_read(int subtune, int chnum, CLIP *clips, int max)
{
  unsigned char *order = songorder[subtune][chnum];
  int len = songlen[subtune][chnum];
  int restart = order[len + 1];
  int pos = 0, trans = 0, n = 0, looped = 0;

  while ((pos < len) && (n < max))
  {
    int group = pos, repeat = 0, r, transbyte = 0;
    unsigned char v = order[pos];

    if ((v >= TRANSDOWN) && (v < LOOPSONG))
    {
      transbyte = 1;
      trans = (signed char)(v - TRANSUP);
      if (++pos >= len) break;
      v = order[pos];
    }
    if ((v >= REPEAT) && (v < TRANSDOWN))
    {
      repeat = v - REPEAT;
      if (++pos >= len) break;
      v = order[pos];
    }
    if (v >= MAX_PATT)
    {
      pos++;
      continue;
    }
    for (r = 0; (r <= repeat) && (n < max); r++)
    {
      CLIP *clip = &clips[n++];
      clip->patt = v;
      clip->trans = trans;
      clip->flags = ((transbyte) && (!r)) ? CLIP_TRANSBYTE : 0;
      clip->orderpos = pos;
      clip->rep = r;
      // The restart position normally points at the start of an entry
      // (its transpose or repeat byte), or into it
      clip->loopofs = 0;
      if ((!looped) && (!r) && (restart <= pos) && (restart >= group))
      {
        clip->flags |= CLIP_LOOPSTART;
        clip->loopofs = restart - group;
        looped = 1;
      }
    }
    pos++;
  }
  return n;
}

int arr_write(int subtune, int chnum, const CLIP *clips, int n)
{
  unsigned char buf[MAX_SONGLEN + 2];
  int len = 0, restart = -1, trans = 0, i = 0;

  if (n <= 0) return 0;
  while (i < n)
  {
    const CLIP *clip = &clips[i];
    int passes = 1, loopstart = clip->flags & CLIP_LOOPSTART, group = len;

    // Passes of a repeated pattern that are still together stay one
    // orderlist entry
    while ((i + passes < n) && (passes < 16) && (clip->orderpos >= 0) &&
      (clips[i + passes].orderpos == clip->orderpos) && (clips[i + passes].rep == clip->rep + passes) &&
      (clips[i + passes].patt == clip->patt) && (clips[i + passes].trans == clip->trans) &&
      (!(clips[i + passes].flags & (CLIP_LOOPSTART | CLIP_TRANSBYTE))))
      passes++;

    // After looping, the transpose is whatever the last clip left, so the
    // loop start needs its own if that is different
    if ((clip->trans != trans) || ((loopstart) && (clips[n - 1].trans != clip->trans)) ||
      (clip->flags & CLIP_TRANSBYTE))
    {
      if (len >= MAX_SONGLEN) return 0;
      buf[len++] = (unsigned char)(TRANSUP + clip->trans);
      trans = clip->trans;
    }
    if (passes > 1)
    {
      if (len >= MAX_SONGLEN) return 0;
      buf[len++] = REPEAT + passes - 1;
    }
    if (len >= MAX_SONGLEN) return 0;
    buf[len++] = clip->patt;
    // Into the entry no further than its pattern number
    if (loopstart) restart = group + (clip->loopofs < len - 1 - group ? clip->loopofs : len - 1 - group);
    i += passes;
  }
  // No loop start: the restart position is past the end, so the song stops
  if (restart < 0) restart = len;

  memset(songorder[subtune][chnum], 0, MAX_SONGLEN + 2);
  memcpy(songorder[subtune][chnum], buf, len);
  songorder[subtune][chnum][len] = LOOPSONG;
  songorder[subtune][chnum][len + 1] = restart;
  songlen[subtune][chnum] = len;
  return 1;
}

int arr_patternuses(int patt)
{
  static CLIP clips[MAX_CLIPS];
  int s, c, i, n, uses = 0;

  for (s = 0; s < MAX_SONGS; s++)
    for (c = 0; c < MAX_CHN; c++)
    {
      n = arr_read(s, c, clips, MAX_CLIPS);
      for (i = 0; i < n; i++)
        if (clips[i].patt == patt) uses++;
    }
  return uses;
}

static int isempty(int patt)
{
  int row;

  for (row = 0; row < pattlen[patt]; row++)
  {
    unsigned char *p = &pattern[patt][row * 4];
    if ((p[0] != REST) || (p[1]) || (p[2]) || (p[3])) return 0;
  }
  return 1;
}

int arr_freepattern(void)
{
  static unsigned char used[MAX_PATT];
  int s, c, d, p;

  memset(used, 0, sizeof used);
  for (s = 0; s < MAX_SONGS; s++)
    for (c = 0; c < MAX_CHN; c++)
      for (d = 0; d < songlen[s][c]; d++)
        if (songorder[s][c][d] < MAX_PATT) used[songorder[s][c][d]] = 1;
  for (p = 0; p < MAX_PATT; p++)
    if ((!used[p]) && (isempty(p))) return p;
  return -1;
}

int arr_copypattern(int src)
{
  int p = arr_freepattern();

  if (p < 0) return -1;
  memcpy(pattern[p], pattern[src], sizeof pattern[p]);
  pattlen[p] = pattlen[src];
  return p;
}

static void emptypattern(int p, int rows)
{
  int c;

  memset(pattern[p], 0, sizeof pattern[p]);
  for (c = 0; c < rows; c++) pattern[p][c * 4] = REST;
  for (c = rows; c <= MAX_PATTROWS; c++) pattern[p][c * 4] = ENDPATT;
  pattlen[p] = rows;
}

void arr_discardpattern(int patt)
{
  if ((patt >= 0) && (patt < MAX_PATT)) emptypattern(patt, pattlen[patt] > 0 ? pattlen[patt] : 1);
}

int arr_newpattern(int rows)
{
  int p = arr_freepattern();

  if (p < 0) return -1;
  if (rows < 1) rows = 1;
  if (rows > MAX_PATTROWS) rows = MAX_PATTROWS;
  emptypattern(p, rows);
  return p;
}
