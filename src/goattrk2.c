//
// GOATTRACKER v2.77
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//

#define GOATTRK2_C

#ifdef __WIN32__
#include <windows.h>
#endif

#include "goattrk2.h"
#include "bme.h"
#include "gui.h"

int editmode = EDIT_PATTERN;
int recordmode = 1;
int followplay = 0;
int hexnybble = -1;
int stepsize = 4;
int autoadvance = 0;
int defaultpatternlength = 64;
int soundinitfailed = 0;
int starthelp = 0;

// Current keypress, set by the GUI before it calls docommand()
int key = 0;
int rawkey = 0;
int shiftpressed = 0;

unsigned keypreset = KEY_TRACKER;
unsigned playerversion = 0;
int fileformat = FORMAT_PRG;
int zeropageadr = 0xfc;
int playeradr = 0x1000;
unsigned sidmodel = 0;
unsigned multiplier = 1;
unsigned adparam = 0x0f00;
unsigned ntsc = 0;
unsigned patterndispmode = 0;
unsigned sidaddress = 0xd400;
unsigned finevibrato = 1;
unsigned optimizepulse = 1;
unsigned optimizerealtime = 1;
unsigned customclockrate = 0;
unsigned usefinevib = 0;
unsigned b = DEFAULTBUF;
unsigned mr = DEFAULTMIXRATE;
unsigned writer = 0;
unsigned hardsid = 0;
unsigned catweasel = 0;
unsigned interpolate = 0;
unsigned residdelay = 0;
unsigned hardsidbufinteractive = 20;
unsigned hardsidbufplayback = 400;
float basepitch = 0.0f;
float equaldivisionsperoctave = 12.0f;
int tuningcount = 0;
double tuning[96];
unsigned bigwindow = 1;
int win_fullscreen = 0;

char configbuf[MAX_PATHNAME];
char loadedsongfilename[MAX_FILENAME];
char songfilename[MAX_FILENAME];
char songfilter[MAX_FILENAME];
char songpath[MAX_PATHNAME];
char instrfilename[MAX_FILENAME];
char instrfilter[MAX_FILENAME];
char instrpath[MAX_PATHNAME];
char packedpath[MAX_PATHNAME];

extern char *notename[];
char *programname = "$VER: GoatTracker v2.77";
char specialnotenames[186];
char scalatuningfilepath[MAX_PATHNAME];
char tuningname[64];

char textbuffer[MAX_PATHNAME];

