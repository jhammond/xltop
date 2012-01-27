#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <ev.h>
#include "trace.h"
#include "cl_conn.h"

static int msg_cb(EV_P_ struct cl_conn *cc, char *msg, size_t msg_len)
{
  TRACE("cl_conn `%s' MSG `%s', len %zu\n", cl_conn_name(cc), msg, msg_len);

  return 0;
}

static int ctl_1_cb(EV_P_ struct cl_conn *cc, char *msg, size_t msg_len)
{
  TRACE("cl_conn `%s' CTL_1 `%s', len %zu\n", cl_conn_name(cc), msg, msg_len);

  return 0;
}

static int ctl_2_cb(EV_P_ struct cl_conn *cc, char *msg, size_t msg_len)
{
  TRACE("cl_conn `%s' CTL_2 `%s', len %zu\n", cl_conn_name(cc), msg, msg_len);

  return 0;
}

static int ctl_3_cb(EV_P_ struct cl_conn *cc, char *msg, size_t msg_len)
{
  TRACE("cl_conn `%s' CTL_3 `%s', len %zu\n", cl_conn_name(cc), msg, msg_len);

  return EPERM;
}

static void end_cb(EV_P_ struct cl_conn *cc, int err)
{
  TRACE("cl_conn `%s' CLOSE %d `%s'\n", cl_conn_name(cc), err, strerror(err));
  cl_conn_destroy(cc);
  free(cc);
}

static struct cl_conn_ctl ctl_table[] = {
  { .cc_ctl_cb = &ctl_1_cb, .cc_ctl_name = "CTL_1" },
  { .cc_ctl_cb = &ctl_2_cb, .cc_ctl_name = "CTL_2" },
  { .cc_ctl_cb = &ctl_3_cb, .cc_ctl_name = "CTL_3" },
};

static struct cl_conn_ops ops = {
  .cc_msg_cb = &msg_cb,
  .cc_ctl = ctl_table,
  .cc_nr_ctl = sizeof(ctl_table) / sizeof(ctl_table[0]),
  .cc_end_cb = &end_cb,
  .cc_timeout = 10,
  .cc_rd_buf_size = 4096,
  .cc_wr_buf_size = 4096,
};

int main(int argc, char *argv[])
{
  struct cl_conn *cc = NULL;
  const char *path;
  int fd;

  if (argc < 2)
    FATAL("usage %s PATH\n", program_invocation_short_name);

  path = argv[1];
  fd = open(path, O_RDWR|O_NONBLOCK);
  if (fd < 0)
    FATAL("cannot open `%s': %m\n", path);

  signal(SIGPIPE, SIG_IGN);

  while (1) {
    cc = malloc(sizeof(*cc));
    if (cc == NULL)
      OOM();

    cl_conn_init(cc, &ops);
    cl_conn_set(cc, dup(fd), EV_READ|EV_WRITE, path);
    cl_conn_start(EV_DEFAULT_ cc);

    ev_run(EV_DEFAULT_ 0);

    sleep(10);
  }

  return 0;
}
