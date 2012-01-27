#ifndef _N_BUF_H_
#define _N_BUF_H_
#include <stddef.h>
#include <string.h>

struct n_buf {
  char *nb_buf;
  size_t nb_size, nb_start, nb_end;
};

int n_buf_init(struct n_buf *nb, size_t size);

void n_buf_fill(struct n_buf *nb, int fd, int *eof, int *err);

int n_buf_move(struct n_buf *dest, struct n_buf *src);

static inline void n_buf_clear(struct n_buf *nb)
{
  nb->nb_start = nb->nb_end = 0;
}

static inline int n_buf_is_empty(const struct n_buf *nb)
{
  return nb->nb_start == nb->nb_end;
}

static inline void n_buf_pullup(struct n_buf *nb)
{
  if (nb->nb_start > 0) {
    memmove(nb->nb_buf, nb->nb_buf + nb->nb_start, nb->nb_end - nb->nb_start);
    nb->nb_end -= nb->nb_start;
    nb->nb_start = 0;
  }
}

void n_buf_destroy(struct n_buf *nb);

#endif