unsigned char hexkeytbl[] = {'0', '1', '2', '3', '4', '5', '6', '7',
  '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

extern unsigned char datafile[];

char* usage[] = {
    "Usage: goattrk2 [songname] [options]",
    "Options:",
    "-Axx Set ADSR parameter for hardrestart in hex. DEFAULT=0F00",
    "-Bxx Set sound buffer length in milliseconds DEFAULT=100",
    "-Cxx Use CatWeasel MK3 PCI SID (0 = off, 1 = on)",
    "-Dxx Pattern row display (0 = decimal, 1 = hex, 2 = decimal w/dots, 3 = hex w/dots)",
    "-Exx Set emulated SID model (0 = 6581 1 = 8580) DEFAULT=6581",
    "-Fxx Set custom SID clock cycles per second (0 = use PAL/NTSC default)",
    "-Gxx Set pitch of A-4 in Hz (0 = use default frequencytable, close to 440Hz)",
    "-Hxx Use HardSID (0 = off, 1 = HardSID ID0 2 = HardSID ID1 etc.)",
    "-Ixx Set reSID interpolation (0 = off, 1 = on, 2 = distortion, 3 = distortion & on) DEFAULT=off",
    "-Jxx Set special note names (2 chars for every note in an octave/cycle, e.g. C-DbD-EbE-F-GbG-AbA-BbB-)",
    "-Kxx Note-entry mode (0 = Protracker, 1 = DMC, 2 = Janko) DEFAULT=Protracker",
    "-Lxx SID memory location in hex. DEFAULT=D400",
    "-Mxx Set sound mixing rate DEFAULT=44100",
    "-Oxx Set pulseoptimization/skipping (0 = off, 1 = on) DEFAULT=on",
    "-Qxx Set equal divisions per octave (12 = default, 8.2019143 = Bohlen-Pierce)",
    "-Rxx Set realtime-effect optimization/skipping (0 = off, 1 = on) DEFAULT=on",
    "-Sxx Set speed multiplier (0 for 25Hz, 1 for 1x, 2 for 2x etc.)",
    "-Txx Set HardSID interactive mode sound buffer length in milliseconds DEFAULT=20, max.buffering=0",
    "-Uxx Set HardSID playback mode sound buffer length in milliseconds DEFAULT=400, max.buffering=0",
    "-Vxx Set finevibrato conversion (0 = off, 1 = on) DEFAULT=on",
    "-Xxx Set window type (0 = window, 1 = fullscreen) DEFAULT=window",
    "-Yxx Path to a Scala tuning file .scl",
    "-Zxx Set random reSID write delay in cycles (0 = off) DEFAULT=off",
    "-wxx Set text size (1 = normal, 2 to 4 = larger) DEFAULT=1",
    "-N   Use NTSC timing",
    "-P   Use PAL timing (DEFAULT)",
    "-W   Write sound output to a file SIDAUDIO.RAW",
    "-?   Show this info again",
    "-??  Open the online help window at startup",
};

int usagelen = (sizeof usage / sizeof usage[0]);

// Load the configuration, parse the command line, start sound and load the
// initial song. Returns -1 when the editor should start, or an exit status.
int goattrk2_init(int argc, char **argv)
{
  char filename[MAX_PATHNAME];
  FILE *configfile;
  int c,d;

  programname += sizeof "$VER:";
  // Open datafile
  io_openlinkeddatafile(datafile);

  // Load configuration
  #ifdef __WIN32__
  GetModuleFileName(NULL, filename, MAX_PATHNAME);
  filename[strlen(filename)-3] = 'c';
  filename[strlen(filename)-2] = 'f';
  filename[strlen(filename)-1] = 'g';
  #elif __amigaos__
  strcpy(filename, "PROGDIR:goattrk2.cfg");
  #else
  strcpy(filename, getenv("HOME"));
  strcat(filename, "/.goattrk/goattrk2.cfg");
  #endif
  specialnotenames[0] = 0;
  scalatuningfilepath[0] = 0;
  configfile = fopen(filename, "rt");
  if (configfile)
  {
    getparam(configfile, &b);
    getparam(configfile, &mr);
    getparam(configfile, &hardsid);
    getparam(configfile, &sidmodel);
    getparam(configfile, &ntsc);
    getparam(configfile, (unsigned *)&fileformat);
    getparam(configfile, (unsigned *)&playeradr);
    getparam(configfile, (unsigned *)&zeropageadr);
    getparam(configfile, &playerversion);
    getparam(configfile, &keypreset);
    getparam(configfile, (unsigned *)&stepsize);
    getparam(configfile, &multiplier);
    getparam(configfile, &catweasel);
    getparam(configfile, &adparam);
    getparam(configfile, &interpolate);
    getparam(configfile, &patterndispmode);
    getparam(configfile, &sidaddress);
    getparam(configfile, &finevibrato);
    getparam(configfile, &optimizepulse);
    getparam(configfile, &optimizerealtime);
    getparam(configfile, &residdelay);
    getparam(configfile, &customclockrate);
    getparam(configfile, &hardsidbufinteractive);
    getparam(configfile, &hardsidbufplayback);
    getfloatparam(configfile, &filterparams.distortionrate);
    getfloatparam(configfile, &filterparams.distortionpoint);
    getfloatparam(configfile, &filterparams.distortioncfthreshold);
    getfloatparam(configfile, &filterparams.type3baseresistance);
    getfloatparam(configfile, &filterparams.type3offset);
    getfloatparam(configfile, &filterparams.type3steepness);
    getfloatparam(configfile, &filterparams.type3minimumfetresistance);
    getfloatparam(configfile, &filterparams.type4k);
    getfloatparam(configfile, &filterparams.type4b);
    getfloatparam(configfile, &filterparams.voicenonlinearity);
    getparam(configfile, (unsigned*)&win_fullscreen);
    getparam(configfile, &bigwindow);
    getfloatparam(configfile, &basepitch);
    getfloatparam(configfile, &equaldivisionsperoctave);
    getstringparam(configfile, specialnotenames);
    getstringparam(configfile, scalatuningfilepath);
    fclose(configfile);
  }

  // Init pathnames
  initpaths();

  // Scan command line
  for (c = 1; c < argc; c++)
  {
    #ifdef __WIN32__
    if ((argv[c][0] == '-') || (argv[c][0] == '/'))
    #else
    if (argv[c][0] == '-')
    #endif
    {
      int y;
      switch (argv[c][1]) //switch (toupper(argv[c][1]))
      {
        case '-':
        if (strcmp(argv[c], "--help"))
            break;
        case '?':
        if (argv[c][2] == '?')
        {
          starthelp = 1;
          break;
        }
        for (y = 0; y < usagelen; ++y)
          printf("%s\n", usage[y]);
        return EXIT_SUCCESS;

        case 'Z':
        sscanf(&argv[c][2], "%u", &residdelay);
        break;

        case 'A':
        sscanf(&argv[c][2], "%x", &adparam);
        break;

        case 'S':
        sscanf(&argv[c][2], "%u", &multiplier);
        break;

        case 'B':
        sscanf(&argv[c][2], "%u", &b);
        break;

        case 'D':
        sscanf(&argv[c][2], "%u", &patterndispmode);
        break;

        case 'E':
        sscanf(&argv[c][2], "%u", &sidmodel);
        break;

        case 'I':
        sscanf(&argv[c][2], "%u", &interpolate);
        break;

        case 'K':
        sscanf(&argv[c][2], "%u", &keypreset);
        break;

        case 'L':
        sscanf(&argv[c][2], "%x", &sidaddress);
        break;

        case 'N':
        ntsc = 1;
        customclockrate = 0;
        break;

        case 'P':
        ntsc = 0;
        customclockrate = 0;
        break;

        case 'F':
        sscanf(&argv[c][2], "%u", &customclockrate);
        break;

        case 'M':
        sscanf(&argv[c][2], "%u", &mr);
        break;

        case 'O':
        sscanf(&argv[c][2], "%u", &optimizepulse);
        break;

        case 'R':
        sscanf(&argv[c][2], "%u", &optimizerealtime);
        break;

        case 'H':
        sscanf(&argv[c][2], "%u", &hardsid);
        break;

        case 'V':
        sscanf(&argv[c][2], "%u", &finevibrato);
        break;

        case 'T':
        sscanf(&argv[c][2], "%u", &hardsidbufinteractive);
        break;

        case 'U':
        sscanf(&argv[c][2], "%u", &hardsidbufplayback);
        break;

        case 'W':
        writer = 1;
        break;

        case 'X':
        sscanf(&argv[c][2], "%u", &win_fullscreen);
        break;

        case 'C':
        sscanf(&argv[c][2], "%u", &catweasel);
        break;

        case 'G':
        sscanf(&argv[c][2], "%f", &basepitch);
        break;
 
        case 'Q':
        sscanf(&argv[c][2], "%f", &equaldivisionsperoctave);
        break;
 
        case 'J':
        sscanf(&argv[c][2], "%s", specialnotenames);
        break;
  
        case 'Y':
        sscanf(&argv[c][2], "%s", scalatuningfilepath);
        break;
  
        case 'w':
        sscanf(&argv[c][2], "%u", &bigwindow);
        break;
      }
    }
    else
    {
      char startpath[MAX_PATHNAME];

      strcpy(songfilename, argv[c]);
      for (d = strlen(argv[c])-1; d >= 0; d--)
      {
        if ((argv[c][d] == '/') || (argv[c][d] == '\\'))
        {
          strcpy(startpath, argv[c]);
          startpath[d+1] = 0;
          chdir(startpath);
          initpaths();
          strcpy(songfilename, &argv[c][d+1]);
          break;
        }
      }
    }
  }

  // Validate parameters
  sidmodel &= 1;
  adparam &= 0xffff;
  zeropageadr &= 0xff;
  playeradr &= 0xff00;
  sidaddress &= 0xffff;
  if (!stepsize) stepsize = 4;
  if (multiplier > 16) multiplier = 16;
  if (keypreset > 2) keypreset = 0;
  if ((finevibrato == 1) && (multiplier < 2)) usefinevib = 1;
  if (finevibrato > 1) usefinevib = 1;
  if (optimizepulse > 1) optimizepulse = 1;
  if (optimizerealtime > 1) optimizerealtime = 1;
  if (residdelay > 63) residdelay = 63;
  if (customclockrate < 100) customclockrate = 0;
  if (bigwindow < 1) bigwindow = 1;
  if (bigwindow > 4) bigwindow = 4;

  // Read Scala tuning file
  if (scalatuningfilepath[0] != '0' && scalatuningfilepath[1] != '\0')
  {
    readscalatuningfile();
  }

  // Calculate frequencytable if necessary
  if (basepitch < 0.0f)
    basepitch = 0.0f;
  if (basepitch > 0.0f)
    calculatefreqtable();

  // Set special note names
  if (specialnotenames[1] != '\0')
  {
    setspecialnotenames();
  }

  // SDL provides the audio output only; the GUI turns SIGINT/SIGTERM into a
  // normal quit
  SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
  if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0) soundinitfailed = 1;
  else atexit(SDL_Quit);

  // Reset channels/song
  initchannels();
  clearsong(1,1,1,1,1);

  // Init sound
  if ((soundinitfailed) || (!sound_init(b, mr, writer, hardsid, sidmodel, ntsc, multiplier, catweasel, interpolate, customclockrate)))
    soundinitfailed = 1;

  // Load song if applicable
  if (strlen(songfilename)) loadsong();

  return -1;
}

