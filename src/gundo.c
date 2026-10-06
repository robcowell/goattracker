//
// GOATTRACKER v2 undo/redo and unsaved-changes tracking
//

#define GUNDO_C

#include "goattrk2.h"
#include "gundo.h"

#define MAX_UNDO 500
#define MAX_BLOCK 4096            // larger than any single block below

// Song data blocks that are compared and stored independently
#define REGION_INSTR 0
#define REGION_LTABLE 1
#define REGION_RTABLE 2
#define REGION_NAMES 3
#define REGION_ORDER 4
#define REGION_PATTERN (REGION_ORDER + MAX_SONGS)
#define NUMREGIONS (REGION_PATTERN + MAX_PATT)

typedef struct
{
  int editmode;
  int epnum[MAX_CHN];
  int eppos, epchn, epcolumn;
  int esnum, eschn, eseditpos, escolumn;
  int einum;
  int etnum, etpos, etcolumn;
} CURSOR;

typedef struct
{
  int id;
  int coalesce;
  CURSOR cursor;
  int nregions;
  int region[NUMREGIONS];
  unsigned char *data;      // the stored blocks, one after another
} UNDOENTRY;

typedef struct
{
  unsigned char *ptr;
  int size;
  int offset;               // in the shadow copy
} REGION;

static REGION regions[NUMREGIONS];
static unsigned char *shadow = NULL;
static UNDOENTRY *undostack[MAX_UNDO];
static UNDOENTRY *redostack[MAX_UNDO];
static int undocount = 0;
static int redocount = 0;
static int nextid = 1;
static int savedid = 0;
static unsigned version = 0;     // changes whenever the song data does
static CURSOR pendingcursor;
static int cursorpending = 0;

static void setregion(int r, void *ptr, int size, int *offset)
{
  regions[r].ptr = ptr;
  regions[r].size = size;
  regions[r].offset = *offset;
  *offset += size;
}

static void initregions(void)
{
  int c, offset = 0;

  if (shadow) return;
  setregion(REGION_INSTR, instr, sizeof instr, &offset);
  setregion(REGION_LTABLE, ltable, sizeof ltable, &offset);
  setregion(REGION_RTABLE, rtable, sizeof rtable, &offset);
  setregion(REGION_NAMES, NULL, 3*MAX_STR, &offset);
  for (c = 0; c < MAX_SONGS; c++)
    setregion(REGION_ORDER + c, songorder[c], sizeof songorder[c], &offset);
  for (c = 0; c < MAX_PATT; c++)
    setregion(REGION_PATTERN + c, pattern[c], sizeof pattern[c], &offset);
  shadow = malloc(offset);
}

// The three name strings are separate arrays; keep them in one block
static void copynames(unsigned char *dest, int tolive)
{
  if (tolive)
  {
    memcpy(songname, dest, MAX_STR);
    memcpy(authorname, dest + MAX_STR, MAX_STR);
    memcpy(copyrightname, dest + 2*MAX_STR, MAX_STR);
  }
  else
  {
    memcpy(dest, songname, MAX_STR);
    memcpy(dest + MAX_STR, authorname, MAX_STR);
    memcpy(dest + 2*MAX_STR, copyrightname, MAX_STR);
  }
}

static int regionsize(int r)
{
  return regions[r].size;
}

// Copy a block's live contents to dest
static void readregion(int r, unsigned char *dest)
{
  if (r == REGION_NAMES) copynames(dest, 0);
  else memcpy(dest, regions[r].ptr, regions[r].size);
}

static void writeregion(int r, unsigned char *src)
{
  if (r == REGION_NAMES) copynames(src, 1);
  else memcpy(regions[r].ptr, src, regions[r].size);
}

static unsigned char *shadowof(int r)
{
  return shadow + regions[r].offset;
}

static void getcursor(CURSOR *c)
{
  int i;

  c->editmode = editmode;
  for (i = 0; i < MAX_CHN; i++) c->epnum[i] = epnum[i];
  c->eppos = eppos;
  c->epchn = epchn;
  c->epcolumn = epcolumn;
  c->esnum = esnum;
  c->eschn = eschn;
  c->eseditpos = eseditpos;
  c->escolumn = escolumn;
  c->einum = einum;
  c->etnum = etnum;
  c->etpos = etpos;
  c->etcolumn = etcolumn;
}

static void setcursor(const CURSOR *c)
{
  int i;

  editmode = c->editmode;
  for (i = 0; i < MAX_CHN; i++) epnum[i] = c->epnum[i];
  esnum = c->esnum;
  epchn = c->epchn;
  eppos = c->eppos;
  epcolumn = c->epcolumn;
  if (eppos > pattlen[epnum[epchn]]) eppos = pattlen[epnum[epchn]];
  eschn = c->eschn;
  eseditpos = c->eseditpos;
  escolumn = c->escolumn;
  if (eseditpos > songlen[esnum][eschn] + 1) eseditpos = songlen[esnum][eschn] + 1;
  einum = c->einum;
  etnum = c->etnum;
  etpos = c->etpos;
  etcolumn = c->etcolumn;
  epmarkchn = -1;
  esmarkchn = -1;
  etmarknum = -1;
  validatetableview();
}

