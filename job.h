#ifndef _JOB_H_
#define _JOB_H_
#include <ev.h>
#include "x_node.h"

struct job_node {
  char *j_owner, *j_title;
  double j_start_time;
  struct ev_timer j_zombie_w;
  struct x_node j_x;
};

static inline int x_is_job(const struct x_node *x)
{
  return x->x_type == &x_types[X_JOB];
}

/* L_CREATE is implied. */
struct x_node *x_job_lookup(const char *name /* jobid */,
                            struct x_node *parent,
                            const char *owner,
                            const char *title /* user's name for job */,
                            const char *start);

void x_job_end(EV_P_ struct x_node *x);

#endif
