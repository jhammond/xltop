#include <signal.h>
#include <ncurses.h>
#include <termios.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/ioctl.h>
#include <ev.h>
#include "screen.h"
#include "trace.h"
#include "fd.h"

static struct ev_timer refresh_timer_w;
static struct ev_io stdin_io_w;
static struct ev_signal sigint_w;
static struct ev_signal sigterm_w;
static struct ev_signal sigwinch_w;

static void refresh_timer_cb(EV_P_ struct ev_timer *w, int revents);
static void stdin_io_cb(EV_P_ struct ev_io *w, int revents);
static void sigint_cb(EV_P_ ev_signal *w, int revents);
static void sigwinch_cb(EV_P_ ev_signal *w, int revents);

int screen_init(void)
{
  fd_set_nonblock(0);
  /* fd_set_cloexec(0); */

  ev_timer_init(&refresh_timer_w, &refresh_timer_cb, 0.2, 0.2);
  ev_io_init(&stdin_io_w, &stdin_io_cb, 0, EV_READ);
  ev_signal_init(&sigint_w, &sigint_cb, SIGINT);
  ev_signal_init(&sigterm_w, &sigint_cb, SIGTERM);
  ev_signal_init(&sigwinch_w, &sigwinch_cb, SIGWINCH);

  return 0;
}

void screen_start(EV_P)
{
  /* Begin curses magic. */
  if (initscr() == NULL)
    FATAL("cannot initialize screen: %m\n");

  cbreak();
  noecho();
  nonl();
  intrflush(stdscr, 0);
  keypad(stdscr, 1);
  nodelay(stdscr, 1);

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
  char buf[4096];
  static int i = -1, j = -1, di = 1, dj = 1;
  int n;

  /* TRACE("LINES %d, COLS %d\n", LINES, COLS); */

  n = snprintf(buf, sizeof(buf), "UPDATE %ld", (long) ev_now(EV_A));

  i += di;
  if (i <= 0) {
    i = 0;
    di = 1;
  } else if (i + n >= COLS) {
    i = COLS - n;
    di = -1;
  }

  j += dj;
  if (j <= 0) {
    j = 0;
    dj = 1;
  } else if (j >= LINES - 1) {
    j = LINES - 1;
    dj = -1;
  }

  erase();
  mvaddnstr(j, i, buf, -1);

#if 0
  struct job_struct **job_list = NULL;
  job_list = calloc(nr_jobs, sizeof(job_list[0]));
  if (job_list == NULL)
    OOM();

  size_t i = 0, j = 0;
  char *name;
  while ((name = dict_for_each(&name_job_dict, &i)) != NULL && j < nr_jobs) {
    GET_NAMED(job_list[j], j_name, name);
    j++;
  }

  if (j != nr_jobs)
    FATAL("internal error: expected %zu jobs, but found %zu\n", nr_jobs, j);

  qsort(job_list, nr_jobs, sizeof(job_list[0]), &job_stats_cmp);

  for (j = 0; j < nr_jobs && j < LINES; j++) {
    struct job_struct *job = job_list[j];
    long *s = job->j_stats;

    char buf[4096];
    snprintf(buf, sizeof(buf), "%s "PRI_STATS_FMT"\n", job->j_name,
	     PRI_STATS_ARG(s));
    mvaddnstr(j, 0, buf, -1);
  }
  free(job_list);
#endif

  refresh();
}

static void stdin_io_cb(EV_P_ ev_io *w, int revents)
{
  int c = getch();
  if (c == ERR || !isascii(c))
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
  if (ioctl(0, TIOCGWINSZ, &ws) < 0) {
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