static void freeentry(UNDOENTRY *e)
{
  free(e->data);
  free(e);
}

static void clearstack(UNDOENTRY **stack, int *count)
{
  while (*count) freeentry(stack[--*count]);
}

static void snapshot(void)
{
  int r;

  for (r = 0; r < NUMREGIONS; r++) readregion(r, shadowof(r));
}

void undo_reset(void)
{
  initregions();
  clearstack(undostack, &undocount);
  clearstack(redostack, &redocount);
  snapshot();
  savedid = 0;
  cursorpending = 0;
  version++;
}

void undo_markcursor(void)
{
  getcursor(&pendingcursor);
  cursorpending = 1;
}

int undo_checkpoint(int coalesce)
{
  unsigned char block[MAX_BLOCK];
  int changed[NUMREGIONS];
  int nchanged = 0;
  int r, size, offset;
  UNDOENTRY *e;
  CURSOR cursor;

  initregions();
  if (cursorpending) cursor = pendingcursor;
  else getcursor(&cursor);
  cursorpending = 0;

  for (r = 0; r < NUMREGIONS; r++)
  {
    readregion(r, block);
    if (memcmp(block, shadowof(r), regionsize(r))) changed[nchanged++] = r;
  }
  if (!nchanged) return 0;

  // Merge into the previous step: keep its older copy of each block
  if ((coalesce) && (undocount) && (!redocount) && (undostack[undocount-1]->coalesce == coalesce) &&
      (undostack[undocount-1]->id != savedid))
  {
    UNDOENTRY *top = undostack[undocount-1];
    int c, d;

    size = 0;
    for (c = 0; c < top->nregions; c++) size += regionsize(top->region[c]);
    for (c = 0; c < nchanged; c++)
    {
      for (d = 0; d < top->nregions; d++) if (top->region[d] == changed[c]) break;
      if (d == top->nregions)
      {
        top->data = realloc(top->data, size + regionsize(changed[c]));
        memcpy(top->data + size, shadowof(changed[c]), regionsize(changed[c]));
        size += regionsize(changed[c]);
        top->region[top->nregions++] = changed[c];
      }
      readregion(changed[c], shadowof(changed[c]));
    }
    version++;
    return 1;
  }

  e = malloc(sizeof(UNDOENTRY));
  e->id = nextid++;
  e->coalesce = coalesce;
  e->cursor = cursor;
  e->nregions = nchanged;
  size = 0;
  for (r = 0; r < nchanged; r++) size += regionsize(changed[r]);
  e->data = malloc(size);
  offset = 0;
  for (r = 0; r < nchanged; r++)
  {
    e->region[r] = changed[r];
    // The shadow holds the block as it was before this step
    memcpy(e->data + offset, shadowof(changed[r]), regionsize(changed[r]));
    offset += regionsize(changed[r]);
    readregion(changed[r], shadowof(changed[r]));
  }

  clearstack(redostack, &redocount);
  if (undocount == MAX_UNDO)
  {
    freeentry(undostack[0]);
    memmove(&undostack[0], &undostack[1], (MAX_UNDO-1) * sizeof(UNDOENTRY *));
    undocount--;
  }
  undostack[undocount++] = e;
  version++;
  return 1;
}

// Exchange the entry's blocks with the live song data, so the same entry
// can be applied again in the other direction
static void swapentry(UNDOENTRY *e)
{
  unsigned char block[MAX_BLOCK];
  int r, offset = 0;

  for (r = 0; r < e->nregions; r++)
  {
    int region = e->region[r];
    int size = regionsize(region);

    readregion(region, block);
    writeregion(region, e->data + offset);
    memcpy(e->data + offset, block, size);
    readregion(region, shadowof(region));
    offset += size;
  }
  countpatternlengths();
  setcursor(&e->cursor);
  version++;
}

int undo_undo(void)
{
  UNDOENTRY *e;

  // Keep anything not yet recorded (should not normally happen)
  undo_checkpoint(0);
  if (!undocount) return 0;
  e = undostack[--undocount];
  swapentry(e);
  redostack[redocount++] = e;
  return 1;
}

int undo_redo(void)
{
  UNDOENTRY *e;

  if (!redocount) return 0;
  e = redostack[--redocount];
  swapentry(e);
  undostack[undocount++] = e;
  return 1;
}

int undo_canundo(void)
{
  return undocount > 0;
}

int undo_canredo(void)
{
  return redocount > 0;
}

static int currentid(void)
{
  return undocount ? undostack[undocount-1]->id : 0;
}

int undo_isdirty(void)
{
  return currentid() != savedid;
}

unsigned undo_version(void)
{
  return version;
}

void undo_marksaved(void)
{
  undo_checkpoint(0);
  savedid = currentid();
}
