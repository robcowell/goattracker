#ifndef GAUDIO_H
#define GAUDIO_H

// Finishing and writing rendered audio (mono 16-bit), shared by the front
// ends. Engine side (no GTK).

// Fade the last seconds out to silence
void audio_fade(short *data, unsigned count, unsigned rate, unsigned seconds);

// The gain that brings the loudest sample to just below full scale, and
// applying a gain
double audio_normalgain(const short *data, unsigned count);
void audio_gain(short *data, unsigned count, double gain);

int audio_writewav(const char *path, const short *data, unsigned count, unsigned rate);

// MP3, through libmp3lame loaded when first needed (no build dependency):
// available when the library is installed
int audio_mp3available(void);
int audio_writemp3(const char *path, const short *data, unsigned count, unsigned rate, int kbps);

#endif
