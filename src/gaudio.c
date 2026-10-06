//
// GOATTRACKER v2: finishing and writing rendered audio
//

#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include "gaudio.h"

void audio_fade(short *data, unsigned count, unsigned rate, unsigned seconds)
{
  unsigned fade = seconds * rate, c;

  if (fade > count) fade = count;
  for (c = 0; c < fade; c++)
  {
    unsigned i = count - fade + c;
    data[i] = (short)(data[i] * (double)(fade - c) / fade);
  }
}

double audio_normalgain(const short *data, unsigned count)
{
  int peak = 1;
  unsigned c;

  for (c = 0; c < count; c++)
    if (abs(data[c]) > peak) peak = abs(data[c]);
  return 32000.0 / peak;
}

void audio_gain(short *data, unsigned count, double gain)
{
  unsigned c;

  for (c = 0; c < count; c++)
  {
    double v = data[c] * gain;
    data[c] = (short)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
  }
}

static void writele16(FILE *f, unsigned v) { fputc(v & 0xff, f); fputc(v >> 8, f); }
static void writele32(FILE *f, unsigned v) { writele16(f, v & 0xffff); writele16(f, v >> 16); }

int audio_writewav(const char *path, const short *data, unsigned count, unsigned rate)
{
  FILE *f = fopen(path, "wb");
  unsigned bytes = count * 2;

  if (!f) return 0;
  fwrite("RIFF", 4, 1, f);
  writele32(f, 36 + bytes);
  fwrite("WAVEfmt ", 8, 1, f);
  writele32(f, 16);
  writele16(f, 1);          // PCM
  writele16(f, 1);          // mono
  writele32(f, rate);
  writele32(f, rate * 2);
  writele16(f, 2);
  writele16(f, 16);
  fwrite("data", 4, 1, f);
  writele32(f, bytes);
  fwrite(data, 2, count, f);
  return fclose(f) == 0;
}

//
// MP3 via libmp3lame's public API (lame.h), declared here
//

typedef void *LAME;
static struct
{
  int tried;
  void *lib;
  LAME (*init)(void);
  int (*set_num_channels)(LAME, int);
  int (*set_in_samplerate)(LAME, int);
  int (*set_brate)(LAME, int);
  int (*set_mode)(LAME, int);
  int (*set_quality)(LAME, int);
  int (*init_params)(LAME);
  int (*encode_buffer)(LAME, const short *, const short *, int, unsigned char *, int);
  int (*encode_flush)(LAME, unsigned char *, int);
  int (*close)(LAME);
} lame;

#define LAME_MONO 3

static int loadlame(void)
{
  if (lame.tried) return lame.lib != NULL;
  lame.tried = 1;
  lame.lib = dlopen("libmp3lame.so.0", RTLD_NOW);
  if (!lame.lib) return 0;
  *(void **)&lame.init = dlsym(lame.lib, "lame_init");
  *(void **)&lame.set_num_channels = dlsym(lame.lib, "lame_set_num_channels");
  *(void **)&lame.set_in_samplerate = dlsym(lame.lib, "lame_set_in_samplerate");
  *(void **)&lame.set_brate = dlsym(lame.lib, "lame_set_brate");
  *(void **)&lame.set_mode = dlsym(lame.lib, "lame_set_mode");
  *(void **)&lame.set_quality = dlsym(lame.lib, "lame_set_quality");
  *(void **)&lame.init_params = dlsym(lame.lib, "lame_init_params");
  *(void **)&lame.encode_buffer = dlsym(lame.lib, "lame_encode_buffer");
  *(void **)&lame.encode_flush = dlsym(lame.lib, "lame_encode_flush");
  *(void **)&lame.close = dlsym(lame.lib, "lame_close");
  if ((!lame.init) || (!lame.set_num_channels) || (!lame.set_in_samplerate) || (!lame.set_brate) ||
    (!lame.set_mode) || (!lame.set_quality) || (!lame.init_params) || (!lame.encode_buffer) ||
    (!lame.encode_flush) || (!lame.close))
  {
    dlclose(lame.lib);
    lame.lib = NULL;
  }
  return lame.lib != NULL;
}

int audio_mp3available(void)
{
  return loadlame();
}

int audio_writemp3(const char *path, const short *data, unsigned count, unsigned rate, int kbps)
{
  enum {CHUNK = 8192};
  unsigned char *out;
  int outsize = CHUNK * 5 / 4 + 7200, ok = 1;
  unsigned pos;
  LAME gf;
  FILE *f;

  if (!loadlame()) return 0;
  if (!(f = fopen(path, "wb"))) return 0;
  gf = lame.init();
  if (!gf)
  {
    fclose(f);
    return 0;
  }
  lame.set_num_channels(gf, 1);
  lame.set_in_samplerate(gf, rate);
  lame.set_brate(gf, kbps);
  lame.set_mode(gf, LAME_MONO);
  lame.set_quality(gf, 2);
  out = malloc(outsize);
  if ((!out) || (lame.init_params(gf) < 0)) ok = 0;
  for (pos = 0; (ok) && (pos < count); pos += CHUNK)
  {
    int n = (count - pos < CHUNK) ? count - pos : CHUNK;
    int bytes = lame.encode_buffer(gf, data + pos, data + pos, n, out, outsize);
    if (bytes < 0) ok = 0;
    else if (bytes) ok = fwrite(out, 1, bytes, f) == (size_t)bytes;
  }
  if (ok)
  {
    int bytes = lame.encode_flush(gf, out, outsize);
    if (bytes > 0) ok = fwrite(out, 1, bytes, f) == (size_t)bytes;
  }
  lame.close(gf);
  free(out);
  if (fclose(f)) ok = 0;
  return ok;
}
