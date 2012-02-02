#ifndef _SUB_H_
#define _SUB_H_
#include "cl_types.h"

#define S_MAY_FOLLOW_ALL (1 << 0)

struct x_node;
struct k_node;
struct user_conn;

struct sub_node {
  struct list_head s_x_link[2];
  struct list_head s_k_link;
  struct list_head s_u_link; /* User conn. */
  struct user_conn *s_u_conn;
  uint64_t s_tid;
  void (*s_cb)(EV_P_ struct sub_node *, struct k_node *,
               struct x_node *, struct x_node *, double *);
  int s_flags;
};

static inline int sub_may_access(const struct sub_node *s, struct x_node *x)
{
  return s->s_flags & S_MAY_FOLLOW_ALL; /* || ... */
}

void sub_init(struct sub_node *s, struct k_node *k, struct user_conn *uc,
              uint64_t tid,
              void (*cb)(EV_P_ struct sub_node *, struct k_node *,
                         struct x_node *, struct x_node *, double *));

void sub_cancel(EV_P_ struct sub_node *s); /* Destroy and free. */

#endif
