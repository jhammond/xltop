#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ev.h>
#include "trace.h"
#include "cl_bind.h"
#include "cl_conn.h"
#include "string1.h"
#include "container_of.h"
#include "fd.h"

static int msg_cb(EV_P_ struct cl_conn *cc, char *msg, size_t msg_len)
{
  TRACE("cl_conn `%s' MSG `%s', len %zu\n", cl_conn_name(cc), msg, msg_len);

  return 0;
}

static int
ctl_1_cb(EV_P_ struct cl_conn *cc, char *ctl, char *msg, size_t msg_len)
{
  TRACE("cl_conn `%s' CTL_1 msg `%s', len %zu\n", cl_conn_name(cc), msg, msg_len);

  return 0;
}

static int
ctl_2_cb(EV_P_ struct cl_conn *cc, char *ctl, char *msg, size_t msg_len)
{
  TRACE("cl_conn `%s' CTL_2 `%s', len %zu\n", cl_conn_name(cc), msg, msg_len);

  return 0;
}

static int
ctl_3_cb(EV_P_ struct cl_conn *cc, char *ctl, char *msg, size_t msg_len)
{
  TRACE("cl_conn `%s' CTL_3 `%s', len %zu\n", cl_conn_name(cc), msg, msg_len);

  return EPERM;
}

static int
ctl_echo_cb(EV_P_ struct cl_conn *cc, char *ctl, char *msg, size_t msg_len)
{
  TRACE("cl_conn `%s' CTL_3 `%s', len %zu\n", cl_conn_name(cc), msg, msg_len);

  cl_conn_writef(EV_A_ cc, "%cECHO %s\n", CL_CONN_CTL_CHAR, msg);

  return 0;
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
  { .cc_ctl_cb = &ctl_echo_cb, .cc_ctl_name = "ECHO" },
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
  struct cl_bind *cb[3];
  int i;

  for (i = 0; i < sizeof(cb) / sizeof(cb[0]); i++) {
    cb[i] = malloc(sizeof(*cb[i]));

    cl_bind_init(cb[i], &ops);
    if (cl_bind_set(cb[i], NULL, NULL) < 0)
      FATAL("cl_bind_set failed\n");
  }

  for (i = 0; i < sizeof(cb) / sizeof(cb[0]); i++)
    cl_bind_start(EV_DEFAULT_ cb[i]);

  ev_run(EV_DEFAULT_ 0);

  return 0;
}
