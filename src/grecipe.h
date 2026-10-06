#ifndef GRECIPE_H
#define GRECIPE_H

// Recipe instruments: an instrument described in plain terms (waveform,
// envelope, chord, pulse sweep, filter, vibrato, or one of a few drums),
// from which recipe_build() makes the instrument and its wave, pulse,
// filter and speedtable programs. Engine side (no GTK).
//
// Recipes are saved with the song in a trailing chunk (after GoatTracker
// Ultra's, so readers that stop at the first chunk they don't know never
// see it), with a checksum of what was built. An instrument changed since
// (in GoatTracker, say) no longer matches, and is treated as custom.

#define RK_TONE 0
#define RK_KICK 1
#define RK_SNARE 2
#define RK_HIHAT 3
#define RK_TOM 4
#define RK_KINDS 5

#define RC_OFF 0
#define RC_MAJOR 1
#define RC_MINOR 2
#define RC_OCTAVE 3
#define RC_POWER 4
#define RC_SUS4 5
#define RC_MAJ7 6
#define RC_MIN7 7
#define RC_CHORDS 8

#define RF_OFF 0
#define RF_LOW 1
#define RF_BAND 2
#define RF_HIGH 3

#define RV_OFF 0
#define RV_GENTLE 1
#define RV_NORMAL 2
#define RV_WIDE 3

typedef struct
{
  unsigned char kind;        // RK_
  unsigned char wave;        // tone: waveform bits ($10 triangle, $20 saw, $40 pulse, $80 noise)
  unsigned char attack, decay, sustain, release;   // 0-15
  unsigned char click;       // tone: a short noise click at the start
  unsigned char chord;       // tone: RC_ arpeggio chord
  unsigned char chordspeed;  // frames per arpeggio step, 1-8
  unsigned char width;       // pulse width, 1-15 ($100 steps; 8 = square)
  unsigned char pwm;         // pulse width sweep depth, 0 = none, 1-15
  unsigned char pwmspeed;    // 1-15
  unsigned char filter;      // RF_
  unsigned char cutoff;      // 0-255
  unsigned char resonance;   // 0-15
  signed char sweep;         // cutoff change per frame
  unsigned char sweeptime;   // frames the sweep lasts (0 = none)
  unsigned char vibrato;     // RV_
  unsigned char vibdelay;    // frames before the vibrato starts
  unsigned char hardrestart; // reset the envelope before each note (crisper attacks)
  signed char pitch;         // drums: semitones up or down
  unsigned char reserved[5];
} RECIPE;

typedef struct
{
  const char *name;
  const char *category;
  const char *description;
  RECIPE recipe;
} PRESET;

extern const PRESET presets[];
extern const int numpresets;

// The recipe each instrument was built from (NULL = none, or changed since)
const RECIPE *recipe_get(int instrnum);

// Build an instrument from a recipe, reusing table rows: its own old
// programs, identical programs elsewhere, or free rows. The name is set if
// the instrument has none. Returns 0, changing nothing, if the tables are
// full.
int recipe_build(int instrnum, const RECIPE *recipe, const char *name);

// The filter's voice bitmask follows the voices each recipe instrument is
// used on; call after editing the song. Returns 1 if anything changed.
int recipe_updatefilters(void);

// Forget an instrument's recipe (it was cleared or replaced)
void recipe_forget(int instrnum);
void recipe_forgetall(void);

// Song file chunk
#define CHUNK_RECIPES 0xc5
void recipe_writechunk(FILE *handle);
void recipe_readchunk(FILE *handle);

#endif
