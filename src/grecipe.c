//
// GOATTRACKER v2: recipe instruments
//
// The programs follow readme.txt section 3.4: wavetable rows are a waveform
// and a note (relative, or absolute from $81), pulsetable rows set or sweep
// the pulse width, filtertable rows set the passband/resonance/voices and
// the cutoff, then sweep it. Slides and vibrato use note-independent
// speedtable rows (left side $80 set), so they sound alike at every pitch.
//

#include "goattrk2.h"
#include "ginfo.h"
#include "grecipe.h"

#define MAXPROG 24

typedef struct
{
  unsigned char l, r;
  int jump;          // r is a jump target: an index into the program
} PROGROW;

typedef struct
{
  int n;
  PROGROW row[MAXPROG];
} PROGRAM;

static RECIPE recipes[MAX_INSTR];
static unsigned char hasrecipe[MAX_INSTR];
static unsigned recipehash[MAX_INSTR];

//
// Presets
//

#define TONE(w, a, d, s, r) RK_TONE, w, a, d, s, r

const PRESET presets[] = {
  {"Pulse Bass", "Bass", "A round, punchy bass", {TONE(0x40, 0, 9, 10, 9), 0, RC_OFF, 1, 6, 2, 5, RF_OFF, 0, 0, 0, 0, RV_OFF, 0, 1, 0}},
  {"Saw Bass", "Bass", "A buzzy bass that closes up as it plays", {TONE(0x20, 0, 9, 9, 9), 0, RC_OFF, 1, 8, 0, 0, RF_LOW, 0x70, 6, -2, 40, RV_OFF, 0, 1, 0}},
  {"Plucked Bass", "Bass", "A triangle bass with a click", {TONE(0x10, 0, 10, 0, 9), 1, RC_OFF, 1, 8, 0, 0, RF_OFF, 0, 0, 0, 0, RV_OFF, 0, 1, 0}},
  {"Square Lead", "Lead", "A classic lead that wobbles gently", {TONE(0x40, 0, 9, 10, 10), 0, RC_OFF, 1, 4, 6, 3, RF_OFF, 0, 0, 0, 0, RV_NORMAL, 16, 1, 0}},
  {"Saw Lead", "Lead", "A bright, cutting lead", {TONE(0x20, 0, 9, 11, 10), 0, RC_OFF, 1, 8, 0, 0, RF_OFF, 0, 0, 0, 0, RV_NORMAL, 20, 1, 0}},
  {"Flute", "Lead", "A soft triangle that fades in", {TONE(0x10, 3, 9, 12, 10), 0, RC_OFF, 1, 8, 0, 0, RF_OFF, 0, 0, 0, 0, RV_GENTLE, 24, 1, 0}},
  {"Major Chord", "Chord", "Plays a major chord by flicking between notes", {TONE(0x40, 0, 9, 10, 10), 0, RC_MAJOR, 1, 6, 4, 4, RF_OFF, 0, 0, 0, 0, RV_OFF, 0, 1, 0}},
  {"Minor Chord", "Chord", "Plays a minor chord by flicking between notes", {TONE(0x40, 0, 9, 10, 10), 0, RC_MINOR, 1, 6, 4, 4, RF_OFF, 0, 0, 0, 0, RV_OFF, 0, 1, 0}},
  {"Power Chord", "Chord", "Root, fifth and octave on a saw", {TONE(0x20, 0, 9, 10, 10), 0, RC_POWER, 1, 8, 0, 0, RF_OFF, 0, 0, 0, 0, RV_OFF, 0, 1, 0}},
  {"Octave Arp", "Chord", "Jumps between a note and its octave", {TONE(0x40, 0, 9, 9, 9), 0, RC_OCTAVE, 2, 3, 0, 0, RF_OFF, 0, 0, 0, 0, RV_OFF, 0, 1, 0}},
  {"Slow Pad", "Pad", "Swells in slowly and sweeps", {TONE(0x40, 10, 10, 12, 11), 0, RC_OFF, 1, 5, 8, 2, RF_LOW, 0x50, 3, 1, 90, RV_GENTLE, 30, 0, 0}},
  {"Kick", "Drums", "A bass drum", {RK_KICK, 0, 0, 9, 0, 9, 0, RC_OFF, 1, 8, 0, 0, RF_OFF, 0, 0, 0, 0, RV_OFF, 0, 1, 0}},
  {"Snare", "Drums", "A snare drum", {RK_SNARE, 0, 0, 9, 0, 9, 0, RC_OFF, 1, 8, 0, 0, RF_OFF, 0, 0, 0, 0, RV_OFF, 0, 1, 0}},
  {"Closed Hi-Hat", "Drums", "A short hi-hat tick", {RK_HIHAT, 0, 0, 6, 0, 6, 0, RC_OFF, 1, 8, 0, 0, RF_OFF, 0, 0, 0, 0, RV_OFF, 0, 1, 0}},
  {"Open Hi-Hat", "Drums", "A ringing hi-hat", {RK_HIHAT, 0, 0, 9, 0, 9, 0, RC_OFF, 1, 8, 0, 0, RF_OFF, 0, 0, 0, 0, RV_OFF, 0, 1, 0}},
  {"Tom", "Drums", "A tom, tuned by the note played", {RK_TOM, 0, 0, 9, 0, 9, 0, RC_OFF, 1, 8, 0, 0, RF_OFF, 0, 0, 0, 0, RV_OFF, 0, 1, 0}},
  {"Noise Sweep", "Effects", "Wind-like noise through a moving filter", {TONE(0x80, 6, 9, 10, 10), 0, RC_OFF, 1, 8, 0, 0, RF_HIGH, 0x10, 6, 2, 100, RV_OFF, 0, 0, 0}},
};
const int numpresets = sizeof presets / sizeof presets[0];

