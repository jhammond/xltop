#ifndef _X_NODE_H_
#define _X_NODE_H_
#include <stddef.h>
#include "list.h"
#include "hash.h"

#define NR_STATS 3 /* MOVEME */

#define L_CREATE (1 << 0)
/* TODO #define L_EXCLUSIVE (1 << 1) */

#define X_HOST     0
#define X_JOB      1
#define X_CLUSTER  2
#define X_TOP_0    3
#define X_SERV     4
#define X_FS       5
#define X_TOP_1    6

#define X_TOP_0_NAME "ALL@ALL"
#define X_TOP_1_NAME "ALL"

#define K_TICK 10.0
#define K_WINDOW 600.0
#define K_ALPHA (1 - exp(- K_TICK / K_WINDOW))

struct x_node_ops {
  struct hash_table x_hash_table;
  size_t x_nr, x_nr_hint;
  int x_which;
};

struct x_node {
  struct x_node_ops *x_ops;
  struct x_node *x_parent;
  struct list_head x_link;
  size_t x_nr_child;
  struct list_head x_child_list;
  size_t x_hash;
  struct hlist_node x_hash_node;
  char x_name[];
};

extern struct x_node_ops x_ops[];
extern struct x_node *x_top[2];

struct k_node {
  struct x_node *k_x[2];
  struct hlist_node k_hash_node;
  double k_tstamp;
  struct {
    double k_count, k_pending, k_rate;
  } k_stats[NR_STATS];
};

extern size_t nr_k;
extern struct hash_table k_hash_table;

int x_ops_init(void);
void x_init(struct x_node *x, int type, struct x_node *parent, size_t hash,
            struct hlist_head *hash_head, const char *name);
struct x_node *x_lookup(int type, const char *name, int flags);
void x_update(struct x_node *x0, struct x_node *x1, double now, double *v);
void x_destroy(struct x_node *x);

#define x_for_each_child(c, x)                          \
  list_for_each_entry(c, &((x)->x_child_list), x_link)

#define x_for_each_child_safe(c, t, x)                          \
  list_for_each_entry_safe(c, t, &((x)->x_child_list), x_link)

struct k_node *k_lookup(struct x_node *x0, struct x_node *x1, int flags);
void k_destroy(struct x_node *x0, struct x_node *x1, int which);
void k_update(struct k_node *k, double now, double *v);

#endif
