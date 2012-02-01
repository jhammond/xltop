#include <unistd.h>
#include "serv.h"
#include "lnet.h"
#include "string1.h"
#include "trace.h"

static int serv_msg_cb(EV_P_ struct cl_conn *cc, char *msg, size_t msg_len)
{
  struct serv_node *s = container_of(cc, struct serv_node, s_conn);
  struct x_node *x;
  char *nid;
  double d[NR_STATS];

  nid = wsep(&msg);
  if (nid == NULL || msg == NULL)
    return 0;

  TRACE("nid `%s', msg `%s'\n", nid, msg);

  x = lnet_lookup_nid(s->s_lnet, nid, L_CREATE);
  if (x == NULL)
    return 0; /* ENOMEN */

  /* ASSERT(NR_STATS == 3); */
  if (sscanf(msg, "%lf %lf %lf", &d[0], &d[1], &d[2]) != 3)
    return 0;

  x_update(EV_A_ x, &s->s_x, d);

  return 0;
}

static void serv_end_cb(EV_P_ struct cl_conn *cc, int err)
{
  struct serv_node *s = container_of(cc, struct serv_node, s_conn);

  TRACE("serv `%s' END err %d\n", s->s_x.x_name, err);

  cl_conn_close(EV_A_ cc);
}

static struct cl_conn_ops serv_conn_ops = {
  .cc_msg_cb = &serv_msg_cb,
  .cc_end_cb = &serv_end_cb,
  .cc_timeout = 7200,
  .cc_rd_buf_size = 65536,
};

struct serv_node *
serv_create(const char *name, struct x_node *parent, struct lnet_struct *l)
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

  cl_conn_init(&s->s_conn, &serv_conn_ops);
  s->s_lnet = l;
  x_init(&s->s_x, X_SERV, parent, hash, head, name);

  return s;
}
