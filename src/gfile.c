//
// GOATTRACKER v2 file paths and string editing
//

#define GFILE_C

#ifdef __WIN32__
#include <windows.h>
#endif

#include "goattrk2.h"

void initpaths(void)
{
  memset(loadedsongfilename, 0, sizeof loadedsongfilename);
  memset(songfilename, 0, sizeof songfilename);
  memset(instrfilename, 0, sizeof instrfilename);
  memset(songpath, 0, sizeof songpath);
  memset(instrpath, 0, sizeof instrpath);
  memset(packedpath, 0, sizeof packedpath);
  strcpy(songfilter, "*.sng");
  strcpy(instrfilter, "*.ins");

  getcwd(songpath, MAX_PATHNAME);
  strcpy(instrpath, songpath);
  strcpy(packedpath, songpath);
}

void editstring(char *buffer, int maxlength)
{
  int len = strlen(buffer);

  if (key)
  {
    if ((key >= 32) && (key < 256))
    {
      if (len < maxlength-1)
      {
        buffer[len] = key;
        buffer[len+1] = 0;
      }
    }
    if ((key == 8) && (len > 0))
    {
      buffer[len-1] = 0;
    }
  }
}

int cmpname(char *string1, char *string2)
{
  for (;;)
  {
    unsigned char char1 = tolower(*string1++);
    unsigned char char2 = tolower(*string2++);
    if (char1 < char2) return -1;
    if (char1 > char2) return 1;
    if ((!char1) || (!char2)) return 0;
  }
}

