#ifndef _USER_H_
#define _USER_H_
#include <ev.h>
#include "list.h"
#include "cl_conn.h"

struct user_domain {
  void *d_auth;
  struct hlist_node d_hash_node;
  char d_name[];
};

int user_init(size_t nr_domains);
struct user_domain *user_domain_lookup(const char *user, int flags);

struct user_conn {
  char *c_name;
  struct cl_conn c_conn;
  struct list_head c_sub_list;
};

int user_conn_init(struct user_conn *uc, const char *name, const char *user);

void user_conn_stop(EV_P_ struct user_conn *uc);

void user_conn_destroy(struct user_conn *uc);

#endif
