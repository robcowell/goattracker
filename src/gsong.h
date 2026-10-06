#ifndef GSONG_H
#define GSONG_H

// Outcome of mergesong(): which limit stopped a merge part way
#define MERGE_OK 0
#define MERGE_NOSONGS 1
#define MERGE_NOINSTRUMENTS 2
#define MERGE_NOTABLES 3
#define MERGE_NOPATTERNS 4
#define MERGE_BADFILE 5

// Outcome of loadsong() for GTS3-GTS5 files that fail validation
#define LOAD_OK 0
#define LOAD_DAMAGED 1
#define LOAD_MULTICHANNEL 2

#ifndef GSONG_C
extern INSTR instr[MAX_INSTR];
extern unsigned char ltable[MAX_TABLES][MAX_TABLELEN];
extern unsigned char rtable[MAX_TABLES][MAX_TABLELEN];
extern unsigned char songorder[MAX_SONGS][MAX_CHN][MAX_SONGLEN+2];
extern unsigned char pattern[MAX_PATT][MAX_PATTROWS*4+4];
extern char songname[MAX_STR];
extern char authorname[MAX_STR];
extern char copyrightname[MAX_STR];
extern int pattlen[MAX_PATT];
extern int songlen[MAX_SONGS][MAX_CHN];
extern int highestusedpattern;
extern int highestusedinstr;
extern int songsidtracker64;
extern int songsidchannels;
extern int songsettingsloaded;
extern int mergeresult;
extern int loadresult;
#endif

void loadsong(void);
int savesongfile(const char *path);
void mergesong(void);
void loadinstrument(void);
int savesong(void);
int saveinstrument(void);
void clearsong(int cs, int cp, int ci, int cf, int cn);
void countpatternlengths(void);
void countthispattern(void);
void clearpattern(int p);
int insertpattern(int p);
void deletepattern(int p);
void findusedpatterns(void);
void findduplicatepatterns(void);
void optimizeeverything(int oi, int ot);

#endif
