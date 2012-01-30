#ifndef _SUB_NODE_H_
#define _SUB_NODE_H_
#include "list.h"

#define S_ADMIN (1 << 0)

struct x_node;
struct k_node;

struct sub_node {
  struct list_head s_x_link[2];
  struct list_head s_k_link;
  struct list_head s_u_link; /* User conn. */
  void (*s_cb)(struct sub_node *s, struct k_node *k,
               struct x_node *x0, struct x_node *x1,
               double now, double *d);
  void *s_u; /* XXX Data/User conn. */
  int s_flags;
};

static inline int sub_is_admin(const struct sub_node *s)
{
  return s->s_flags & S_ADMIN;
}

int sub_init(struct sub_node *s, struct x_node *x0, struct x_node *x1,
             void (*cb)(), void *u);

void sub_cancel(struct sub_node *s); /* Must free s. */

#endif
