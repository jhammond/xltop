#include <malloc.h>
#include "user.h"
#include "hash.h"
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

  TRACE("user_conn `%s' END err %d\n", uc->uc_name, err);

  user_conn_destroy(uc);
  free(uc);
}

static int
user_conn_echo_cb(EV_P_ struct cl_conn *cc, char *ctl, char *args, size_t args_len)
{
  struct user_conn *uc = container_of(cc, struct user_conn, uc_conn);

  TRACE("user_conn `%s', CTL `%s', args `%s'\n", uc->uc_name, ctl, args);

  cl_conn_writef(EV_A_ cc, "%s\n", args);

  return 0;
}

static int
user_conn_sub_cb(EV_P_ struct cl_conn *cc, char *ctl, char *args, size_t args_len)
{
  struct user_conn *uc = container_of(cc, struct user_conn, uc_conn);

  TRACE("user_conn `%s', CTL `%s', args `%s'\n", uc->uc_name, ctl, args);

  return 0;
}

static struct cl_conn_ctl user_conn_ctl[] = {
  { .cc_ctl_cb = &user_conn_echo_cb, .cc_ctl_name = "echo" },
  { .cc_ctl_cb = &user_conn_sub_cb,  .cc_ctl_name = "sub" }
};

static struct cl_conn_ops user_conn_ops = {
  .cc_ctl = user_conn_ctl,
  .cc_nr_ctl = sizeof(user_conn_ctl) / sizeof(user_conn_ctl[0]),
  .cc_end_cb = &user_conn_end_cb,
  .cc_timeout = 60,
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

void user_conn_stop(EV_P_ struct user_conn *uc)
{
  cl_conn_stop(EV_A_ &uc->uc_conn);
  /* TODO subs. */
}

void user_conn_destroy(struct user_conn *uc)
{
  cl_conn_destroy(&uc->uc_conn);
  list_del(&uc->uc_domain_link);
}
