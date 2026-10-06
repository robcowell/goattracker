//
// GOATTRACKER v2 offline rendering
//
// Drives the editor playroutine and the SID emulation directly, the same
// way the audio callback does (one playroutine() call per frame followed by
// sid_fillbuffer() for that frame's samples), but without waiting for the
// sound card. Used for WAV export and for measuring the song length.
//

#define GRENDER_C

#include "goattrk2.h"
#include "grender.h"

#define COUNTOF(a) (sizeof(a) / sizeof((a)[0]))

static int rendering = 0;
static int silentrender = 0;
static int finished = 0;
static int looptarget = 1;
static long frames = 0;
static long maxframes = 0;
static int rate = 0;
static double samplesperframe = 0;
static double fraction = 0;

static void resetsid(void)
{
  sid_init(rate, sidmodel, ntsc, interpolate & 1, customclockrate, interpolate >> 1);
}

// The SID's output level jumps when the master volume is first set, which
// would put a click at the start of every render. Set the volume the
// playroutine starts with and let the emulation settle for half a second.
static void prerollsid(void)
{
  short scratch[4096];
  int left = rate / 2;

  sidreg[0x18] = masterfader;
  while (left > 0)
  {
    int n = (left > (int)COUNTOF(scratch)) ? (int)COUNTOF(scratch) : left;
    sid_fillbuffer(scratch, n);
    left -= n;
  }
}

int render_begin(int subtune, int loops, int maxseconds, int silent)
{
  if (rendering) return 0;

  // Keep the audio callback out until render_end()
  SDL_PauseAudio(1);
  rendering = 1;
  silentrender = silent;
  finished = 0;
  frames = 0;
  fraction = 0;
  looptarget = (loops < 1) ? 1 : loops;
  rate = playspeed ? playspeed : (int)mr;
  if (!framerate) framerate = PALFRAMERATE;
  samplesperframe = (double)rate / framerate;
  maxframes = (long)maxseconds * framerate;

  stopsong();
  playroutine();
  if (!silent)
  {
    resetsid();
    prerollsid();
  }
  initsong(subtune, PLAY_BEGINNING);
  return 1;
}

static int songended(void)
{
  int c;

  if ((!isplaying()) || (frames >= maxframes)) return 1;
  for (c = 0; c < MAX_CHN; c++)
    if (songloops[c] < looptarget) return 0;
  return 1;
}

int render_run(short *buf, int maxsamples)
{
  int written = 0;

  if ((!rendering) || (finished)) return 0;
  while ((maxsamples - written) > (int)samplesperframe + 1)
  {
    int n;

    playroutine();
    frames++;
    n = (int)(samplesperframe + fraction);
    fraction = samplesperframe + fraction - n;
    if ((!silentrender) && (buf)) sid_fillbuffer(buf + written, n);
    written += n;
    if (songended())
    {
      finished = 1;
      break;
    }
  }
  return written;
}

double render_seconds(void)
{
  return framerate ? (double)frames / framerate : 0;
}

int render_finished(void)
{
  return finished;
}

int render_rate(void)
{
  return rate;
}

void render_end(void)
{
  if (!rendering) return;
  stopsong();
  playroutine();
  resettime();
  if (!silentrender) resetsid();
  rendering = 0;
  SDL_PauseAudio(0);
}

double render_songlength(int subtune, int maxseconds)
{
  double seconds;
  int ended;

  if (!render_begin(subtune, 1, maxseconds, 1)) return -1;
  while (render_run(NULL, 1 << 30)) ;
  ended = (frames < maxframes);
  seconds = render_seconds();
  render_end();
  return ended ? seconds : -1;
}
