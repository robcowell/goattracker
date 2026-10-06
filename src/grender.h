#ifndef GRENDER_H
#define GRENDER_H

// Offline rendering: play the song through the editor's playroutine as fast
// as possible, with or without the SID emulation. Live playback is stopped
// and the audio device paused until render_end().

// Start rendering subtune from its beginning. The render ends when every
// channel has looped the given number of times, the song stops, or after
// maxseconds. silent = only run the playroutine (to measure the song).
int render_begin(int subtune, int loops, int maxseconds, int silent);

// Render whole frames into buf (mono 16-bit at render_rate()) while they
// fit. Returns the number of samples written; 0 when the render has ended.
int render_run(short *buf, int maxsamples);

// Seconds rendered so far, and the end of the render
double render_seconds(void);
int render_finished(void);
int render_rate(void);
void render_end(void);

// Length of a subtune in seconds (one pass through every channel's
// orderlist), or a negative value if it doesn't end within maxseconds
double render_songlength(int subtune, int maxseconds);

#endif