// Stop sound and save the configuration
void goattrk2_shutdown(void)
{
  char filename[MAX_PATHNAME];
  FILE *configfile;

  sound_uninit();

  // Save configuration
  #ifndef __WIN32__
  #ifdef __amigaos__
  strcpy(filename, "PROGDIR:goattrk2.cfg");
  #else
  strcpy(filename, getenv("HOME"));
  strcat(filename, "/.goattrk");
  mkdir(filename, S_IRUSR | S_IWUSR | S_IXUSR);
  strcat(filename, "/goattrk2.cfg");
  #endif
  #endif
  configfile = fopen(filename, "wt");
  if (configfile)
  {
    fprintf(configfile, ";------------------------------------------------------------------------------\n"
                        ";GT2 config file. Rows starting with ; are comments. Hexadecimal parameters are\n"
                        ";to be preceded with $ and decimal parameters with nothing.                    \n"
                        ";------------------------------------------------------------------------------\n"
                        "\n"
                        ";reSID buffer length (in milliseconds)\n%d\n\n"
                        ";reSID mixing rate (in Hz)\n%d\n\n"
                        ";Hardsid device number (0 = off)\n%d\n\n"
                        ";reSID model (0 = 6581, 1 = 8580)\n%d\n\n"
                        ";Timing mode (0 = PAL, 1 = NTSC)\n%d\n\n"
                        ";Packer/relocator fileformat (0 = SID, 1 = PRG, 2 = BIN)\n%d\n\n"
                        ";Packer/relocator player address\n$%04x\n\n"
                        ";Packer/relocator zeropage baseaddress\n$%02x\n\n"
                        ";Packer/relocator player type (0 = standard ... 3 = minimal)\n%d\n\n"
                        ";Key entry mode (0 = Protracker, 1 = DMC, 2 = Janko)\n%d\n\n"
                        ";Pattern highlight step size\n%d\n\n"
                        ";Speed multiplier (0 = 25Hz, 1 = 1X, 2 = 2X etc.)\n%d\n\n"
                        ";Use CatWeasel SID (0 = off, 1 = on)\n%d\n\n"
                        ";Hardrestart ADSR parameter\n$%04x\n\n"
                        ";reSID interpolation (0 = off, 1 = on, 2 = distortion, 3 = distortion & on)\n%d\n\n"
                        ";Pattern display mode (0 = decimal, 1 = hex, 2 = decimal w/dots, 3 = hex w/dots)\n%d\n\n"
                        ";SID baseaddress\n$%04x\n\n"
                        ";Finevibrato mode (0 = off, 1 = on)\n%d\n\n"
                        ";Pulseskipping (0 = off, 1 = on)\n%d\n\n"
                        ";Realtime effect skipping (0 = off, 1 = on)\n%d\n\n"
                        ";Random reSID write delay in cycles (0 = off)\n%d\n\n"
                        ";Custom SID clock cycles per second (0 = use PAL/NTSC default)\n%d\n\n"
                        ";HardSID interactive mode buffer size (in milliseconds, 0 = maximum/no flush)\n%d\n\n"
                        ";HardSID playback mode buffer size (in milliseconds, 0 = maximum/no flush)\n%d\n\n"
                        ";reSID-fp distortion rate\n%f\n\n"
                        ";reSID-fp distortion point\n%f\n\n"
                        ";reSID-fp distortion CF threshold\n%f\n\n"
                        ";reSID-fp type 3 base resistance\n%f\n\n"
                        ";reSID-fp type 3 base offset\n%f\n\n"
                        ";reSID-fp type 3 base steepness\n%f\n\n"
                        ";reSID-fp type 3 minimum FET resistance\n%f\n\n"
                        ";reSID-fp type 4 k\n%f\n\n"
                        ";reSID-fp type 4 b\n%f\n\n"
                        ";reSID-fp voice nonlinearity\n%f\n\n"
                        ";Window type (0 = window, 1 = fullscreen)\n%d\n\n"
                        ";window scale factor (1 = no scaling, 2 to 4 = 2 to 4 times bigger window)\n%d\n\n"
                        ";Base pitch of A-4 in Hz (0 = use default frequencytable)\n%f\n\n"
                        ";Equal divisions per octave (12 = default, 8.2019143 = Bohlen-Pierce)\n%f\n\n"
                        ";Special note names (2 chars for every note in an octave/cycle)\n%s\n\n"
                        ";Path to a Scala tuning file .scl\n%s\n\n",
    b,
    mr,
    hardsid,
    sidmodel,
    ntsc,
    fileformat,
    playeradr,
    zeropageadr,
    playerversion,
    keypreset,
    stepsize,
    multiplier,
    catweasel,
    adparam,
    interpolate,
    patterndispmode,
    sidaddress,
    finevibrato,
    optimizepulse,
    optimizerealtime,
    residdelay,
    customclockrate,
    hardsidbufinteractive,
    hardsidbufplayback,
    filterparams.distortionrate,
    filterparams.distortionpoint,
    filterparams.distortioncfthreshold,
    filterparams.type3baseresistance,
    filterparams.type3offset,
    filterparams.type3steepness,
    filterparams.type3minimumfetresistance,
    filterparams.type4k,
    filterparams.type4b,
    filterparams.voicenonlinearity,
    win_fullscreen,
    bigwindow,
    basepitch,
    equaldivisionsperoctave,
    specialnotenames,
    scalatuningfilepath);
    fclose(configfile);
  }
}

