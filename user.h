#ifndef _USER_H_
#define _USER_H_
#include <ev.h>
#include "list.h"
#include "cl_conn.h"

struct user_domain {
  void *ud_auth;
  struct list_head ud_conn_list;
  struct hlist_node ud_hash_node;
  char ud_name[];
};

int user_init(size_t nr_domains);
struct user_domain *user_domain_lookup(const char *user, int flags);

struct user_conn {
  struct cl_conn uc_conn;
  struct user_domain *uc_domain;
  struct list_head uc_domain_link;
  struct list_head uc_sub_list;
  char uc_name[];
};

int user_conn_init(struct user_conn *uc, struct user_domain *ud, const char *name);

void user_conn_destroy(EV_P_ struct user_conn *uc);

#endif
