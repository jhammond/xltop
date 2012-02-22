#include "x_botz.h"
#include "string1.h"

struct botz_listen x_listen;

void x_printf(struct n_buf *nb, struct x_node *x)
{
  n_buf_printf(nb,
               "x_name: %s\n"
               "x_type: %s\n"
               "x_parent: %s\n"
               "x_parent_type: %s\n"
               "x_nr_child: %zu\n"
               "x_hash: %016zx\n",
               x->x_name,
               x->x_type->x_type_name,
               x->x_parent != NULL ? x->x_parent->x_name : "NONE",
               x->x_parent != NULL ? x->x_parent->x_type->x_type_name : "NONE",
               x->x_nr_child,
               x->x_hash);
}

struct botz_entry *x_dir_lookup_cb(EV_P_ struct botz_lookup *p,
                                         struct botz_request *q,
                                         struct botz_response *r)
{
  struct x_type *type = p->p_entry->e_data;
  struct x_node *x = x_lookup(type->x_type, p->p_name, NULL, 0);

  if (x == NULL)
    return NULL;

  if ((p->p_name = pathsep(&p->p_rest)) == NULL) {
    /* TODO Generic x_node GET.  For now return OK. */
    return BOTZ_RESPONSE_READY;
  }

  if (p->p_rest != NULL)
    return NULL;

  if (strcmp(p->p_name, "_info") == 0) {
    if (q->q_method == BOTZ_GET)
      x_printf(&r->r_body, x);
    else
      r->r_status = BOTZ_FORBIDDEN;
    return BOTZ_RESPONSE_READY;
  }

  return NULL;
}

void x_dir_get_cb(EV_P_ struct botz_entry *e,
                        struct botz_request *q,
                        struct botz_response *r)
{
  struct x_type *type = e->e_data;
  struct hash_table *t = &type->x_hash_table;
  struct hlist_node *node;
  struct x_node *x;

  size_t i;
  for (i = 0; i < (1ULL << t->t_shift); i++)
    hlist_for_each_entry(x, node, t->t_table + i, x_hash_node)
      n_buf_printf(&r->r_body, "%s\n", x->x_name);
}

static const struct botz_entry_ops x_dir_ops_default = {
  X_DIR_OPS_DEFAULT,
};

int x_dir_init(int i, const struct botz_entry_ops *ops)
{
  const char *name = x_types[i].x_type_name;
  void *data = &x_types[i];

  if (ops == NULL)
    ops = &x_dir_ops_default;

  if (botz_add(&x_listen, name, ops, data) < 0) {
    ERROR("cannot add x_dir `%s': %m\n", name);
    return -1;
  }

  return 0;
}
