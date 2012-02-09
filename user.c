#include <malloc.h>
#include "cl_types.h"
#include "cl_conn.h"
#include "string1.h"
#include "user.h"
#include "sub.h"
#include "trace.h"

static struct hash_table user_domain_table;

int user_init(size_t nr_domains)
{
  return hash_table_init(&user_domain_table, nr_domains);
}

struct user_domain *user_domain_lookup(const char *name, int flags)
{
  struct hlist_head *head;
  struct user_domain *ud;
  const char *at = strchr(name, '@');
  const char *domain = (at != NULL) ? at + 1 : name;

 retry:
  ud = str_table_lookup_entry(&user_domain_table, name, &head,
                              struct user_domain, ud_hash_node, ud_name);
  if (ud != NULL)
    return ud;

  if (!(flags & L_CREATE)) {
    const char *dot = strchr(domain, '.');
    if (dot == NULL)
      return NULL;
    domain = dot + 1;
    goto retry;
  }

  ud = malloc(sizeof(*ud) + strlen(name) + 1);
  if (ud == NULL)
    return NULL;

  memset(ud, 0, sizeof(*ud));
  INIT_LIST_HEAD(&ud->ud_conn_list);
  hlist_add_head(&ud->ud_hash_node, head);
  strcpy(ud->ud_name, name);

  return ud;
}

static void user_conn_end_cb(EV_P_ struct cl_conn *cc, int err)
{
  struct user_conn *uc = container_of(cc, struct user_conn, uc_conn);

  /* Don't timeout if there are active subscriptions.  Kind of a hack. */
  if (err == ETIMEDOUT && !list_empty(&uc->uc_sub_list)) {
    cl_conn_start(EV_A_ cc);
    return;
  }

  TRACE("user_conn `%s' END err %d\n", uc->uc_name, err);

  user_conn_destroy(EV_A_ uc);
  free(uc);
}

static int
user_ctl_echo_cb(EV_P_ struct cl_conn *cc, struct ctl_data *cd)
{
  struct user_conn *uc = container_of(cc, struct user_conn, uc_conn);

  TRACE("user_conn `%s', CTL `%s', tid %"PRIu64", args `%s'\n", uc->uc_name,
        cd->cd_name, cd->cd_tid, cd->cd_args);

  cl_conn_writef(EV_A_ cc, "%s\n", cd->cd_args);

  return 0;
}

static int
user_ctl_dump_cb(EV_P_ struct cl_conn *cc, struct ctl_data *cd)
{
  struct user_conn *uc = container_of(cc, struct user_conn, uc_conn);
  char *args = cd->cd_args, *type_name;
  size_t offset = 0, limit = ((size_t) -1) / 2;
  struct hash_table *ht = NULL;
  struct hlist_node *node;
  struct x_node *x;
  size_t i, j;

  TRACE("user_conn `%s', CTL `%s', tid %"PRIu64", args `%s'\n", uc->uc_name,
        cd->cd_name, cd->cd_tid, cd->cd_args);

  if (split(&args, &type_name, (char *) NULL) != 1)
    return CL_ERR_NR_ARGS;

  if (args != NULL)
    sscanf(args, "%zu %zu", &offset, &limit);

  for (i = 0; i < nr_x_types; i++) {
    if (strcmp(type_name, x_types[i].x_type_name) == 0) {
      ht = &x_types[i].x_hash_table;
      break;
    }
  }

  if (ht == NULL)
    return CL_ERR_NO_X;

  j = 0;
  for (i = 0; i < (1ULL << ht->ht_shift); i++) {
    hlist_for_each_entry(x, node, ht->ht_table + i, x_hash_node) {
      if (offset <= j++)
        cl_conn_writef(EV_A_ cc, "%s %s %zu\n",
                       x->x_name,
                       x->x_parent != NULL ? x->x_parent->x_name : "NONE",
                       x->x_nr_child);
      if (j == offset + limit)
        goto out;
    }
  }

 out:
  return 0;
}

void
user_sub_cb(EV_P_ struct sub_node *s, struct k_node *k,
            struct x_node *x0, struct x_node *x1, double *d)
{
  struct user_conn *uc = s->s_u_conn;

  cl_conn_writef(EV_A_ &uc->uc_conn, "%s %s %f %f %f %f\n",
                 k->k_x[0]->x_name, k->k_x[1]->x_name, ev_now(EV_A),
                 k->k_rate[0], k->k_rate[1], k->k_rate[2]);
}

