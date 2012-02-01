#include <ev.h>
#include <unistd.h>
#include <sys/wait.h>
#include <ctype.h>
#include "string1.h"
#include "clus.h"
#include "job.h"
#include "sub.h"
#include "trace.h"

static int clus_msg_cb(EV_P_ struct cl_conn *cc, char *msg, size_t msg_len)
{
  struct clus_node *c = container_of(cc, struct clus_node, c_conn);
  struct x_node *x, *j0, *j1;
  struct sub_node *s, *t;

  char *host_name, *job_name, *owner, *title, *start;

  /* This must stay aligned with output of qhost_j_filer().
       HOST.DOMAIN JOB_ID@CLUSTER OWNER TITLE START_EPOCH
     Or if host is idle:
       HOST.DOMAIN IDLE@CLUSTER NONE NONE 0
  */

  if (split(&msg, &host_name, &job_name, &owner, &title, &start, NULL) != 5)
    return 0;

  /* No point in creating host if we didn't already do so when reading
     the nid files, but let's do it this way in case we change
     something elsewhere. */
  x = x_lookup(X_HOST, host_name, L_CREATE);
  if (x == NULL)
    return -1;

  j1 = x_job_lookup(job_name, &c->c_x, owner, title, start);
  if (j1 == NULL)
    return -1;

  j0 = x->x_parent;
  if (j0 == j1)
    return 0;

  /* Cancel subscriptions on x not owned by admin... */
  list_for_each_entry_safe(s, t, &x->x_sub_list, s_x_link[x_which(x)]) {
    if (!sub_may_follow(s, j1))
      sub_cancel(s);
  }

  x_set_parent(x, j1);

  if (j0 != NULL && x_is_job(j0) && x->x_nr_child == 0)
    x_job_end(EV_A_ j0);

  return 0;
}

static void clus_end_cb(EV_P_ struct cl_conn *cc, int err)
{
  struct clus_node *c = container_of(cc, struct clus_node, c_conn);

  TRACE("clus `%s' END err %d\n", c->c_x.x_name, err);

  cl_conn_close(EV_A_ cc);
}

static struct cl_conn_ops clus_conn_ops = {
  .cc_msg_cb = clus_msg_cb,
  .cc_end_cb = clus_end_cb,
  .cc_timeout = clus_timeout,
  .cc_rd_buf_size = 65536, /* XXX */
};

struct clus_node *clus_lookup(const char *name, int flags)
{
  size_t hash;
  struct hlist_head *head;
  struct x_node *x;
  struct clus_node *c;

  x = x_lookup_hash(X_CLUS, name, &hash, &head);
  if (x != NULL)
    return container_of(x, struct clus_node, c_x);

  c = malloc(sizeof(*c) + strlen(name) + 1);
  if (c == NULL)
    return NULL;

  memset(c, 0, sizeof(*c));

  /* TODO c_auth. */

  if (cl_conn_init(&c->c_conn, &clus_conn_ops) < 0)
    return NULL;

  x_init(&c->c_x, X_CLUS, NULL, hash, head, name);

  return c;
}
