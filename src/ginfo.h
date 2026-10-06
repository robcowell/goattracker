#ifndef GINFO_H
#define GINFO_H

// Plain-language descriptions of song data (readme.txt sections 3.2-3.4)

// Describe the item under the editor cursor (editmode and the e* cursors)
void info_describe(char *buf, int size);

// Describe a table row. brief = a short form for narrow columns.
void table_describe(int table, int pos, char *buf, int size, int brief);

// Whether anything (an instrument, a pattern command or a wavetable command)
// can ever execute this table row
int table_isreachable(int table, int pos);

#endif
