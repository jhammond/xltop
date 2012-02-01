#include <malloc.h>
#include <string.h>
#include "sub.h"
#include "user.h"
#include "x_node.h"
#include "trace.h"

void sub_init(struct sub_node *s, struct k_node *k, struct user_conn *uc,
              void (*cb)(EV_P_ struct sub_node *, struct k_node *,
                         struct x_node *, struct x_node *, double *))
{
  memset(s, 0, sizeof(*s));

  list_add_tail(&s->s_x_link[0], &k->k_x[0]->x_sub_list);
  list_add_tail(&s->s_x_link[1], &k->k_x[1]->x_sub_list);
  list_add_tail(&s->s_k_link, &k->k_sub_list);
  list_add_tail(&s->s_u_link, &uc->uc_sub_list);
  s->s_cb = cb;
  s->s_u_conn = uc;
}

void sub_cancel(struct sub_node *s)
{
  list_del_init(&s->s_x_link[0]);
  list_del_init(&s->s_x_link[1]);
  list_del_init(&s->s_k_link);
  list_del_init(&s->s_u_link);

  /* ... */

  free(s);
}
