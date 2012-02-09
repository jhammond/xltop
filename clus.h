#ifndef _CLUS_H_
#define _CLUS_H_
#include "cl_conn.h"
#include "x_node.h"

#define clus_timeout 240

struct job_node;

struct clus_node {
  void *c_auth;
  double c_interval, c_offset;
  struct cl_conn c_conn;
  struct job_node *c_idle_job;
  struct x_node c_x;
};

int clus_0_init(void);

struct clus_node *clus_lookup(const char *name, int flags);

struct clus_node *clus_lookup_for_host(const char *name);

int
clus_connect(EV_P_ struct cl_conn *cc, struct ctl_data *cd);

#endif
