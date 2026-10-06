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

// The same for every row at once, without the cache (for code that changes
// the tables): map[table][row] is set for rows something can execute,
// leaving out the table pointers of instrument skipinstr (-1 = none)
void table_reachmap(unsigned char map[MAX_TABLES][MAX_TABLELEN], int skipinstr);

// Add the rows a program starting at pos (0-based) runs through to map
void table_followprogram(unsigned char map[MAX_TABLES][MAX_TABLELEN], int table, int pos);

#endif
