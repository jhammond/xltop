#ifndef _SERV_H_
#define _SERV_H_
#include <ev.h>
#include "cl_conn.h"
#include "x_node.h"

struct lnet_struct;

struct serv_node {
  void *s_auth;
  double s_interval;
  struct cl_conn s_conn;
  struct lnet_struct *s_lnet;
  struct x_node s_x;
};

struct serv_node *
serv_create(const char *name, struct x_node *parent, struct lnet_struct *l);

int
serv_connect(EV_P_ struct cl_conn *cc, struct ctl_data *cd);

#endif
