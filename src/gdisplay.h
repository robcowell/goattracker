#ifndef GDISPLAY_H
#define GDISPLAY_H

#ifndef GDISPLAY_C
extern int timemin;
extern int timesec;
extern int timeframe;
#endif

void followplayupdate(void);
void resettime(void);
void incrementtime(void);

#endif