//
// Program generation
//

static void addrow(PROGRAM *p, unsigned char l, unsigned char r)
{
  if (p->n >= MAXPROG) return;
  p->row[p->n].l = l;
  p->row[p->n].r = r;
  p->row[p->n].jump = 0;
  p->n++;
}

// A jump back to index target (0-based in the program), or a stop (-1)
static void addjump(PROGRAM *p, int target)
{
  addrow(p, 0xff, 0);
  if (target >= 0) p->row[p->n - 1].jump = target + 1;
}

static int clampnote(int v)
{
  if (v < 0x81) v = 0x81;
  if (v > 0xdf) v = 0xdf;
  return v;
}

static const signed char chords[RC_CHORDS][5] = {
  {0}, {3, 0, 4, 7}, {3, 0, 3, 7}, {2, 0, 12}, {3, 0, 7, 12}, {3, 0, 5, 7}, {4, 0, 4, 7, 11}, {4, 0, 3, 7, 10}};

static void wavetable(const RECIPE *rc, PROGRAM *p)
{
  int k = rc->pitch;

  switch (rc->kind)
  {
    case RK_KICK:
    {
      static const unsigned char drop[] = {0xb4, 0xa8, 0x9e, 0x96, 0x91, 0x8e};
      int c;
      addrow(p, 0x81, clampnote(0xd0 + k));
      for (c = 0; c < (int)sizeof drop; c++)
        addrow(p, c == (int)sizeof drop - 1 ? 0x10 : 0x11, clampnote(drop[c] + k));
      addjump(p, -1);
      return;
    }

    case RK_TOM:
      // Relative notes, so the note played tunes it
      addrow(p, 0x81, clampnote(0xc8 + k));
      addrow(p, 0x11, 0x0c);
      addrow(p, 0x11, 0x05);
      addrow(p, 0x11, 0x00);
      addrow(p, 0x11, 0x7d);
      addrow(p, 0x10, 0x7a);
      addjump(p, -1);
      return;

    case RK_SNARE:
      // readme.txt's snare: use with pulse width $800
      addrow(p, 0x81, clampnote(0xd0 + k));
      addrow(p, 0x41, clampnote(0xaa + k));
      addrow(p, 0x41, clampnote(0xa4 + k));
      addrow(p, 0x80, clampnote(0xd4 + k));
      addrow(p, 0x80, clampnote(0xd1 + k));
      addjump(p, -1);
      return;

    case RK_HIHAT:
      addrow(p, 0x81, clampnote(0xdf + k));
      addrow(p, 0x80, clampnote(0xdf + k));
      addjump(p, -1);
      return;
  }

  {
    unsigned char wave = (rc->wave & 0xf0) | 0x01;
    const signed char *chord = chords[rc->chord < RC_CHORDS ? rc->chord : 0];
    int speed = rc->chordspeed < 1 ? 1 : (rc->chordspeed > 8 ? 8 : rc->chordspeed);

    if (!(rc->wave & 0xf0)) wave = 0x41;
    // Noise doesn't combine with the other waveforms
    if (wave & 0x80) wave = 0x81;
    if (rc->click) addrow(p, 0x81, 0xdf);
    if (!chord[0])
    {
      addrow(p, wave, 0x00);
      addjump(p, -1);
    }
    else
    {
      // The first step sets the waveform; the loop only changes the note
      int c, loop;
      addrow(p, wave, chord[1]);
      loop = p->n;
      for (c = 2; c <= chord[0]; c++) addrow(p, speed - 1, chord[c]);
      addrow(p, speed - 1, chord[1]);
      addjump(p, loop);
    }
  }
}

