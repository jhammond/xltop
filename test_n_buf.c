#include <stdio.h>
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "trace.h"
#include "n_buf.h"

int main(int argc, char *argv[])
{
  struct n_buf nb_storage, *nb = &nb_storage;
  const char *path;
  int fd, eof = 0, err = 0;

  if (argc < 2)
    FATAL("usage %s PATH\n", program_invocation_short_name);

  path = argv[1];
  fd = open(path, O_RDONLY|O_NONBLOCK);
  if (fd < 0)
    FATAL("cannot open `%s': %m\n", path);

  n_buf_init(nb, 4096);

  while (!eof && err == 0) {
    struct pollfd pfd = {
      .fd = fd,
      .events = POLLIN,
    };
    int nr_ready;

    errno = 0;
    nr_ready = poll(&pfd, 1, -1);
    TRACE("nr_ready %d, revents %d, errno %d (%m)\n",
          nr_ready, pfd.revents, errno);

    if (nr_ready < 0)
      FATAL("cannot poll: %m\n");

    TRACE("revents%s%s%s%s%s\n",
          pfd.revents & POLLIN ? " IN" : "",
          pfd.revents & POLLOUT ? " OUT" : "",
          pfd.revents & POLLERR ? " ERR" : "",
          pfd.revents & POLLHUP ? " HUP" : "",
          pfd.revents & POLLNVAL ? " NVAL" : "");

    n_buf_fill(nb, fd, &eof, &err);

    while (!n_buf_is_empty(nb)) {
      char *msg, *msg_end;

      msg = nb->nb_buf + nb->nb_start;
      msg_end = memchr(msg, '\n', nb->nb_end - nb->nb_start);
      if (msg_end == NULL)
        break;

      *msg_end = 0;
      nb->nb_start += msg_end - msg + 1;
      printf("msg `%s', msg_len %zd\n", msg, (size_t) (msg_end - msg));
    }
  }

  n_buf_destroy(nb);

  return 0;
}
