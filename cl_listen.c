#include <malloc.h>
#include "string1.h"
#include "cl_listen.h"
#include "x_node.h"
#include "trace.h"

struct botz_listen cl_listen;

int cl_listen_add(const char *path, const struct botz_entry_ops *ops, void *data)
{
  if (botz_add(&cl_listen, path, ops, data) < 0) {
    ERROR("cannot add listen entry `%s': %m\n", path);
    return -1;
  }

  return 0;
}