static void pulsetable(const RECIPE *rc, PROGRAM *p)
{
  int width, depth, speed, time;

  if (rc->kind == RK_SNARE) width = 0x800;
  else if ((rc->kind != RK_TONE) || (!(rc->wave & 0x40))) return;
  else width = (rc->width ? rc->width : 8) << 8;
  if (width > 0xf00) width = 0xf00;

  if ((rc->kind != RK_TONE) || (!rc->pwm))
  {
    addrow(p, 0x80 | (width >> 8), width & 0xff);
    addjump(p, -1);
    return;
  }
  // Sweep between width - depth and width + depth, round and round
  depth = rc->pwm * 0x50;
  if (width - depth < 0x40) depth = width - 0x40;
  if (width + depth > 0xfc0) depth = 0xfc0 - width;
  speed = (rc->pwmspeed ? rc->pwmspeed : 4) * 6;
  time = 2 * depth / speed;
  while (time > 0x7f)
  {
    speed++;
    time = 2 * depth / speed;
  }
  if (time < 1) time = 1;
  addrow(p, 0x80 | ((width - depth) >> 8), (width - depth) & 0xff);
  addrow(p, time, speed);
  addrow(p, time, (unsigned char)(-speed));
  addjump(p, 1);
}

// Voices that play an instrument: those whose patterns set it
int recipe_usedvoices(int instrnum)
{
  int s, c, i, row, mask = 0;

  for (s = 0; s < MAX_SONGS; s++)
    for (c = 0; c < MAX_CHN; c++)
      for (i = 0; i < songlen[s][c]; i++)
      {
        int patt = songorder[s][c][i];
        if (patt >= MAX_PATT) continue;
        for (row = 0; row < pattlen[patt]; row++)
          if (pattern[patt][row * 4 + 1] == instrnum) mask |= 1 << c;
      }
  return mask;
}

static int usedvoices(int instrnum)
{
  int mask = recipe_usedvoices(instrnum);
  return mask ? mask : 7;
}

static void filtertable(const RECIPE *rc, int instrnum, PROGRAM *p)
{
  static const unsigned char passband[] = {0x80, 0x90, 0xa0, 0xc0};

  if ((rc->filter == RF_OFF) || (rc->filter > RF_HIGH)) return;
  addrow(p, passband[rc->filter], ((rc->resonance & 0x0f) << 4) | usedvoices(instrnum));
  addrow(p, 0x00, rc->cutoff);
  if ((rc->sweep) && (rc->sweeptime))
  {
    int t = rc->sweeptime;
    while (t > 0)
    {
      addrow(p, t > 0x7f ? 0x7f : t, (unsigned char)rc->sweep);
      t -= 0x7f;
    }
  }
  addjump(p, -1);
}

//
// Placing programs in the tables
//

static unsigned char others[MAX_TABLES][MAX_TABLELEN];
static unsigned char mine[MAX_TABLES][MAX_TABLELEN];

static int matches(int table, int start, const PROGRAM *p)
{
  int i;

  if (start + p->n > MAX_TABLELEN) return 0;
  for (i = 0; i < p->n; i++)
  {
    unsigned char r = p->row[i].jump ? start + p->row[i].jump : p->row[i].r;
    if ((ltable[table][start + i] != p->row[i].l) || (rtable[table][start + i] != r)) return 0;
  }
  return 1;
}

static int isfree(int table, int pos)
{
  if (mine[table][pos]) return 1;
  return (!others[table][pos]) && (!ltable[table][pos]) && (!rtable[table][pos]);
}

// Where a program goes (0-based), or -1 if there's no room
static int place(int table, const PROGRAM *p, int oldstart)
{
  int s, i;

  // The same program already in the table. Not for the filter: its voice
  // mask is updated for each instrument (recipe_updatefilters())
  for (s = 0; (table != FTBL) && (s + p->n <= MAX_TABLELEN); s++)
    if ((others[table][s]) && (matches(table, s, p))) return s;
  // Where it was, then the first free run
  if (oldstart >= 0)
  {
    for (i = 0; (i < p->n) && (oldstart + i < MAX_TABLELEN) && (isfree(table, oldstart + i)); i++);
    if (i == p->n) return oldstart;
  }
  for (s = 0; s + p->n <= MAX_TABLELEN; s++)
  {
    for (i = 0; (i < p->n) && (isfree(table, s + i)); i++);
    if (i == p->n) return s;
  }
  return -1;
}

