#ifndef _CL_LISTEN_H_
#define _CL_LISTEN_H_
#include "botz.h"

extern struct botz_listen cl_listen;

int cl_listen_add(const char *dir, const char *name,
                  const struct botz_entry_ops *ops, void *data);

#endif
