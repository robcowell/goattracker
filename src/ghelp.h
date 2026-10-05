#ifndef GHELP_H
#define GHELP_H

typedef struct
{
  const char *title;
  char **rows;
  int editmodes;      // bitmask of (1 << EDIT_xxx) the section is relevant to
} HELPSECTION;

// Help sections, terminated by an entry with a NULL title
const HELPSECTION *gethelpsections(void);
int helpsectionforeditmode(const HELPSECTION *section, int mode);

#endif
