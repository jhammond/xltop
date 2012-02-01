#include <malloc.h>
#include "user.h"
#include "hash.h"

struct hash_table user_domain_table;

int user_init(size_t nr_domains)
{
  return hash_table_init(&user_domain_table, nr_domains);
}

struct user_domain *user_domain_lookup(const char *name, int flags)
{
  struct hlist_head *head;
  struct user_domain *ud;

  ud = str_table_lookup_entry(&user_domain_table, name, &head,
                              struct user_domain, d_hash_node, d_name);
  if (ud != NULL)
    return ud;

  if (!(flags & L_CREATE))
    return NULL;

  ud = malloc(sizeof(*ud) + strlen(name) + 1);
  if (ud == NULL)
    return NULL;

  memset(ud, 0, sizeof(*ud));
  hlist_add_head(&ud->d_hash_node, head);
  strcpy(ud->d_name, name);

  return ud;
}

struct cl_conn_ctl user_conn_ctl[] = {
};

struct cl_conn_ops user_conn_ops = {

};

int user_conn_init(struct user_conn *uc, const char *name, const char *user)
{
  return 0;
}

void user_conn_stop(EV_P_ struct user_conn *uc)
{
  /* subs? */
}

void user_conn_destroy(struct user_conn *uc)
{

}
