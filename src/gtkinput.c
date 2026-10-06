//
// GOATTRACKER v2 GTK user interface: MIDI notes
//
// In jam mode MIDI notes play on any free channel (ginput.c); in edit mode
// a MIDI note is entered at the pattern cursor like a typed one.
//

#include "gtkui.h"

static int lastentered = -1;        // MIDI note last entered in edit mode

static void midinote(int midinote, int velocity)
{
  int note = midi_tonote(midinote);

  if (!velocity)
  {
    if (midinote == lastentered)
    {
      releasenote(epchn);
      lastentered = -1;
    }
    jam_noteoff(MIDIVOICE(midinote));
    return;
  }
  if (note < 0) return;
  if ((recordmode) && (editmode == EDIT_PATTERN))
  {
    shiftpressed = 0;
    undo_markcursor();
    pattern_enternote(note);
    undo_checkpoint(0);
    lastentered = midinote;
    grid_followcursor();
    ui_refresh();
  }
  else jam_noteon(MIDIVOICE(midinote), note, einum, epchn);
}

void ui_midiinit(void)
{
  midi_sethandler("GoatTracker", midinote);
}
