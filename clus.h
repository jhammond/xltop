#ifndef _CLUS_H_
#define _CLUS_H_
#include "cl_conn.h"
#include "x_node.h"

#define clus_timeout 240

struct clus_node {
  void *c_auth;
  double c_interval;
  struct cl_conn c_conn;
  struct x_node c_x;
};

struct clus_node *clus_lookup(const char *name, int flags);

int
clus_connect(EV_P_ struct cl_conn *cc, struct ctl_data *cd);

#endif
