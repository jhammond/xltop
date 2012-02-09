#include <ev.h>
#include <unistd.h>
#include <sys/wait.h>
#include <ctype.h>
#include "string1.h"
#include "clus.h"
#include "job.h"
#include "sub.h"
#include "trace.h"

#define IDLE_JOBID "IDLE"
#define CLUS_0_NAME "UNKNOWN"
static struct clus_node *clus_0; /* Default/unknown cluster. */

int clus_0_init(void)
{
  clus_0 = clus_lookup(CLUS_0_NAME, L_CREATE);
  if (clus_0 == NULL) {
    ERROR("cannot create cluster `%s': %m\n", CLUS_0_NAME);
    return -1;
  }

  return 0;
}

static int clus_msg_cb(EV_P_ struct cl_conn *cc, char *msg)
{
  struct clus_node *c = container_of(cc, struct clus_node, c_conn);
  struct x_node *p, *x;
  struct job_node *j;
  struct sub_node *s, *t;

  char *host_name, *job_name, *owner = "NONE", *title = "NONE", *start = "0";

  /* This must stay aligned with output of qhost_j_filer().
       HOST.DOMAIN JOBID@CLUSTER OWNER TITLE START_EPOCH
     Or if host is idle:
       HOST.DOMAIN IDLE@CLUSTER
  */

  if (split(&msg, &host_name, &job_name, &owner, &title, &start,
            (char **) NULL) < 2)
    return 0;

  j = job_lookup(job_name, &c->c_x, owner, title, start);
  if (j == NULL)
    return 0;

  x = x_lookup(X_HOST, host_name, &j->j_x, L_CREATE);
  if (x == NULL)
    return 0;

  p = x->x_parent;
  if (p == &j->j_x)
    return 0;

  /* Cancel subscriptions on x that are not allowed to access j1. */
  list_for_each_entry_safe(s, t, &x->x_sub_list, s_x_link[x_which(x)])
    if (!sub_may_access(s, &j->j_x))
      sub_cancel(EV_A_ s);

  x_set_parent(x, &j->j_x);

  TRACE("clus set `%s' parent `%s'\n", x->x_name, j->j_x.x_name);

  if (p != NULL && x_is_job(p) && p->x_nr_child == 0)
    job_end(EV_A_ container_of(p, struct job_node, j_x));

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
  struct clus_node *c = NULL;
  char *idle_job_name = NULL;

  x = x_lookup_hash(X_CLUS, name, &hash, &head);
  if (x != NULL)
    return container_of(x, struct clus_node, c_x);

  if (!(flags & L_CREATE))
    return NULL;

  c = malloc(sizeof(*c) + strlen(name) + 1);
  if (c == NULL)
    goto err;

  memset(c, 0, sizeof(*c));

  size_t n = strlen(IDLE_JOBID) + 1 + strlen(name) + 1;
  idle_job_name = malloc(n);
  if (idle_job_name == NULL)
    goto err;

  snprintf(idle_job_name, n, "%s@%s", IDLE_JOBID, name);

  x_init(&c->c_x, X_CLUS, x_all[0], hash, head, name);

  if (cl_conn_init(&c->c_conn, &clus_conn_ops) < 0)
    goto err;

  c->c_idle_job = job_lookup(idle_job_name, &c->c_x, "NONE", "NONE", "0");
  if (c->c_idle_job == NULL)
    ERROR("cluster `%s': cannot create idle job: %m\n", name);
  else
    c->c_idle_job->j_fake = 1;

  /* TODO Auth. */

 err:
  free(idle_job_name);

  return c;
}

struct clus_node *clus_lookup_for_host(const char *name)
{
  while (1) {
    struct clus_node *c = clus_lookup(name, 0);

    if (c != NULL)
      return c;

    const char *s = strchr(name, '.');
    if (s == NULL)
      return clus_0;

    name = s + 1;
  }
}
