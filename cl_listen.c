#include <malloc.h>
#include "string1.h"
#include "cl_listen.h"
#include "x_node.h"
#include "trace.h"

struct botz_listen cl_listen;

int cl_listen_add(const char *dir, const char *name,
                  const struct botz_entry_ops *ops, void *data)
{
  char *path = NULL;
  size_t path_size = 1 + strlen(dir) + 1 + strlen(name) + 1;
  int rc = -1;

  path = malloc(path_size);
  if (path == NULL)
    goto err;

  snprintf(path, path_size, "/%s/%s", dir, name);

  if (botz_add(&cl_listen, path, ops, data) < 0) {
    ERROR("cannot add listen entry `%s': %m\n", path);
    goto err;
  }

  rc = 0;

 err:
  free(path);

  return rc;
}

int cl_listen_add_x(struct x_node *x, const struct botz_entry_ops *ops, void *data)
{
  return cl_listen_add(x->x_type->x_type_name, x->x_name, ops, data);
}
