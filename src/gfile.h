#ifndef GFILE_H
#define GFILE_H

#define MAX_FILENAME 60
#define MAX_PATHNAME 256

void initpaths(void);
void editstring(char *buffer, int maxlength);
int cmpname(char *string1, char *string2);

#endif

