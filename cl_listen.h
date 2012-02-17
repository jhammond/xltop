#ifndef _CL_LISTEN_H_
#define _CL_LISTEN_H_
#include "botz.h"

extern struct botz_listen cl_listen;
struct x_node;

int cl_listen_add(const char *path, const struct botz_entry_ops *ops, void *data);

#endif
