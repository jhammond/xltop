#include <math.h>
#include <string.h>
#include "trace.h"
#include "x_node.h"
#include "sub.h"

size_t nr_k;
struct hash_table k_hash_table;

/* TODO Move default hints to a header. */

struct x_node *x_top[2];

struct x_node_ops x_ops[] = {
  [X_HOST] = {
    .x_nr_hint = 4096,
    .x_which = 0,
  },
  [X_JOB] = {
    .x_nr_hint = 256,
    .x_which = 0,
  },
  [X_CLUS] = {
    .x_nr_hint = 1,
    .x_which = 0,
  },
  [X_TOP_0] = {
    .x_nr_hint = 1,
    .x_which = 0,
  },
  [X_SERV] = {
    .x_nr_hint = 128,
    .x_which = 1,
  },
  [X_FS] = {
    .x_nr_hint = 1,
    .x_which = 1,
  },
  [X_TOP_1] = {
    .x_nr_hint = 1,
    .x_which = 1,
  },
};

void x_init(struct x_node *x, int type, struct x_node *parent, size_t hash,
            struct hlist_head *hash_head, const char *name)
{
  memset(x, 0, sizeof(*x));

  x->x_ops = &x_ops[type];
  x->x_ops->x_nr++;

  if (parent == NULL && type != X_TOP_0 && type != X_TOP_1)
    parent = x_top[x_which(x)];

  if (parent == NULL) {
    INIT_LIST_HEAD(&x->x_parent_link);
  } else {
    parent->x_nr_child++;
    list_add_tail(&x->x_parent_link, &parent->x_child_list);
    x->x_parent = parent;
  }

  INIT_LIST_HEAD(&x->x_child_list);
  INIT_LIST_HEAD(&x->x_sub_list);

  x->x_hash = hash;

  /* FIXME We don't look to see if name is already hashed. */
  if (hash_head == NULL) {
    struct hash_table *ht = &x->x_ops->x_hash_table;
    hash_head = ht->ht_table + (hash & ht->ht_mask);
  }

  hlist_add_head(&x->x_hash_node, hash_head);
  strcpy(x->x_name, name);
}

void x_set_parent(struct x_node *x, struct x_node *p)
{
  if (x->x_parent == p)
    return;

  if (x->x_parent != NULL) {
    list_del_init(&x->x_parent_link);
    x->x_parent->x_nr_child--;
  }

  if (p != NULL) {
    ASSERT(x_which(x) == x_which(p));
    list_move_tail(&x->x_parent_link, &p->x_child_list);
    p->x_nr_child++;
  }

  x->x_parent = p;
}

/* TODO s/k_destroy/k_destroy_and_free_rec.../ */

void x_destroy(struct x_node *x)
{
  struct x_node *c, *t;
  struct sub_node *s, *u;

  if (x_which(x) == 0)
    k_destroy(x, x_top[1], 0);
  else
    k_destroy(x_top[0], x, 1);

  if (x->x_parent != NULL)
    x->x_parent->x_nr_child--;
  x->x_parent = NULL;

  list_del_init(&x->x_parent_link);

  x_for_each_child_safe(c, t, x)
    x_set_parent(c, x_top[x_which(x)]);

  ASSERT(x->x_nr_child == 0);

  /* Do we need this with k_destroy() above? */
  list_for_each_entry_safe(s, u, &x->x_sub_list, s_x_link[x_which(x)])
    sub_cancel(s);

  hlist_del(&x->x_hash_node);

  x->x_ops->x_nr--;
  memset(x, 0, sizeof(*x));
}

/* FIXME x_lookup() may return orphans. */

struct x_node *x_lookup(int type, const char *name, int flags)
{
  struct hash_table *ht = &x_ops[type].x_hash_table;
  size_t hash = str_hash(name, 64); /* XXX */
  struct hlist_head *head = ht->ht_table + (hash & ht->ht_mask);
  struct hlist_node *node;
  struct x_node *x;

  hlist_for_each_entry(x, node, head, x_hash_node) {
    if (strcmp(name, x->x_name) == 0)
      return x;
  }

  if (!(flags & L_CREATE)) /* TODO L_EXCLUSIVE */
    return NULL;

  x = malloc(sizeof(*x) + strlen(name) + 1);
  if (x == NULL)
    return NULL;

  x_init(x, type, NULL, hash, head, name);

  return x;
}

struct x_node *x_lookup_hash(int type, const char *name,
                             size_t *hash_ref, struct hlist_head **head_ref)
{
  struct hash_table *ht = &x_ops[type].x_hash_table;
  size_t hash = str_hash(name, 64); /* XXX */
  struct hlist_head *head = ht->ht_table + (hash & ht->ht_mask);
  struct hlist_node *node;
  struct x_node *x;

  hlist_for_each_entry(x, node, head, x_hash_node) {
    if (strcmp(name, x->x_name) == 0)
      return x;
  }

  *hash_ref = hash;
  *head_ref = head;
  return NULL;
}

