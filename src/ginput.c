//
// GOATTRACKER v2: playing notes live (jamming) and MIDI input
//
// Jammed notes, from the keyboard or a MIDI controller, are spread over the
// three channels and released when the key comes up.
//
// MIDI comes from the ALSA sequencer: the program appears as a client with
// an input port, connected to the chosen source (other sources can be
// connected with aconnect or a patchbay). Shared by the GTK edition and
// SidMonkey; uses GLib for the main loop but not GTK.
//

#include <alsa/asoundlib.h>
#include <glib.h>
#include <glib-unix.h>
#include "goattrk2.h"
#include "ginput.h"

//
// Jam voices
//

typedef struct
{
  unsigned id;                // what plays it (a key or MIDI note); 0 = free
  unsigned age;
} VOICE;

static VOICE voices[MAX_CHN];
static unsigned agecounter = 0;

void jam_noteon(unsigned id, int note, int instr, int firstchn)
{
  int c, i, best = -1;

  for (c = 0; c < MAX_CHN; c++)
    if (voices[c].id == id) return;     // key repeat

  // A free channel, starting from the cursor's; otherwise the oldest note
  // is cut off. Muted channels are left alone.
  for (i = 0; (i < MAX_CHN) && (best < 0); i++)
  {
    c = (firstchn + i) % MAX_CHN;
    if ((!voices[c].id) && (!chn[c].mute)) best = c;
  }
  if (best < 0)
  {
    for (c = 0; c < MAX_CHN; c++)
      if ((!chn[c].mute) && ((best < 0) || (voices[c].age < voices[best].age))) best = c;
  }
  if (best < 0) best = firstchn;

  voices[best].id = id;
  voices[best].age = ++agecounter;
  playtestnote(note, instr, best);
}

void jam_noteoff(unsigned id)
{
  int c;

  for (c = 0; c < MAX_CHN; c++)
  {
    if (voices[c].id == id)
    {
      releasenote(c);
      voices[c].id = 0;
    }
  }
}

void jam_releaseall(void)
{
  int c;

  for (c = 0; c < MAX_CHN; c++)
  {
    if (voices[c].id) releasenote(c);
    voices[c].id = 0;
  }
}

//
// MIDI input
//

static snd_seq_t *seq = NULL;
static int inport = -1;
static guint watches[8];
static int numwatches = 0;
static void (*notehandler)(int midinote, int velocity) = NULL;
static const char *client = "GoatTracker";

char midi_inputname[256] = "";

void midi_sethandler(const char *clientname, void (*handler)(int midinote, int velocity))
{
  client = clientname;
  notehandler = handler;
}

int midi_tonote(int midinote)
{
  int note = midinote - 12;

  if ((note < 0) || (note > LASTNOTE - FIRSTNOTE)) return -1;
  return FIRSTNOTE + note;
}

static gboolean onmidiready(gint fd, GIOCondition condition, gpointer data)
{
  snd_seq_event_t *ev;

  while ((seq) && (snd_seq_event_input(seq, &ev) >= 0))
  {
    switch (ev->type)
    {
      case SND_SEQ_EVENT_NOTEON:
      if (notehandler) notehandler(ev->data.note.note, ev->data.note.velocity);
      break;

      case SND_SEQ_EVENT_NOTEOFF:
      if (notehandler) notehandler(ev->data.note.note, 0);
      break;
    }
  }
  return G_SOURCE_CONTINUE;
}

static int midiopen(void)
{
  struct pollfd fds[8];
  int c, n;

  if (seq) return 1;
  if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_INPUT, SND_SEQ_NONBLOCK) < 0)
  {
    seq = NULL;
    return 0;
  }
  snd_seq_set_client_name(seq, client);
  inport = snd_seq_create_simple_port(seq, "Input", SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
    SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
  n = snd_seq_poll_descriptors(seq, fds, G_N_ELEMENTS(fds), POLLIN);
  for (c = 0; c < n; c++)
    watches[numwatches++] = g_unix_fd_add(fds[c].fd, G_IO_IN, onmidiready, NULL);
  return 1;
}

static void midiclose(void)
{
  int c;

  for (c = 0; c < numwatches; c++) g_source_remove(watches[c]);
  numwatches = 0;
  if (seq) snd_seq_close(seq);
  seq = NULL;
  inport = -1;
  jam_releaseall();
}

// Call func for each port that MIDI can be read from
static void foreachsource(snd_seq_t *handle, void (*func)(const char *name, int client, int port, void *data), void *data)
{
  snd_seq_client_info_t *cinfo;
  snd_seq_port_info_t *pinfo;
  unsigned need = SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ;

  snd_seq_client_info_alloca(&cinfo);
  snd_seq_port_info_alloca(&pinfo);
  snd_seq_client_info_set_client(cinfo, -1);
  while (snd_seq_query_next_client(handle, cinfo) >= 0)
  {
    int client = snd_seq_client_info_get_client(cinfo);

    if (client == snd_seq_client_id(handle)) continue;
    snd_seq_port_info_set_client(pinfo, client);
    snd_seq_port_info_set_port(pinfo, -1);
    while (snd_seq_query_next_port(handle, pinfo) >= 0)
    {
      unsigned caps = snd_seq_port_info_get_capability(pinfo);
      char name[256];

      if (((caps & need) != need) || (caps & SND_SEQ_PORT_CAP_NO_EXPORT)) continue;
      snprintf(name, sizeof name, "%s: %s", snd_seq_client_info_get_name(cinfo), snd_seq_port_info_get_name(pinfo));
      func(name, client, snd_seq_port_info_get_port(pinfo), data);
    }
  }
}

static void addname(const char *name, int client, int port, void *data)
{
  g_ptr_array_add((GPtrArray *)data, g_strdup(name));
}

// The names of the MIDI sources, as a NULL-terminated array to free with
// g_strfreev()
char **midi_listsources(void)
{
  GPtrArray *names = g_ptr_array_new();
  snd_seq_t *handle = seq;

  if ((handle) || (snd_seq_open(&handle, "default", SND_SEQ_OPEN_INPUT, SND_SEQ_NONBLOCK) >= 0))
  {
    foreachsource(handle, addname, names);
    if (handle != seq) snd_seq_close(handle);
  }
  g_ptr_array_add(names, NULL);
  return (char **)g_ptr_array_free(names, FALSE);
}

typedef struct
{
  const char *name;
  int client, port;
} FINDSOURCE;

static void findname(const char *name, int client, int port, void *data)
{
  FINDSOURCE *f = data;

  if ((f->client < 0) && (!strcmp(name, f->name)))
  {
    f->client = client;
    f->port = port;
  }
}

// Read MIDI from the named source ("" = off). Returns 0 if it can't be used.
int midi_setinput(const char *name)
{
  FINDSOURCE f = {name, -1, -1};

  g_strlcpy(midi_inputname, name, sizeof midi_inputname);
  midiclose();
  if (!name[0]) return 1;
  if (!midiopen()) return 0;
  foreachsource(seq, findname, &f);
  if ((f.client < 0) || (snd_seq_connect_from(seq, inport, f.client, f.port) < 0)) return 0;
  return 1;
}
