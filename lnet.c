#include "lnet.h"
#include "x_node.h"
#include "host.h"
#include "string1.h"
#include "trace.h"

static LIST_HEAD(lnet_list);

struct nid_entry {
  struct hlist_node e_hash_node;
  struct x_node *e_x;
  char e_nid[];
};

struct lnet_struct *lnet_lookup(const char *name, int flags, size_t hint)
{
  struct lnet_struct *l;

  list_for_each_entry(l, &lnet_list, l_link) {
    if (strcmp(name, l->l_name) == 0)
      return l;
  }

  if (!(flags & L_CREATE))
    return NULL;

  l = malloc(sizeof(*l) + strlen(name) + 1);
  if (l == NULL)
    return NULL;

  memset(l, 0, sizeof(*l));
  strcpy(l->l_name, name);

  if (hash_table_init(&l->l_hash_table, hint) < 0)
    goto err;

  list_add(&l->l_link, &lnet_list);

  if (0) {
  err:
    free(l);
    l = NULL;
  }

  return l;
}

struct x_node *
lnet_lookup_nid(struct lnet_struct *l, const char *nid, int flags)
{
  struct hlist_head *head;
  struct nid_entry *e;
  struct x_node *x;

  e = str_table_lookup_entry(&l->l_hash_table, nid, &head,
                             struct nid_entry, e_hash_node, e_nid);
  if (e != NULL)
    return e->e_x;

  if (!(flags & L_CREATE))
    return NULL;

  /* Create a new host using NID as its name. */
  x = x_host_lookup(nid, NULL, L_CREATE);
  if (x == NULL)
    return NULL;

  e = malloc(sizeof(*e) + strlen(nid) + 1);
  if (e == NULL)
    return NULL;

  hlist_add_head(&e->e_hash_node, head);
  e->e_x = x;
  strcpy(e->e_nid, nid);

  return x;
}

static int
lnet_set_nid(struct lnet_struct *l, const char *nid, struct x_node *x)
{
  struct hlist_head *head;
  struct nid_entry *e;

  e = str_table_lookup_entry(&l->l_hash_table, nid, &head,
                             struct nid_entry, e_hash_node, e_nid);
  if (e != NULL) {
    if (e->e_x != x) {
      ERROR("nid `%s' already assigned, old `%s', new `%s'\n",
            nid, e->e_x->x_name, x->x_name);
      e->e_x = x;
    }
    return 0;
  }

  e = malloc(sizeof(*e) + strlen(nid) + 1);
  if (e == NULL)
    return -1;

  hlist_add_head(&e->e_hash_node, head);
  e->e_x = x;
  strcpy(e->e_nid, nid);

  TRACE("lnet `%s', set nid `%s' `%s'\n", l->l_name, nid, x->x_name);

  return 0;
}

int lnet_read(struct lnet_struct *l, const char *path)
{
  int rc = -1;
  FILE *file = NULL;
  char *line = NULL;
  size_t line_size = 0;

  file = fopen(path, "r");
  if (file == NULL)
    goto out;

  while (getline(&line, &line_size, file) >= 0) {
    char *str, *nid, *name;
    struct x_node *x;

    str = chop(line, '#');

    if (split(&str, &nid, &name, NULL) != 2)
      continue;

    x = x_host_lookup(name, NULL, L_CREATE);
    if (x == NULL)
      continue;

    if (lnet_set_nid(l, nid, x) < 0) {
      rc = -1;
      break;
    }
  }

  rc = 0;

 out:
  free(line);
  if (file != NULL)
    fclose(file);

  return rc;
}
