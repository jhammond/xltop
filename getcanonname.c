#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "getcanonname.h"

int getcanonname(const char *host, char *c_name, size_t c_name_size)
{
  char h_name[NI_MAXHOST + 1];
  struct addrinfo ai_hints = {
    .ai_flags = AI_CANONNAME,
    .ai_family = AF_UNSPEC,
    .ai_socktype = 0,
  };
  struct addrinfo *ai_list = NULL;
  int rc = -1, saved_errno = errno;

  errno = 0;

  if (host == NULL) {
    memset(h_name, 0, sizeof(h_name));
    if (gethostname(h_name, sizeof(h_name) - 1) < 0)
      goto err;
    host = h_name;
  }

  int ai_rc = getaddrinfo(host, NULL, &ai_hints, &ai_list);
  if (ai_rc != 0)
    goto err;

  if (ai_list == NULL || ai_list->ai_canonname == NULL)
    goto err;

  int len = snprintf(c_name, c_name_size, "%s", ai_list->ai_canonname);
  if (!(len < c_name_size)) {
    errno = ENAMETOOLONG;
    goto err;
  }

  rc = 0;
  errno = saved_errno;

 err:
  freeaddrinfo(ai_list);

  if (rc < 0 && errno == 0)
    errno = EINVAL;

  return rc;
}
