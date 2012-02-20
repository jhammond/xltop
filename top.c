#include <ev.h>
#include <unistd.h>
#include <sys/wait.h>
#include <ctype.h>
#include "cl_listen.h"
#include "botz.h"
#include "string1.h"
#include "x_node.h"
#include "k_heap.h"
#include "trace.h"
#include "query.h"

int q_x_parse(struct query *q, char *s)
{
  q->q_u.u_void_p = x_lookup_str(s);
  return 0;
}

static inline void k_top_spec_init(struct k_top *t)
{
  size_t i, n = 0;

  for (i = 0; i < NR_STATS && n < T_SPEC_LEN; i++)
    t->t_spec[n++] = offsetof(struct k_node, k_sum[i]);

  for (i = 0; i < NR_STATS && n < T_SPEC_LEN; i++)
    t->t_spec[n++] = offsetof(struct k_node, k_rate[i]);

  for (i = 0; i < NR_STATS && n < T_SPEC_LEN; i++)
    t->t_spec[n++] = offsetof(struct k_node, k_pending[i]);

  if (n < T_SPEC_LEN)
    t->t_spec[n++] = offsetof(struct k_node, k_t);
}

int q_k_top_parse(struct query *q, char *s)
{
  struct k_top *t = q->q_u.u_void_p;
  size_t n = 0;

  while (n < T_SPEC_LEN && s != NULL) {
    char *u = strsep(&s, ",");
    size_t i = atoi(u + 1);

    if (!(i < NR_STATS))
      return -1;

    switch (*u) {
    case 'p':
      t->t_spec[n++] = offsetof(struct k_node, k_pending[i]);
      break;
    case 'r':
      t->t_spec[n++] = offsetof(struct k_node, k_rate[i]);
      break;
    case 's':
      t->t_spec[n++] = offsetof(struct k_node, k_sum[i]);
      break;
    case 't':
      t->t_spec[n++] = offsetof(struct k_node, k_t);
      break;
    default:
      return -1;
    }
  }

  if (s != NULL)
    return -1;

  while (n < T_SPEC_LEN)
    t->t_spec[n++] = -1;

  return 0;
}

static void top_query_cb(EV_P_ struct botz_x *bx, struct n_buf *nb,
                         struct x_node *x0, struct x_node *x1, size_t d0, size_t d1,
                         size_t limit, struct k_top *top)
{
  struct k_heap *h = &top->t_h;
  size_t i;

  memset(h, 0, sizeof(*h));

  if (x0 == NULL) {
    bx->x_status = BOTZ_NOT_FOUND;
    goto out;
  }

  if (x1 == NULL) {
    bx->x_status = BOTZ_NOT_FOUND;
    goto out;
  }

  TRACE("x0 `%s', x1 `%s', d0 %zu, d1 %zu, limit %zu\n",
        x0->x_name, x1->x_name, d0, d1, limit);

  /* TODO AUTH. */

  if (k_heap_init(h, limit) < 0) {
    bx->x_status = BOTZ_INTERVAL_SERVER_ERROR;
    goto out;
  }

  k_heap_top(h, x0, d0, x1, d1, &k_top_cmp);
  k_heap_order(h, &k_top_cmp);

  for (i = 0; i < h->h_count; i++) {
    struct k_node *k = h->h_k[i];
    n_buf_printf(nb, PRI_K_NODE_FMT"\n", PRI_K_NODE_ARG(k));
  }

 out:
  k_heap_destroy(h);
}

static void top_get_cb(EV_P_ struct botz_x *bx, struct n_buf *nb)
{
#define TOP_QUERY_DESC(X)     \
  X(0, void_p, x0, NULL, q_x_parse, 1),     \
  X(1, void_p, x1, NULL, q_x_parse, 1),    \
  X(2, size, d0, 0, q_size_parse, 0),        \
  X(3, size, d1, 0, q_size_parse, 0),         \
  X(4, size, limit, 0, q_size_parse, 0), \
  X(5, void_p, sort, &top, q_k_top_parse, 0)

  struct k_top top;
  struct query q[] = { TOP_QUERY_DESC(QUERY_STRUCT) };

  k_top_spec_init(&top);

  if (query_parse(q, sizeof(q) / sizeof(q[0]), bx->x_query, 0) < 0) {
    bx->x_status = BOTZ_BAD_REQUEST;
    return;
  }

  top_query_cb(EV_A_ bx, nb, TOP_QUERY_DESC(QUERY_VALUE));
}

const struct botz_entry_ops top_entry_ops = {
  .o_method_ops = {
    [BOTZ_GET] = { .o_rsp_body_cb = &top_get_cb },
  }
};