int x_ops_init(void)
{
  size_t i, nr_x_types = sizeof(x_ops) / sizeof(x_ops[0]);
  size_t nr[2] = { 0, 0 };
  size_t k_nr_hint;

  for (i = 0; i < nr_x_types; i++) {
    if (hash_table_init(&x_ops[i].x_hash_table, x_ops[i].x_nr_hint) < 0)
      return -1;
    nr[x_ops[i].x_which] += x_ops[i].x_nr_hint;
  }

  for (i = 0; i < 2; i++) {
    x_top[i] = x_lookup((i == 0) ? X_TOP_0 : X_TOP_1,
                        (i == 0) ? X_TOP_0_NAME : X_TOP_1_NAME,
                        L_CREATE);

    if (x_top[i] == NULL)
      return -1;
  }

  k_nr_hint = nr[0] * nr[1];
  TRACE("nr %zu %zu, k_nr_hint %zu\n", nr[0], nr[1], k_nr_hint);

  if (hash_table_init(&k_hash_table, k_nr_hint) < 0)
    return -1;

  return 0;
}

void x_update(struct x_node *x0, struct x_node *x1, double now, double *d)
{
  struct x_node *i0, *i1;
  struct k_node *k;

  for (i0 = x0; i0 != NULL; i0 = i0->x_parent) {
    for (i1 = x1; i1 != NULL; i1 = i1->x_parent) {
      k = k_lookup(i0, i1, L_CREATE);

      if (k != NULL)
        k_update(k, x0, x1, now, d);
    }
  }
}

struct k_node *k_lookup(struct x_node *x0, struct x_node *x1, int flags)
{
  struct hash_table *ht = &k_hash_table;
  size_t hash = pair_hash(x0->x_hash, x1->x_hash, ht->ht_shift);
  struct hlist_head *hash_head = ht->ht_table + (hash & ht->ht_mask);
  struct hlist_node *hash_node;
  struct k_node *k;

  hlist_for_each_entry(k, hash_node, hash_head, k_hash_node) {
    if (k->k_x[0] == x0 && k->k_x[1] == x1)
      return k;
  }

  if (!(flags & L_CREATE))
    return NULL;

  if (x_which(x0) != 0 || x_which(x1) != 1) {
    errno = EINVAL;
    return NULL;
  }

  k = malloc(sizeof(*k));
  if (k == NULL)
    return NULL;

  /* k_init() */
  memset(k, 0, sizeof(*k));
  hlist_add_head(&k->k_hash_node, hash_head);
  k->k_x[0] = x0;
  k->k_x[1] = x1;
  INIT_LIST_HEAD(&k->k_sub_list);
  nr_k++;

  return k;
}

void k_destroy(struct x_node *x0, struct x_node *x1, int which)
{
  struct k_node *k = k_lookup(x0, x1, 0);
  struct x_node *c;
  struct sub_node *s, *t;

  if (k == NULL)
    return;

  list_for_each_entry_safe(s, t, &k->k_sub_list, s_k_link)
    sub_cancel(s);

  hlist_del(&k->k_hash_node);
  free(k);
  nr_k--;

  if (which == 0) {
    x_for_each_child(c, x1)
      k_destroy(x0, c, which);
  } else {
    x_for_each_child(c, x0)
      k_destroy(c, x1, which);
  }
}

void k_update(struct k_node *k, struct x_node *x0, struct x_node *x1, double now, double *d)
{
  double n, r;
  int i;
  struct sub_node *s;

  TRACE("%s %s, k_t %f, now %f, d %f %f %f\n",
        k->k_x[0]->x_name, k->k_x[1]->x_name, k->k_t, now, d[0], d[1], d[2]);

  if (k->k_t <= 0)
    k->k_t = now;

  n = floor((now - k->k_t) / K_TICK);
  k->k_t += fmax(n, 0) * K_TICK;

  for (i = 0; i < NR_STATS; i++) {
    if (n > 0) {
      /* Apply pending. */
      r = k->k_pending[i] / K_TICK;
      k->k_pending[i] = 0;

      /* TODO (n > K_TICKS_HUGE || k_rate < K_RATE_EPS) */
      if (k->k_rate[i] <= 0)
        k->k_rate[i] = r;
      else
        k->k_rate[i] += expm1(-K_TICK / K_WINDOW) * (k->k_rate[i] - r);
    }


    if (n > 1)
      /* Decay rate for missed intervals. */
      k->k_rate[i] *= exp((n - 1) * (-K_TICK / K_WINDOW));

    k->k_sum[i] += d[i];
    k->k_pending[i] += d[i];

    /* TRACE("now %8.3f, t %8.3f, p %12f, A %12f %12e\n", now, t, p, A, A); */
  }

  list_for_each_entry(s, &k->k_sub_list, s_k_link)
    (*s->s_cb)(s, k, x0, x1, now, d);
}
