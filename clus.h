#ifndef _CLUS_H_
#define _CLUS_H_
#include "x_node.h"

#define clus_timeout 240

struct job_node;

struct clus_node {
  void *c_auth;
  double c_interval, c_offset;
  struct job_node *c_idle_job;
  struct x_node c_x;
};

int clus_0_init(void);

struct clus_node *clus_lookup(const char *name, int flags);

struct clus_node *clus_lookup_for_host(const char *name);

#endif
