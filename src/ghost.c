//
// GOATTRACKER v2: engine host layer for front ends other than the tracker
//

#include "goattrk2.h"
#include "gundo.h"
#include "ghost.h"

static int validsongfile(const char *path)
{
  char header[4] = {0};
  FILE *handle = fopen(path, "rb");
  int ok;

  if (!handle) return 0;
  ok = (fread(header, 4, 1, handle) == 1) && (!memcmp(header, "GTS", 3)) &&
    (((header[3] >= '2') && (header[3] <= '5')) || (header[3] == '!'));
  fclose(handle);
  return ok;
}

// The engine loads bare file names from the current directory
static int enterfolder(const char *path, char *name)
{
  char dir[MAX_PATHNAME];
  const char *base = strrchr(path, '/');

  if (!base)
  {
    if (strlen(path) >= MAX_FILENAME) return 0;
    strcpy(name, path);
    return 1;
  }
  if ((strlen(base + 1) >= MAX_FILENAME) || (base - path >= MAX_PATHNAME)) return 0;
  memcpy(dir, path, base - path);
  dir[base - path] = 0;
  if (chdir(base == path ? "/" : dir)) return 0;
  strcpy(name, base + 1);
  strcpy(songpath, base == path ? "/" : dir);
  return 1;
}

int host_loadsong(const char *path)
{
  char name[MAX_FILENAME];

  if (!validsongfile(path)) return HOST_LOAD_NOTSONG;
  if (!enterfolder(path, name)) return HOST_LOAD_BADPATH;
  stopsong();
  strcpy(songfilename, name);
  loadsong();
  if (loadresult != LOAD_OK) return loadresult;
  undo_reset();
  // Songs saved by GoatTracker Ultra (and the GTK edition) carry their
  // playback settings
  if (songsettingsloaded) host_restartsound();
  return LOAD_OK;
}

int host_savesong(const char *path)
{
  char name[MAX_FILENAME];

  if (!enterfolder(path, name)) return 0;
  strcpy(songfilename, name);
  if (!savesong()) return 0;
  undo_marksaved();
  return 1;
}

int host_restartsound(void)
{
  return sound_init(b, mr, writer, hardsid, sidmodel, ntsc, multiplier, catweasel, interpolate, customclockrate);
}

void host_play(int subtune)
{
  esnum = subtune;
  initsong(subtune, PLAY_BEGINNING);
}

int host_playfrom(int subtune, int chnum, int songpos)
{
  esnum = subtune;
  return render_seek(subtune, chnum, songpos, 30 * 60);
}

void host_stop(void)
{
  stopsong();
}

void host_preview(int note, int ins, int chnum)
{
  playtestnote(note, ins, chnum);
}

void host_release(int chnum)
{
  releasenote(chnum);
}

void host_lock(void)
{
  SDL_LockAudio();
}

void host_unlock(void)
{
  SDL_UnlockAudio();
}
