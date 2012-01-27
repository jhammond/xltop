#include <malloc.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include "n_buf.h"
#include "trace.h"

int n_buf_init(struct n_buf *nb, size_t size)
{
  memset(nb, 0, sizeof(*nb));

  nb->nb_buf = malloc(size);
  if (nb->nb_buf == NULL && size > 0)
    return -1;

  nb->nb_size = size;

  return 0;
}

void n_buf_fill(struct n_buf *nb, int fd, int *eof, int *err)
{
  ssize_t rc;

  n_buf_pullup(nb);
  if (nb->nb_end == nb->nb_size) {
    *err = ENOBUFS;
    return;
  }

  errno = 0;
  rc = read(fd, nb->nb_buf + nb->nb_end, nb->nb_size - nb->nb_end);

  if (errno != 0 && errno != EAGAIN && errno != EWOULDBLOCK)
    *err = errno;

  if (rc == 0)
    *eof = 1;

  if (rc > 0)
    nb->nb_end += rc;

  TRACE("fd %d, rc %zd, errno %d\n", fd, rc, errno);
}

int n_buf_move(struct n_buf *n0, struct n_buf *n1)
{
  if (n0->nb_size == n1->nb_size) {
    void *buf = n0->nb_buf;

    memcpy(n0, n1, sizeof(*n0));
    n1->nb_buf = buf;
    n1->nb_start = n1->nb_end = 0;
    return 0;
  }

  n_buf_pullup(n1);
  if (n1->nb_end < n0->nb_size) {
    memcpy(n0->nb_buf, n1->nb_buf, n1->nb_end);
    n0->nb_start = 0;
    n0->nb_end = n1->nb_end;
    n1->nb_end = 0;
    return 0;
  }

  return ENOBUFS;
}

void n_buf_destroy(struct n_buf *nb)
{
  free(nb->nb_buf);
  memset(nb, 0, sizeof(*nb));
}
