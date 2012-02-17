#include <ev.h>
#include <unistd.h>
#include <sys/wait.h>
#include <ctype.h>
#include "cl_listen.h"
#include "botz.h"
#include "string1.h"
#include "clus.h"
#include "job.h"
#include "sub.h"
#include "trace.h"

#define IDLE_JOBID "IDLE"
#define CLUS_0_NAME "UNKNOWN"
static struct clus_node *clus_0; /* Default/unknown cluster. */

static void clus_msg_cb(EV_P_ struct clus_node *c, char *msg)
{
  struct x_node *p, *x;
  struct job_node *j;
  struct sub_node *s, *t;

  char *host_name, *job_name, *owner = "NONE", *title = "NONE", *start = "0";

  TRACE("clus `%s', msg `%s'\n", c->c_x.x_name, msg);

  /* This must stay aligned with output of qhost_j_filer().
       HOST.DOMAIN JOBID@CLUSTER OWNER TITLE START_EPOCH
     Or if host is idle:
       HOST.DOMAIN IDLE@CLUSTER
  */

  if (split(&msg, &host_name, &job_name, &owner, &title, &start,
            (char **) NULL) < 2)
    return;

#if 0
  TRACE("host_name `%s', job_name `%s', owner `%s', title `%s', start `%s'\n",
        host_name, job_name, owner, title, start);
#endif

  j = job_lookup(job_name, &c->c_x, owner, title, start);
  if (j == NULL)
    return;

  x = x_lookup(X_HOST, host_name, &j->j_x, L_CREATE);
  if (x == NULL)
    return;

  p = x->x_parent;
  if (p == &j->j_x)
    return;

  /* Cancel subscriptions on x that are not allowed to access j1. */
  list_for_each_entry_safe(s, t, &x->x_sub_list, s_x_link[x_which(x)])
    if (!sub_may_access(s, &j->j_x))
      sub_cancel(EV_A_ s);

  x_set_parent(x, &j->j_x);

  TRACE("clus set `%s' parent `%s'\n", x->x_name, j->j_x.x_name);

  if (p != NULL && x_is_job(p) && p->x_nr_child == 0)
    job_end(EV_A_ container_of(p, struct job_node, j_x));
}

static void clus_put_cb(EV_P_ struct botz_x *bx, struct n_buf *nb)
{
  struct clus_node *c = bx->x_entry->e_data;
  char *msg;
  size_t msg_len;

  /* TODO AUTH. */

  TRACE("clus `%s' PUT length %zu, body `%.*s'\n",
        c->c_x.x_name, n_buf_length(nb),
        (int) (n_buf_length(nb) < 40 ? n_buf_length(nb) : 40), nb->nb_buf);

  while (n_buf_get_msg(nb, &msg, &msg_len) == 0)
    clus_msg_cb(EV_A_ c, msg);
}

static void clus_get_cb(EV_P_ struct botz_x *bx, struct n_buf *nb)
{
  struct clus_node *c = bx->x_entry->e_data;
  struct x_node *x;

  x_for_each_child(x, &c->c_x)
    n_buf_printf(nb, "%s %zu\n", x->x_name, x->x_nr_child);
}

static struct botz_entry_ops clus_entry_ops[BOTZ_NR_METHODS] = {
  [BOTZ_GET] = { .o_rsp_body_cb = &clus_get_cb },
  [BOTZ_PUT] = { .o_req_body_cb = &clus_put_cb },
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

  c->c_idle_job = job_lookup(idle_job_name, &c->c_x, "NONE", "NONE", "0");
  if (c->c_idle_job == NULL)
    ERROR("cluster `%s': cannot create idle job: %m\n", name);
  else
    c->c_idle_job->j_fake = 1;

  if (cl_listen_add("clus", name, clus_entry_ops, c) < 0) {
    ERROR("cannot add listen entry for cluster `%s': %m\n", name);
    goto err;
  }

 err:
  /* ... */

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

int clus_0_init(void)
{
  clus_0 = clus_lookup(CLUS_0_NAME, L_CREATE);
  if (clus_0 == NULL) {
    ERROR("cannot create cluster `%s': %m\n", CLUS_0_NAME);
    return -1;
  }

  return 0;
}