void converthex()
{
  int c;

  hexnybble = -1;
  for (c = 0; c < 16; c++)
  {
    if (tolower(key) == hexkeytbl[c])
    {
      if (c >= 10)
      {
        if (!shiftpressed) hexnybble = c;
      }
      else
      {
        hexnybble = c;
      }
    }
  }
}


void docommand(void)
{
  // Mode-specific commands
  switch(editmode)
  {
    case EDIT_ORDERLIST:
    orderlistcommands();
    break;

    case EDIT_INSTRUMENT:
    instrumentcommands();
    break;

    case EDIT_TABLES:
    tablecommands();
    break;

    case EDIT_PATTERN:
    patterncommands();
    break;

    case EDIT_NAMES:
    namecommands();
    break;
  }

  // General commands
  generalcommands();
}

void generalcommands(void)
{
  int c;

  switch(key)
  {
    case '?':
    case '-':
    if ((editmode != EDIT_NAMES) && (editmode != EDIT_ORDERLIST))
    {
      if (!((editmode == EDIT_INSTRUMENT) && (eipos == 9))) previnstr();
    }
    break;

    case '+':
    case '_':
    if ((editmode != EDIT_NAMES) && (editmode != EDIT_ORDERLIST))
    {
      if (!((editmode == EDIT_INSTRUMENT) && (eipos >= 9))) nextinstr();

    }
    break;

    case '*':
    if (editmode != EDIT_NAMES)
    {
      if (!((editmode == EDIT_INSTRUMENT) && (eipos >= 9)))
      {
        if (epoctave < 7) epoctave++;
      }
    }
    break;

    case '/':
    case '\'':
    if (editmode != EDIT_NAMES)
    {
      if (!((editmode == EDIT_INSTRUMENT) && (eipos >= 9)))
      {
        if (epoctave > 0) epoctave--;
      }
    }
    break;

    case '<':
    if (((editmode == EDIT_INSTRUMENT) && (eipos != 9)) || (editmode == EDIT_TABLES))
      previnstr();
    break;

    case '>':
    if (((editmode == EDIT_INSTRUMENT) && (eipos != 9)) || (editmode == EDIT_TABLES))
      nextinstr();
    break;

    case ';':
    for (c = 0; c < MAX_CHN; c++)
    {
      if (espos[c]) espos[c]--;
      if (espos[c] < esview)
      {
        esview = espos[c];
        eseditpos = espos[c];
      }
    }
    updateviewtopos();
    rewindsong();
    break;

    case ':':
    for (c = 0; c < MAX_CHN; c++)
    {
      if (espos[c] < songlen[esnum][c]-1)
        espos[c]++;
      if (espos[c] - esview >= VISIBLEORDERLIST)
      {
        esview = espos[c] - VISIBLEORDERLIST + 1;
        eseditpos = espos[c];
      }
    }
    updateviewtopos();
    rewindsong();
    break;

  }
  switch(rawkey)
  {
    case KEY_ESC:
    if (!shiftpressed)
      ui_quit();
    else
      ui_clear();
    break;

    case KEY_KPMULTIPLY:
    if ((editmode != EDIT_NAMES) && (!key))
    {
      if (!((editmode == EDIT_INSTRUMENT) && (eipos >= 9)))
      {
        if (epoctave < 7) epoctave++;
      }
    }
    break;

    case KEY_KPDIVIDE:
    if ((editmode != EDIT_NAMES) && (!key))
    {
      if (!((editmode == EDIT_INSTRUMENT) && (eipos >= 9)))
      {
        if (epoctave > 0) epoctave--;
      }
    }
    break;

    case KEY_F12:
    ui_help(shiftpressed);
    break;

    case KEY_TAB:
    if (!shiftpressed) editmode++;
    else editmode--;
    if (editmode > EDIT_NAMES) editmode = EDIT_PATTERN;
    if (editmode < EDIT_PATTERN) editmode = EDIT_NAMES;
    break;

    case KEY_F1:
    initsong(esnum, PLAY_BEGINNING);
    followplay = shiftpressed;
    break;

    case KEY_F2:
    initsong(esnum, PLAY_POS);
    followplay = shiftpressed;
    break;

    case KEY_F3:
    // With looping on and rows marked, loop just those rows
    if ((loopplay) && (epmarkchn >= 0) && (epmarkstart != epmarkend))
    {
      int start = (epmarkstart < epmarkend) ? epmarkstart : epmarkend;
      int end = (epmarkstart < epmarkend) ? epmarkend : epmarkstart;

      initsongpos(esnum, PLAY_PATTERN, start);
      looprowstart = start;
      looprowend = end;
    }
    else initsong(esnum, PLAY_PATTERN);
    followplay = shiftpressed;
    break;

    case KEY_F4:
    if (shiftpressed)
      mutechannel(epchn);
    else
    stopsong();
    break;

    case KEY_F5:
    if (!shiftpressed)
      editmode = EDIT_PATTERN;
    else prevmultiplier();
    break;

    case KEY_F6:
    if (!shiftpressed)
      editmode = EDIT_ORDERLIST;
    else nextmultiplier();
    break;

    case KEY_F7:
    if (!shiftpressed)
    {
      if (editmode == EDIT_INSTRUMENT)
        editmode = EDIT_TABLES;
      else
        editmode = EDIT_INSTRUMENT;
    }
    else ui_editadsr();
    break;

    case KEY_F8:
    if (!shiftpressed)
      editmode = EDIT_NAMES;
    else
    {
      sidmodel ^= 1;
      sound_init(b, mr, writer, hardsid, sidmodel, ntsc, multiplier, catweasel, interpolate, customclockrate);
    }
    break;

    case KEY_F9:
    ui_relocator();
    break;

    case KEY_F10:
    ui_load(shiftpressed);
    break;

    case KEY_F11:
    ui_save();
    break;
  }
}

