#include <malloc.h>
#include <string.h>
#include "sub_node.h"
#include "x_node.h"
#include "trace.h"

int sub_init(struct sub_node *s, struct x_node *x0, struct x_node *x1,
              void (*cb)(), void *u)
{
  struct k_node *k;

  memset(s, 0, sizeof(*s));

  k = k_lookup(x0, x1, L_CREATE);
  if (k == NULL)
    return -1;

  list_add_tail(&s->s_x_link[0], &x0->x_sub_list);
  list_add_tail(&s->s_x_link[1], &x1->x_sub_list);
  list_add_tail(&s->s_k_link, &k->k_sub_list);
  INIT_LIST_HEAD(&s->s_u_link); /* XXX */
  s->s_cb = cb;
  s->s_u = u;

  return 0;
}

void sub_cancel(struct sub_node *s)
{
  list_del_init(&s->s_x_link[0]);
  list_del_init(&s->s_x_link[1]);
  list_del_init(&s->s_k_link);
  list_del_init(&s->s_u_link);

  free(s);
}
