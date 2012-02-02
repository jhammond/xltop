#ifndef _CL_CONN_H_
#define _CL_CONN_H_
#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include <ev.h>
#include "n_buf.h"

#define CL_CONN_CTL_CHAR '%'

enum {
  CL_ERR_OK = 1024,
  CL_ERR_ENDED,
  CL_ERR_INTERNAL,
  CL_ERR_MOVED,
  CL_ERR_NO_AUTH,
  CL_ERR_NO_CLUS,
  CL_ERR_NO_CTL,
  CL_ERR_NO_FS,
  CL_ERR_NO_HOST,
  CL_ERR_NO_JOB,
  CL_ERR_NO_MEM,
  CL_ERR_NO_SERV,
  CL_ERR_NO_USER,
  CL_ERR_NO_X,
  CL_ERR_NR_ARGS,
  CL_ERR_WHICH,
  CL_ERR_MAX_PLUS_1,
};

struct cl_conn;

/* cc_msg_cb, cc_ctl, and cc_end_cb are all optional.
   If cc_end_cb is not NULL then cc will be stopped before it gets
   called.  If it is NULL then cc will be stopped, destroyed and
   freed. */

struct ctl_data {
  char *cd_name, *cd_args;
  uint64_t cd_tid;
  int cd_err;
};

struct cl_conn_ctl {
  int (*cc_ctl_cb)(EV_P_ struct cl_conn *, struct ctl_data *);
  char *cc_ctl_name;
};

struct cl_conn_ops {
  int (*cc_msg_cb)(EV_P_ struct cl_conn *, char *);
  struct cl_conn_ctl *cc_ctl;
  size_t cc_nr_ctl;
  void (*cc_end_cb)(EV_P_ struct cl_conn *cc, int err);
  double cc_timeout;
  size_t cc_rd_buf_size, cc_wr_buf_size;
};

struct cl_conn {
  char *cc_name;
  struct ev_timer cc_timer_w;
  struct ev_io cc_io_w;
  struct n_buf cc_rd_buf, cc_wr_buf;
  const struct cl_conn_ops *cc_ops;
  unsigned int cc_rd_eof:1;
};

static inline const char *cl_conn_name(struct cl_conn *cc)
{
  return cc->cc_name != NULL ? cc->cc_name : "NONE";
}

int cl_conn_init(struct cl_conn *cc, const struct cl_conn_ops *ops);
int cl_conn_set(struct cl_conn *cc, int fd, int events, const char *name);
void cl_conn_start(EV_P_ struct cl_conn *cc);
void cl_conn_stop(EV_P_ struct cl_conn *cc);
int cl_conn_move(EV_P_ struct cl_conn *cc, struct cl_conn *src);
void cl_conn_close(EV_P_ struct cl_conn *cc);
void cl_conn_destroy(struct cl_conn *cc);

int cl_conn_writef(EV_P_ struct cl_conn *cc, const char *fmt, ...)
  __attribute__((format (printf, 3, 4)));

#endif
