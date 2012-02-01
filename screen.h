#ifndef _SCREEN_H_
#define _SCREEN_H_
#include <ev.h>

int screen_init(void);
void screen_start(EV_P);
void screen_stop(EV_P);

#endif
