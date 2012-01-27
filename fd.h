#ifndef _FD_H_
#define _FD_H_
#include <fcntl.h>

static inline void fd_set_nonblock(int fd)
{
  long flags = fcntl(fd, F_GETFL);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static inline void fd_set_cloexec(int fd)
{
  long flags = fcntl(fd, F_GETFD);
  fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

#endif