static void writeprogram(int table, int start, const PROGRAM *p)
{
  int i;

  for (i = 0; i < p->n; i++)
  {
    ltable[table][start + i] = p->row[i].l;
    rtable[table][start + i] = p->row[i].jump ? start + p->row[i].jump : p->row[i].r;
  }
}

static int speedrow(unsigned char l, unsigned char r)
{
  int i;

  for (i = 0; i < MAX_TABLELEN; i++)
    if ((ltable[STBL][i] == l) && (rtable[STBL][i] == r)) return i;
  for (i = 0; i < MAX_TABLELEN; i++)
    if (isfree(STBL, i)) return i;
  return -1;
}

//
// Checksums of what was built, to notice changes made elsewhere
//

static unsigned hashbyte(unsigned h, unsigned char b)
{
  return (h ^ b) * 16777619u;
}

// The program a pointer starts, relative to where it starts
static unsigned hashprogram(unsigned h, int table, int ptr)
{
  int pos = ptr - 1, n;

  if (!ptr) return hashbyte(h, 0);
  for (n = 0; (n < MAXPROG * 2) && (pos < MAX_TABLELEN); n++, pos++)
  {
    unsigned char l = ltable[table][pos], r = rtable[table][pos];
    h = hashbyte(h, l);
    if (l == 0xff)
    {
      h = hashbyte(h, r ? (unsigned char)(r - ptr) : 0xff);
      break;
    }
    h = hashbyte(h, r);
    if (table == STBL) break;
  }
  return h;
}

static unsigned hashinstrument(int instrnum)
{
  const INSTR *in = &instr[instrnum];
  unsigned h = 2166136261u;
  int t;

  h = hashbyte(h, in->ad);
  h = hashbyte(h, in->sr);
  h = hashbyte(h, in->vibdelay);
  h = hashbyte(h, in->gatetimer);
  h = hashbyte(h, in->firstwave);
  for (t = 0; t < MAX_TABLES; t++) h = hashprogram(h, t, in->ptr[t]);
  return h;
}

const RECIPE *recipe_get(int instrnum)
{
  if ((instrnum < 1) || (instrnum >= MAX_INSTR) || (!hasrecipe[instrnum])) return NULL;
  if (recipehash[instrnum] != hashinstrument(instrnum))
  {
    hasrecipe[instrnum] = 0;
    return NULL;
  }
  return &recipes[instrnum];
}

void recipe_forget(int instrnum)
{
  if ((instrnum >= 0) && (instrnum < MAX_INSTR)) hasrecipe[instrnum] = 0;
}

void recipe_forgetall(void)
{
  memset(hasrecipe, 0, sizeof hasrecipe);
}

//
// Building
//