// Clear the chosen parts of the song (what Shift+ESC offers)
void doclear(int cs, int cp, int ci, int ct, int cn, int newpatternlength)
{
  if (cp)
  {
    if (newpatternlength < 1) newpatternlength = 1;
    if (newpatternlength > MAX_PATTROWS) newpatternlength = MAX_PATTROWS;
    defaultpatternlength = newpatternlength;
  }
  if (cs | cp | ci | ct | cn)
    memset(songfilename, 0, sizeof songfilename);
  clearsong(cs, cp, ci, ct, cn);
}

void getparam(FILE *handle, unsigned *value)
{
  char *configptr;

  for (;;)
  {
    if (feof(handle)) return;
    fgets(configbuf, MAX_PATHNAME, handle);
    if ((configbuf[0]) && (configbuf[0] != ';') && (configbuf[0] != ' ') && (configbuf[0] != 13) && (configbuf[0] != 10)) break;
  }

  configptr = configbuf;
  if (*configptr == '$')
  {
    *value = 0;
    configptr++;
    for (;;)
    {
      char c = tolower(*configptr++);
      int h = -1;

      if ((c >= 'a') && (c <= 'f')) h = c - 'a' + 10;
      if ((c >= '0') && (c <= '9')) h = c - '0';

      if (h >= 0)
      {
        *value *= 16;
        *value += h;
      }
      else break;
    }
  }
  else
  {
    *value = 0;
    for (;;)
    {
      char c = tolower(*configptr++);
      int d = -1;

      if ((c >= '0') && (c <= '9')) d = c - '0';

      if (d >= 0)
      {
        *value *= 10;
        *value += d;
      }
      else break;
    }
  }
}

