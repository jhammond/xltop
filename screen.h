#ifndef _SCREEN_H_
#define _SCREEN_H_
#include <ev.h>

extern int screen_is_active;

int screen_init(void (*refresh_cb)(EV_P_ int LINES, int COLS), double interval);
void screen_start(EV_P);
void screen_stop(EV_P);
void screen_refresh(EV_P);
void screen_set_key_cb(void(*cb)(EV_P_ int c));

#endif