int recipe_build(int instrnum, const RECIPE *recipe, const char *name)
{
  static unsigned char before[MAX_TABLES][MAX_TABLELEN], after[MAX_TABLES][MAX_TABLELEN];
  static const unsigned char vib[][2] = {{0, 0}, {0x84, 0x05}, {0x83, 0x03}, {0x83, 0x02}};
  PROGRAM prog[MAX_TABLES - 1];
  int start[MAX_TABLES];
  INSTR *in = &instr[instrnum];
  RECIPE rc = *recipe;
  int t, i;

  if ((instrnum < 1) || (instrnum >= MAX_INSTR)) return 0;
  memset(prog, 0, sizeof prog);
  wavetable(&rc, &prog[WTBL]);
  pulsetable(&rc, &prog[PTBL]);
  filtertable(&rc, instrnum, &prog[FTBL]);

  // Rows only this instrument uses can be rewritten
  table_reachmap(others, instrnum);
  table_reachmap(before, -1);
  for (t = 0; t < MAX_TABLES; t++)
    for (i = 0; i < MAX_TABLELEN; i++)
      mine[t][i] = before[t][i] && !others[t][i];

  for (t = 0; t < MAX_TABLES - 1; t++)
  {
    start[t] = -1;
    if (!prog[t].n) continue;
    start[t] = place(t, &prog[t], ((in->ptr[t]) && (mine[t][in->ptr[t] - 1])) ? in->ptr[t] - 1 : -1);
    if (start[t] < 0) return 0;
  }
  start[STBL] = -1;
  if ((rc.vibrato) && (rc.vibrato <= RV_WIDE))
  {
    start[STBL] = speedrow(vib[rc.vibrato][0], vib[rc.vibrato][1]);
    if (start[STBL] < 0) return 0;
  }

  // Everything fits: write it
  for (t = 0; t < MAX_TABLES - 1; t++)
  {
    if (start[t] >= 0) writeprogram(t, start[t], &prog[t]);
    in->ptr[t] = start[t] + 1;
  }
  if (start[STBL] >= 0)
  {
    ltable[STBL][start[STBL]] = vib[rc.vibrato][0];
    rtable[STBL][start[STBL]] = vib[rc.vibrato][1];
  }
  in->ptr[STBL] = start[STBL] + 1;
  in->vibdelay = start[STBL] >= 0 ? (rc.vibdelay ? rc.vibdelay : 1) : 0;
  in->ad = ((rc.attack & 0xf) << 4) | (rc.decay & 0xf);
  in->sr = ((rc.sustain & 0xf) << 4) | (rc.release & 0xf);
  // As new instruments get it (ginstr.c clearinstr()): at most tempo - 1
  in->gatetimer = (multiplier ? 2 * multiplier : 1) | (rc.hardrestart ? 0 : 0x80);
  in->firstwave = 0x09;
  if ((name) && (!in->name[0])) strncpy(in->name, name, MAX_INSTRNAMELEN - 1);

  // Clear the old rows nothing runs any more, so they are free again
  table_reachmap(after, -1);
  for (t = 0; t < MAX_TABLES; t++)
    for (i = 0; i < MAX_TABLELEN; i++)
      if ((mine[t][i]) && (!after[t][i])) ltable[t][i] = rtable[t][i] = 0;

  recipes[instrnum] = rc;
  hasrecipe[instrnum] = 1;
  recipehash[instrnum] = hashinstrument(instrnum);
  return 1;
}

int recipe_updatefilters(void)
{
  int c, changed = 0;

  for (c = 1; c < MAX_INSTR; c++)
  {
    const RECIPE *rc = recipe_get(c);
    int pos;
    unsigned char r;

    if ((!rc) || (rc->filter == RF_OFF) || (!instr[c].ptr[FTBL])) continue;
    pos = instr[c].ptr[FTBL] - 1;
    r = ((rc->resonance & 0x0f) << 4) | usedvoices(c);
    if (rtable[FTBL][pos] == r) continue;
    rtable[FTBL][pos] = r;
    recipehash[c] = hashinstrument(c);
    changed = 1;
  }
  return changed;
}

//
// Song file chunk: id, version, count, then for each recipe the instrument
// number, the recipe's size and bytes, and the checksum (little-endian)
//

void recipe_writechunk(FILE *handle)
{
  int c, n = 0;

  for (c = 1; c < MAX_INSTR; c++)
    if (recipe_get(c)) n++;
  if (!n) return;
  fputc(CHUNK_RECIPES, handle);
  fputc(1, handle);
  fputc(n, handle);
  for (c = 1; c < MAX_INSTR; c++)
  {
    if (!recipe_get(c)) continue;
    fputc(c, handle);
    fputc(sizeof(RECIPE), handle);
    fwrite(&recipes[c], sizeof(RECIPE), 1, handle);
    fputc(recipehash[c] & 0xff, handle);
    fputc((recipehash[c] >> 8) & 0xff, handle);
    fputc((recipehash[c] >> 16) & 0xff, handle);
    fputc((recipehash[c] >> 24) & 0xff, handle);
  }
}

void recipe_readchunk(FILE *handle)
{
  int version = fgetc(handle), n = fgetc(handle), c;

  recipe_forgetall();
  if ((version != 1) || (n < 0)) return;
  for (c = 0; c < n; c++)
  {
    int num = fgetc(handle), size = fgetc(handle), i;
    RECIPE rc;
    unsigned hash = 0;

    if ((num < 0) || (size < 0)) return;
    memset(&rc, 0, sizeof rc);
    for (i = 0; i < size; i++)
    {
      int b = fgetc(handle);
      if (b < 0) return;
      if (i < (int)sizeof rc) ((unsigned char *)&rc)[i] = b;
    }
    for (i = 0; i < 4; i++)
    {
      int b = fgetc(handle);
      if (b < 0) return;
      hash |= (unsigned)b << (i * 8);
    }
    if ((num < 1) || (num >= MAX_INSTR)) continue;
    recipes[num] = rc;
    recipehash[num] = hash;
    hasrecipe[num] = 1;
  }
}