void getfloatparam(FILE *handle, float *value)
{
  char *configptr;

  for (;;)
  {
    if (feof(handle)) return;
    fgets(configbuf, MAX_PATHNAME, handle);
    if ((configbuf[0]) && (configbuf[0] != ';') && (configbuf[0] != ' ') && (configbuf[0] != 13) && (configbuf[0] != 10)) break;
  }

  configptr = configbuf;
  *value = 0.0f;
  sscanf(configptr, "%f", value);
}

void getstringparam(FILE *handle, char *value)
{
  char *configptr;

  for (;;)
  {
    if (feof(handle)) return;
    fgets(configbuf, MAX_PATHNAME, handle);
    if ((configbuf[0]) && (configbuf[0] != ';') && (configbuf[0] != ' ') && (configbuf[0] != 13) && (configbuf[0] != 10)) break;
  }

  configptr = configbuf;
  
  sscanf(configptr, "%s", value);
}

void prevmultiplier(void)
{
  if (multiplier > 0)
  {
    multiplier--;
    sound_init(b, mr, writer, hardsid, sidmodel, ntsc, multiplier, catweasel, interpolate, customclockrate);
  }
}

void nextmultiplier(void)
{
  if (multiplier < 16)
  {
    multiplier++;
    sound_init(b, mr, writer, hardsid, sidmodel, ntsc, multiplier, catweasel, interpolate, customclockrate);
  }
}

