#ifndef GINPUT_H
#define GINPUT_H

// Playing notes live (jamming) and MIDI input, shared by the front ends.
// Needs GLib (MIDI is read in the main loop) and ALSA, but not GTK.

// Polyphonic jamming: notes go to a free unmuted voice, starting from
// firstchn, and cut off the oldest one when all are busy. id identifies what
// plays the note (a key or a MIDI note), so it can be released.
void jam_noteon(unsigned id, int note, int instr, int firstchn);
void jam_noteoff(unsigned id);
void jam_releaseall(void);

// Whether any jammed note is held
int jam_active(void);

// MIDI notes from the ALSA sequencer. The program appears as a client with
// an input port, connected to the chosen source. The handler is called in
// the main loop for each note on (velocity > 0) and note off (velocity 0).
#define MIDIVOICE(n) (0x10000 | (n))

extern char midi_inputname[256];
void midi_sethandler(const char *clientname, void (*handler)(int midinote, int velocity));

// The names of the MIDI sources, as a NULL-terminated array to free with
// g_strfreev()
char **midi_listsources(void);

// Read MIDI from the named source ("" = off). Returns 0 if it can't be used.
int midi_setinput(const char *name);

// The pattern note (FIRSTNOTE-based) for a MIDI note: middle C (60) is
// C-4. -1 if out of range.
int midi_tonote(int midinote);

#endif
