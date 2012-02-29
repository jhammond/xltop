#include <ctype.h>
#include <ncurses.h>
#include <signal.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "evx.h"
#include "screen.h"
#include "trace.h"

static void (*screen_refresh_cb)(EV_P_ int LINES, int COLS);

static struct ev_timer refresh_timer_w;
static struct ev_io stdin_io_w;
static struct ev_signal sigint_w;
static struct ev_signal sigterm_w;
static struct ev_signal sigwinch_w;

static unsigned long bkgd_attr[2];

static void refresh_timer_cb(EV_P_ struct ev_timer *w, int revents);
static void stdin_io_cb(EV_P_ struct ev_io *w, int revents);
static void sigint_cb(EV_P_ ev_signal *w, int revents);
static void sigwinch_cb(EV_P_ ev_signal *w, int revents);

int screen_init(void (*refresh_cb)(EV_P_ int, int), double interval)
{
  evx_set_nonblock(STDIN_FILENO);
  evx_set_cloexec(STDIN_FILENO);

  screen_refresh_cb = refresh_cb;

  ev_timer_init(&refresh_timer_w, &refresh_timer_cb, 0.001, interval);
  ev_io_init(&stdin_io_w, &stdin_io_cb, STDIN_FILENO, EV_READ);
  ev_signal_init(&sigint_w, &sigint_cb, SIGINT);
  ev_signal_init(&sigterm_w, &sigint_cb, SIGTERM);
  ev_signal_init(&sigwinch_w, &sigwinch_cb, SIGWINCH);

  return 0;
}

void screen_start(EV_P)
{
  short f[2], b[2];

  /* Begin curses magic. */
  if (initscr() == NULL)
    FATAL("cannot initialize screen: %m\n");

  cbreak();
  noecho();
  nonl();
  intrflush(stdscr, 0);
  keypad(stdscr, 1);
  nodelay(stdscr, 1);

  bkgd_attr[0] = getbkgd(stdscr);
  pair_content(bkgd_attr[0], &f[0], &b[0]);
  TRACE("bkgd[0] %lx, f %x, b %x\n", bkgd_attr[0],
        (unsigned int) f[0], (unsigned int) b[0]);

  if (!has_colors())
    ERROR("terminal has no color capabilities\n");

  use_default_colors();
  start_color();

  init_pair(1, -1, -1);
  init_pair(2, COLOR_BLUE, -1);

  bkgd(COLOR_PAIR(1));
  bkgd_attr[1] = getbkgd(stdscr);
  pair_content(bkgd_attr[1], &f[1], &b[1]);
  TRACE("bkgd[1] %lx, f %x, b %x\n", bkgd_attr[1],
        (unsigned int) f[1], (unsigned int) b[1]);

  /* Hide the cursor. */
  curs_set(0);

  ev_timer_start(EV_A_ &refresh_timer_w);
  ev_io_start(EV_A_ &stdin_io_w);
  ev_signal_start(EV_A_ &sigint_w);
  ev_signal_start(EV_A_ &sigterm_w);
  ev_signal_start(EV_A_ &sigwinch_w);
}

void screen_stop(EV_P)
{
  endwin();
}

static void refresh_timer_cb(EV_P_ ev_timer *w, int revents)
{
#if 0
  static int i = -1, j = -1, di = 1, dj = 1;
  char buf[80];
  int n = snprintf(buf, sizeof(buf), "***");

  i += di;
  if (i < 0) {
    i = 0;
    di = 1;
  } else if (i + n >= COLS) {
    i = COLS - n;
    di = -1;
  }

  j += dj;
  if (j <= nr_hdr_lines) {
    j = nr_hdr_lines;
    dj = 1;
  } else if (j >= LINES - 1) {
    j = LINES - 1;
    dj = -1;
  }

  attron(COLOR_PAIR(2)|A_BLINK|A_BOLD);
  mvaddnstr(j, i, buf, -1);
  attroff(COLOR_PAIR(2)|A_BLINK|A_BOLD);
#endif

  (*screen_refresh_cb)(EV_A_ LINES, COLS);
  refresh();
}

static void stdin_io_cb(EV_P_ ev_io *w, int revents)
{
  int c = getch();
  if (c == ERR || !isascii(c)) /* XXX isascii() */
    return;

  TRACE("got `%c' from stdin\n", c);
  switch (c) {
  case ' ':
  case '\n':
    ev_feed_event(EV_A_ &refresh_timer_w, EV_TIMER);
    break;
  case 'q':
    ev_break(EV_A_ EVBREAK_ALL); /* XXX */
    break;
  default:
    ERROR("unknown command `%c': try `h' for help\n", c); /* TODO help. */
    break;
  }
}

static void sigwinch_cb(EV_P_ ev_signal *w, int revents)
{
  TRACE("handling signal %d `%s'\n", w->signum, strsignal(w->signum));

  struct winsize ws;
  if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) < 0) {
    ERROR("cannot get window size: %m\n");
    return;
  }

  LINES = ws.ws_row;
  COLS = ws.ws_col;
  resizeterm(LINES, COLS);

  ev_feed_event(EV_A_ &refresh_timer_w, EV_TIMER);
}

static void sigint_cb(EV_P_ ev_signal *w, int revents)
{
  TRACE("handling signal %d `%s'\n", w->signum, strsignal(w->signum));

  ev_break(EV_A_ EVBREAK_ALL);
}
