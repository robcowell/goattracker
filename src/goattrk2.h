#ifndef GOATTRK2_H
#define GOATTRK2_H

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <math.h>
#include "bme.h"

#include "gcommon.h"
#include "gsound.h"
#include "gsid.h"
#include "gsong.h"
#include "gplay.h"
#include "gdisplay.h"
#include "greloc.h"
#include "gfile.h"
#include "gpattern.h"
#include "gorder.h"
#include "ginstr.h"
#include "gtable.h"
#include "ghelp.h"

#define EDIT_PATTERN 0
#define EDIT_ORDERLIST 1
#define EDIT_INSTRUMENT 2
#define EDIT_TABLES 3
#define EDIT_NAMES 4

#define KEY_TRACKER 0
#define KEY_DMC 1
#define KEY_JANKO 2

#define VISIBLEPATTROWS 31
#define VISIBLEORDERLIST 15
#define VISIBLETABLEROWS 15

#define PGUPDNREPEAT 8

#ifndef GOATTRK2_C
extern int key, rawkey, shiftpressed;
extern int soundinitfailed;
extern int starthelp;
extern unsigned bigwindow;
extern int win_fullscreen;
extern unsigned customclockrate;
extern int editmode;
extern int recordmode;
extern int followplay;
extern int hexnybble;
extern int stepsize;
extern int autoadvance;
extern int defaultpatternlength;
extern unsigned keypreset;
extern unsigned playerversion;
extern int fileformat;
extern int zeropageadr;
extern int playeradr;
extern unsigned sidmodel;
extern unsigned multiplier;
extern unsigned adparam;
extern unsigned ntsc;
extern unsigned patterndispmode;
extern unsigned sidaddress;
extern unsigned finevibrato;
extern unsigned optimizepulse;
extern unsigned optimizerealtime;
extern unsigned usefinevib;
extern unsigned b;
extern unsigned mr;
extern unsigned writer;
extern unsigned hardsid;
extern unsigned catweasel;
extern unsigned interpolate;
extern unsigned hardsidbufinteractive;
extern unsigned hardsidbufplayback;
extern float basepitch;
extern char configbuf[MAX_PATHNAME];
extern char loadedsongfilename[MAX_FILENAME];
extern char songfilename[MAX_FILENAME];
extern char songfilter[MAX_FILENAME];
extern char songpath[MAX_PATHNAME];
extern char instrfilename[MAX_FILENAME];
extern char instrfilter[MAX_FILENAME];
extern char instrpath[MAX_PATHNAME];
extern char packedpath[MAX_PATHNAME];
extern char *programname;
extern char *notename[];
extern char textbuffer[MAX_PATHNAME];
extern unsigned char hexkeytbl[16];
extern unsigned char datafile[];
#endif

int goattrk2_init(int argc, char **argv);
void goattrk2_shutdown(void);
void doclear(int cs, int cp, int ci, int ct, int cn, int newpatternlength);
void getparam(FILE *handle, unsigned *value);
void getfloatparam(FILE *handle, float *value);
void getstringparam(FILE *handle, char *value);
void converthex(void);
void docommand(void);
void generalcommands(void);
void prevmultiplier(void);
void nextmultiplier(void);
void calculatefreqtable(void);
void setspecialnotenames(void);
void readscalatuningfile(void);

#endif