void calculatefreqtable()
{
  double basefreq = (double)basepitch * (16777216.0 / 985248.0) * pow(2.0, 0.25) / 32.0;
  double cyclebasefreq = basefreq;
  double freq = basefreq;
  int c;
  int i;

  if (tuningcount)
  {
    c = 0;
    while (c < 96)
    {
      for (i = 0; i < tuningcount; i++)
      {
        if (c < 96)
        {
          int intfreq = freq + 0.5;
          if (intfreq > 0xffff)
              intfreq = 0xffff;
          freqtbllo[c] = intfreq & 0xff;
          freqtblhi[c] = intfreq >> 8;          
          freq = cyclebasefreq * tuning[i];
          c++;
        }
      }
      cyclebasefreq = freq;
    }
  }
  else
  {
    for (c = 0; c < 8*12 ; c++)
    {
      double note = c;
      double freq = basefreq * pow(2.0, note/(double)equaldivisionsperoctave);
      int intfreq = freq + 0.5;
      if (intfreq > 0xffff)
          intfreq = 0xffff;
      freqtbllo[c] = intfreq & 0xff;
      freqtblhi[c] = intfreq >> 8;
    }
  }
}

void setspecialnotenames()
{
  int i;
  int j;
  int oct;
  char *name;
  char octave[11];
  
  i = 0;
  oct = 0;
  while (i < 93)
  {
    for (j = 0; j < 186; j += 2)
    {
      if (specialnotenames[j] == '\0')
        break;
      if (i < 93)
      {
        name = malloc(4);
        strncpy(name, specialnotenames + j, 2);
        sprintf(octave, "%d", oct);
        strcpy(name + 2, octave);
        notename[i] = name;
        i++;
      }
    }
    oct++;
  }
}

