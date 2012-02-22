#include <unistd.h>
#include "x_botz.h"
#include "lnet.h"
#include "serv.h"
#include "string1.h"
#include "trace.h"

struct serv_node *
serv_create(const char *name, struct x_node *p, struct lnet_struct *l)
{
  size_t hash;
  struct hlist_head *head;
  struct x_node *x;
  struct serv_node *s;

  x = x_lookup_hash(X_SERV, name, &hash, &head);
  if (x != NULL)
    return container_of(x, struct serv_node, s_x);

  s = malloc(sizeof(*s) + strlen(name) + 1);
  if (s == NULL)
    return NULL;

  memset(s, 0, sizeof(*s));

  s->s_lnet = l;
  x_init(&s->s_x, X_SERV, p, hash, head, name);

  return s;
}

static void serv_msg_cb(EV_P_ struct serv_node *s, char *msg)
{
  struct x_node *x;
  char *nid;
  double d[NR_STATS];

  nid = wsep(&msg);
  if (nid == NULL || msg == NULL)
    return;

  TRACE("nid `%s', msg `%s'\n", nid, msg);

  x = lnet_lookup_nid(s->s_lnet, nid, L_CREATE);
  if (x == NULL)
    return;

  if (sscanf(msg, SCN_STATS_FMT("%lf"), SCN_STATS_ARG(d)) != NR_STATS)
    return;

  x_update(EV_A_ x, &s->s_x, d);
}

static void
serv_get_r(struct n_buf *nb, struct x_node *x0, struct x_node *sx, double now)
{
  struct k_node *k = k_lookup(x0, sx, 0);

  if (k == NULL)
    return;

  if (x0->x_type == &x_types[X_HOST]) {
    k_freshen(k, now);
    n_buf_printf(nb, PRI_K_NODE_FMT"\n", PRI_K_NODE_ARG(k));
  } else {
    struct x_node *c;

    x_for_each_child(c, x0)
      serv_get_r(nb, c, sx, now);
  }
}

static void serv_get_cb(EV_P_ struct botz_entry *e,
                              struct botz_request *q,
                              struct botz_response *r)
{
  struct serv_node *s = e->e_data;

  serv_get_r(&r->r_body, x_all[0], &s->s_x, ev_now(EV_A));
}

static void serv_put_cb(EV_P_ struct botz_entry *e,
                              struct botz_request *q,
                              struct botz_response *r)
{
  struct serv_node *s = e->e_data;
  char *msg;
  size_t msg_len;

  /* TODO AUTH. */

  s->s_modified = ev_now(EV_A);

  while (n_buf_get_msg(&q->q_body, &msg, &msg_len) == 0)
    serv_msg_cb(EV_A_ s, msg);
}

static void serv_info_cb(struct serv_node *s,
                         struct botz_request *q,
                         struct botz_response *r)
{
  if (q->q_method == BOTZ_GET) {
    n_buf_printf(&r->r_body,
                 "name: %s\n"
                 "interval: %f\n"
                 "offset: %f\n"
                 "modified: %f\n"
                 "load: %f %f %f\n"
                 "lnet: %s\n",
                 s->s_x.x_name,
                 s->s_interval,
                 s->s_offset,
                 s->s_modified,
                 s->s_load[0], s->s_load[1], s->s_load[2],
                 s->s_lnet->l_name);
    x_printf(&r->r_body, &s->s_x);
  } else {
    r->r_status = BOTZ_FORBIDDEN;
  }
}

static void serv_load_cb(struct serv_node *s,
                         struct botz_request *q,
                         struct botz_response *r)
{
  if (q->q_method == BOTZ_GET) {
    n_buf_printf(&r->r_body, "%f %f %f\n",
                 s->s_load[0], s->s_load[1], s->s_load[2]);
  } else if (q->q_method == BOTZ_PUT) {
    char *msg;
    size_t msg_len;
    double load[3];

    /* TODO AUTH. */
    if (n_buf_get_msg(&q->q_body, &msg, &msg_len) == 0 &&
        sscanf(msg, "%lf %lf %lf", &load[0], &load[1], &load[2]) == 3)
      memcpy(s->s_load, load, sizeof(load));
    else
      r->r_status = BOTZ_BAD_REQUEST;
  } else {
    r->r_status = BOTZ_FORBIDDEN;
  }
}

static struct botz_entry *
serv_entry_lookup_cb(EV_P_ struct botz_lookup *p,
                           struct botz_request *q,
                           struct botz_response *r)
{
  struct serv_node *s = p->p_entry->e_data;

  if (p->p_rest != NULL)
    return NULL;

  if (strcmp(p->p_name, "_info") == 0) {
    serv_info_cb(s, q, r);
    return BOTZ_RESPONSE_READY;
  }

  if (strcmp(p->p_name, "load") == 0) {
    serv_load_cb(s, q, r);
    return BOTZ_RESPONSE_READY;
  }

  return NULL;
}

static const struct botz_entry_ops serv_entry_ops = {
  .o_lookup = &serv_entry_lookup_cb,
  .o_method = {
    [BOTZ_GET] = &serv_get_cb,
    [BOTZ_PUT] = &serv_put_cb,
  }
};

static struct botz_entry *
serv_dir_lookup_cb(EV_P_ struct botz_lookup *p,
                         struct botz_request *q,
                         struct botz_response *r)
{
  struct x_node *x = x_lookup(X_SERV, p->p_name, NULL, 0);
  struct serv_node *s;

  TRACE("name `%s', x %p\n", p->p_name, x);
  if (x == NULL)
    return NULL;

  s = container_of(x, struct serv_node, s_x);

  return botz_new_entry(p->p_name, &serv_entry_ops, s);
}

static struct botz_entry_ops serv_dir_ops = {
  X_DIR_OPS_DEFAULT,
  .o_lookup = &serv_dir_lookup_cb,
};

int serv_type_init(void)
{
  return x_dir_init(X_SERV, &serv_dir_ops);
}
