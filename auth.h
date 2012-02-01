#ifndef _AUTH_H_
#define _AUTH_H_
#include <string.h>

static inline int
auth_ctl_is_allowed(void *auth, double now, const char *ctl,
                    const char *name, const char *user, const char *stime,
                    const char *sig)
{
  return strcmp(sig, "11") == 0;
}

#endif