static int
user_ctl_sub_cb(EV_P_ struct cl_conn *cc, struct ctl_data *cd)
{
  struct user_conn *uc = container_of(cc, struct user_conn, uc_conn);
  char *args = cd->cd_args, *name0, *name1;
  struct x_node *x0, *x1;
  struct k_node *k;
  struct sub_node *s;

  TRACE("user_conn `%s', CTL `%s', tid %"PRIu64", args `%s'\n", uc->uc_name,
        cd->cd_name, cd->cd_tid, cd->cd_args);

  if (split(&args, &name0, &name1, (char *) NULL) != 2)
    return CL_ERR_NR_ARGS;

  x0 = x_lookup_str(name0);
  if (x0 == NULL)
    return CL_ERR_NO_X;

  x1 = x_lookup_str(name1);
  if (x1 == NULL)
    return CL_ERR_NO_X;

  /* TODO auth. */

  k = k_lookup(x0, x1, L_CREATE);
  if (k == NULL)
    return (errno == EINVAL) ? CL_ERR_WHICH : CL_ERR_NO_MEM; /* XXX */

  s = malloc(sizeof(*s));
  if (s == NULL)
    return CL_ERR_NO_MEM;

  sub_init(s, k, uc, cd->cd_tid, &user_sub_cb);

  return 0;
}

int k_top_cmp(struct k_heap *h, struct k_node *k0, struct k_node *k1)
{
  int i = 0;

  return k0->k_rate[i] < k1->k_rate[i] ? -1 :
    k0->k_rate[i] == k1->k_rate[i] ? 0 : 1;
}

static int user_ctl_top_cb(EV_P_ struct cl_conn *cc, struct ctl_data *cd)
{
  struct user_conn *uc = container_of(cc, struct user_conn, uc_conn);
  char *args = cd->cd_args, *name0, *name1, *sd0, *sd1, *slimit;
  size_t d0, d1, limit;
  struct x_node *x0, *x1;
  struct k_heap h;

  /* x0 x1 d0 d1 limit */

  TRACE("user_conn `%s', CTL `%s', tid %"PRIu64", args `%s'\n", uc->uc_name,
        cd->cd_name, cd->cd_tid, cd->cd_args);

  if (split(&args, &name0, &name1, &sd0, &sd1, &slimit, (char **) NULL) != 5)
    return CL_ERR_NR_ARGS;

  x0 = x_lookup_str(name0);
  if (x0 == NULL)
    return CL_ERR_NO_X;

  x1 = x_lookup_str(name1);
  if (x1 == NULL)
    return CL_ERR_NO_X;

  /* TODO auth. */

  d0 = strtoul(sd0, NULL, 0);
  d1 = strtoul(sd1, NULL, 0);
  limit = strtoul(slimit, NULL, 0);

  TRACE("x0 `%s', x1 `%s', d0 %zu, d1 %zu, limit %zu\n",
        x0->x_name, x1->x_name, d0, d1, limit);

  /* TODO Check d0, d1, and limit. */

  if (k_heap_init(&h, limit) < 0)
    return CL_ERR_NO_MEM;

  k_heap_top(&h, x0, d0, x1, d1, &k_top_cmp);
  k_heap_order(&h, &k_top_cmp);

  size_t i;
  for (i = 0; i < h.h_count; i++) {
    struct k_node *k = h.h_k[i];
    cl_conn_writef(EV_A_ cc, "%s %s %f %f\n",
                   k->k_x[0]->x_name, k->k_x[1]->x_name, k->k_t,
                   k->k_rate[0]);
  }

  k_heap_destroy(&h);

  return 0;
}

static struct cl_conn_ctl user_conn_ctl[] = {
  { .cc_ctl_cb = &user_ctl_echo_cb, .cc_ctl_name = "echo" },
  { .cc_ctl_cb = &user_ctl_dump_cb, .cc_ctl_name = "dump" },
  { .cc_ctl_cb = &user_ctl_sub_cb,  .cc_ctl_name = "sub" },
  { .cc_ctl_cb = &user_ctl_top_cb,  .cc_ctl_name = "top" },
};

static struct cl_conn_ops user_conn_ops = {
  .cc_ctl = user_conn_ctl,
  .cc_nr_ctl = sizeof(user_conn_ctl) / sizeof(user_conn_ctl[0]),
  .cc_end_cb = &user_conn_end_cb,
  .cc_timeout = 60, /* XXX */
  .cc_rd_buf_size = 4096, /* XXX */
  .cc_wr_buf_size = 65536, /* XXX */
};

int user_conn_init(struct user_conn *uc, struct user_domain *ud, const char *name)
{
  memset(uc, 0, sizeof(*uc));
  INIT_LIST_HEAD(&uc->uc_domain_link);
  INIT_LIST_HEAD(&uc->uc_sub_list);
  strcpy(uc->uc_name, name);

  if (cl_conn_init(&uc->uc_conn, &user_conn_ops) < 0)
    return -1;

  uc->uc_domain = ud;
  list_add(&uc->uc_domain_link, &ud->ud_conn_list);

  return 0;
}

void user_conn_destroy(EV_P_ struct user_conn *uc)
{
  struct sub_node *s, *t;

  list_for_each_entry_safe(s, t, &uc->uc_sub_list, s_u_link)
    sub_cancel(EV_A_ s);

  cl_conn_close(EV_A_ &uc->uc_conn);
  cl_conn_destroy(&uc->uc_conn);
  list_del_init(&uc->uc_domain_link);
}
