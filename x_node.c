#include <math.h>
#include <string.h>
#include "trace.h"
#include "x_node.h"

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
  [X_CLUSTER] = {
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

static inline int x_which(struct x_node *x)
{
  return x->x_ops->x_which;
}

void x_init(struct x_node *x, int type, struct x_node *parent, size_t hash,
            struct hlist_head *hash_head, const char *name)
{
  memset(x, 0, sizeof(*x));

  x->x_ops = &x_ops[type];
  x->x_ops->x_nr++;

  if (parent == NULL)
    /* INIT_LIST_HEAD(&x->x_link); */
    parent = x_top[x_which(x)];

  x->x_parent = parent;
  list_add(&x->x_link, &parent->x_child_list);
  x->x_parent->x_nr_child++;

  INIT_LIST_HEAD(&x->x_child_list);
  x->x_hash = hash;

  /* FIXME We don't look to see if name is already hashed. */
  if (hash_head == NULL) {
    struct hash_table *ht = &x->x_ops->x_hash_table;
    hash_head = ht->ht_table + (hash & ht->ht_mask);
  }

  hlist_add_head(&x->x_hash_node, hash_head);
  strcpy(x->x_name, name);
}

void x_destroy(struct x_node *x)
{
  struct x_node *c, *t;

  if (x_which(x) == 0)
    k_destroy(x, x_top[1], 0);
  else
    k_destroy(x_top[0], x, 1);

  if (x->x_parent != NULL)
    x->x_parent->x_nr_child--;
  x->x_parent = NULL;

  list_del(&x->x_link); /* _init */

  x->x_nr_child = 0;
  x_for_each_child_safe(c, t, x) {
    c->x_parent = NULL;
    list_del_init(&c->x_link);
  }

  hlist_del(&x->x_hash_node);

  x->x_ops->x_nr--;
  memset(x, 0, sizeof(*x));
}

struct x_node *x_lookup(int type, const char *name, int flags)
{
  struct hash_table *ht = &x_ops[type].x_hash_table;
  size_t hash = str_hash(name, 64); /* XXX */
  struct hlist_head *hash_head = ht->ht_table + (hash & ht->ht_mask);
  struct hlist_node *hash_node;
  struct x_node *x;

  hlist_for_each_entry(x, hash_node, hash_head, x_hash_node) {
    if (strcmp(name, x->x_name) == 0)
      return x;
  }

  if (!(flags & L_CREATE)) /* L_EXCLUSIVE */
    return NULL;

  x = malloc(sizeof(*x) + strlen(name) + 1);
  if (x == NULL)
    return NULL;

  x_init(x, type, NULL, hash, hash_head, name);

  return x;
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

void x_update(struct x_node *x0, struct x_node *x1, double now, double *v)
{
  struct x_node *i0, *i1;
  struct k_node *k;

  TRACE("%s %s %f %f %f %f\n",
        x0->x_name, x1->x_name, now, v[0], v[1], v[2]);

  for (i0 = x0; 0 != NULL; i0 = i0->x_parent) {
    for (i1 = x1; i1 != NULL; i1 = i1->x_parent) {
      k = k_lookup(i0, i1, L_CREATE);

      if (k != NULL)
        k_update(k, now, v);
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

  k = malloc(sizeof(*k));
  if (k == NULL)
    OOM();

  memset(k, 0, sizeof(*k));
  k->k_x[0] = x0;
  k->k_x[1] = x1;
  nr_k++;
  hlist_add_head(&k->k_hash_node, hash_head);

  return k;
}

void k_destroy(struct x_node *x0, struct x_node *x1, int which)
{
  struct k_node *k = k_lookup(x0, x1, 0);
  struct x_node *c;

  if (k == NULL)
    return;

  /* subn */
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

void k_update(struct k_node *k, double now, double *v)
{
  int i, j, nr_ticks;

  TRACE("%s %s %f %f %f %f\n",
        k->k_x[0]->x_name, k->k_x[1]->x_name, now, v[0], v[1], v[2]);

  if (k->k_tstamp <= 0)
    k->k_tstamp = now;

  nr_ticks = (now - k->k_tstamp) / K_TICK;
  k->k_tstamp += nr_ticks * K_TICK;

  for (i = 0; i < NR_STATS; i++) {
    /* FIXME Check this and make it suck less. */
    for (j = 0; j < nr_ticks; j++) {
      double r = k->k_stats[i].k_pending / K_TICK;

      if (k->k_stats[i].k_rate <= 0)
        k->k_stats[i].k_rate = r;
      else
        k->k_stats[i].k_rate += K_ALPHA * (r - k->k_stats[i].k_rate);

      k->k_stats[i].k_pending = 0;
    }

    k->k_stats[i].k_count += v[i];
    k->k_stats[i].k_pending += v[i];
  }
}
