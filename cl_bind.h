#ifndef _CL_BIND_H_
#define _CL_BIND_H_
#include <sys/socket.h>
#include <ev.h>
#include "cl_bind.h"
#include "list.h"

extern struct list_head cl_bind_list;

struct cl_bind {
  char *cb_ni_host, *cb_ni_port;
  const struct cl_conn_ops *cb_conn_ops;
  struct list_head cb_link;
  struct ev_io cb_io_w;
  struct sockaddr *cb_addr;
  socklen_t cb_addrlen;
};

void cl_bind_init(struct cl_bind *cb, const struct cl_conn_ops *ops);

void cl_bind_destroy(struct cl_bind *cb);

int cl_bind_set(struct cl_bind *cb, const char *host, const char *port);

void cl_bind_start(EV_P_ struct cl_bind *cb);

#endif