void readscalatuningfile()
{
  FILE *scalatuningfile;
  char *configptr;
  char strbuf[64];
  char name[3];
  int i;
  double numerator;
  double denominator;
  double centvalue;

  scalatuningfile = fopen(scalatuningfilepath, "rt");
  if (scalatuningfile)
  {
    // Tuning name
    for (;;)
    {
      if (feof(scalatuningfile)) return;
      fgets(configbuf, MAX_PATHNAME, scalatuningfile);
      if ((configbuf[0]) && (configbuf[0] != '!') && (configbuf[0] != 13) && (configbuf[0] != 10)) break;
    }
    configptr = configbuf;
    sscanf(configptr, "%63[^\t\n]", tuningname);

    // Tuning count
    for (;;)
    {
      if (feof(scalatuningfile)) return;
      fgets(configbuf, MAX_PATHNAME, scalatuningfile);
      if ((configbuf[0]) && (configbuf[0] != '!') && (configbuf[0] != 13) && (configbuf[0] != 10)) break;
    }
    configptr = configbuf;
    sscanf(configptr, "%d", &tuningcount);

    // Tunings
    for (i = 0; i < tuningcount; i++)
    {
      for (;;)
      {
        if (feof(scalatuningfile)) return;
        fgets(configbuf, MAX_PATHNAME, scalatuningfile);
        if ((configbuf[0]) && (configbuf[0] != '!') && (configbuf[0] != 13) && (configbuf[0] != 10)) break;
      }
      configptr = configbuf;
      name[0] = '\0';
      sscanf(configptr, "%63s %2s", strbuf, name);
      if (!i)
      {
        strcpy(specialnotenames, name);
      }
      else
      {
        if (i == tuningcount - 1)
        {
          char *tmp = strdup(specialnotenames);
          strcpy(specialnotenames, name);
          strcat(specialnotenames, tmp);
          free(tmp);
        }
        else
        {
          strcat(specialnotenames, name);
        }
      }
      if (!strchr(strbuf, '.'))
      {
        sscanf(strbuf, "%lf", &numerator);
        if (strchr(strbuf, '/'))
        {
          sscanf(strchr(strbuf, '/') + 1, "%lf", &denominator);
          tuning[i] = numerator / denominator;
        }
      }
      else
      {
        sscanf(configptr, "%lf", &centvalue);
        tuning[i] = pow(2.0, centvalue / 1200.0);
      }
    }
    fclose(scalatuningfile);
  }  
}
