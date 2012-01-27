#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include "string1.h"
#include "cl_conn.h"
#include "container_of.h"
#include "trace.h"

static void cl_conn_timer_cb(EV_P_ ev_timer *w, int revents);
static void cl_conn_io_cb(EV_P_ ev_io *w, int revents);

int cl_conn_init(struct cl_conn *cc, const struct cl_conn_ops *ops)
{
  memset(cc, 0, sizeof(*cc));
  ev_init(&cc->cc_timer_w, &cl_conn_timer_cb);
  cc->cc_timer_w.repeat = ops->cc_timeout;

  ev_init(&cc->cc_io_w, &cl_conn_io_cb);
  if (n_buf_init(&cc->cc_rd_buf, ops->cc_rd_buf_size) < 0)
    ;
  if (n_buf_init(&cc->cc_wr_buf, ops->cc_wr_buf_size) < 0)
    ;
  cc->cc_ops = ops;

  return 0;
}

int cl_conn_set(struct cl_conn *cc, int fd, int events, const char *name)
{
  TRACE("cl_conn `%s' SET fd %d, name `%s'\n", cl_conn_name(cc), fd, name);

  free(cc->cc_name);
  cc->cc_name = strdup(name);
  if (cc->cc_name == NULL)
    return -1;

  ev_io_set(&cc->cc_io_w, fd, events);

  return 0;
}

void cl_conn_start(EV_P_ struct cl_conn *cc)
{
  TRACE("cl_conn `%s' START\n", cl_conn_name(cc));

  ev_timer_again(EV_A_ &cc->cc_timer_w);
  ev_io_start(EV_A_ &cc->cc_io_w);
}

void cl_conn_stop(EV_P_ struct cl_conn *cc)
{
  TRACE("cl_conn `%s' STOP\n", cl_conn_name(cc));

  ev_timer_stop(EV_A_ &cc->cc_timer_w);
  ev_io_stop(EV_A_ &cc->cc_io_w);
}

void cl_conn_destroy(struct cl_conn *cc)
{
  if (ev_is_active(&cc->cc_timer_w) || ev_is_active(&cc->cc_io_w))
    FATAL("destroying cl_conn `%s' with active watchers\n", cl_conn_name(cc));

  TRACE("destroying cl_conn `%s'\n", cl_conn_name(cc));

  if (cc->cc_io_w.fd >= 0)
    close(cc->cc_io_w.fd);
  cc->cc_io_w.fd = -1;

  n_buf_destroy(&cc->cc_rd_buf);
  n_buf_destroy(&cc->cc_wr_buf);

  free(cc->cc_name);
  cc->cc_name = NULL;
}

static int cl_conn_ctl_cmp(const char *name, const struct cl_conn_ctl *ctl)
{
  return strcmp(name, ctl->cc_ctl_name);
}

static int cl_conn_rd(EV_P_ struct cl_conn *cc)
{
  struct n_buf *nb = &cc->cc_rd_buf;
  const struct cl_conn_ops *ops = cc->cc_ops;
  int eof = 0, err = 0;

  n_buf_fill(nb, cc->cc_io_w.fd, &eof, &err);

  if (eof)
    cc->cc_read_eof = 1;

  char *msg;
  size_t msg_len;

  while (err == 0 && n_buf_get_msg(nb, &msg, &msg_len) == 0) {
    if (*msg == CL_CONN_CTL_CHAR) {
      char *ctl_name;
      int (**ctl_cb)(EV_P_ struct cl_conn *, char *, size_t);

      msg++;

      ctl_name = wsep(&msg);
      if (ctl_name == NULL)
        continue;

      ctl_cb = bsearch(ctl_name, ops->cc_ctl, ops->cc_nr_ctl,
                       sizeof(ops->cc_ctl[0]),
                       (int (*)(const void *, const void *)) &cl_conn_ctl_cmp);

      if (ctl_cb == NULL) {
        TRACE("cl_conn `%s', no call back for ctl_name `%s'\n",
              cl_conn_name(cc), ctl_name);
        err = ENOTTY; /* CL_CONN_ERR_BAD_CTL... */
        continue;
      }

      if (msg == NULL)
        msg = ctl_name + strlen(ctl_name);

      err = (**ctl_cb)(EV_A_ cc, msg, strlen(msg));
    } else if (ops->cc_msg_cb != NULL) {
      err = (*ops->cc_msg_cb)(EV_A_ cc, msg, msg_len);
    } else {
      err = EINVAL; /* CL_CONN_BAD_MSG */
    }
  }

  return err;
}

static int cl_conn_wr(EV_P_ struct cl_conn *cc)
{
  struct n_buf *nb = &cc->cc_wr_buf;
  int err = 0;
  ssize_t rc;

  TRACE("cl_conn `%s', nb_start %zu, nb_end %zu, nb_size %zu\n",
        cl_conn_name(cc), nb->nb_start, nb->nb_end, nb->nb_size);

  if (n_buf_is_empty(nb))
    return 0;

  errno = 0;
  rc = write(cc->cc_io_w.fd, nb->nb_buf + nb->nb_start,
             nb->nb_end - nb->nb_start);

  TRACE("cl_conn `%s', fd %d, rc %zd, errno %d\n",
        cl_conn_name(cc), cc->cc_io_w.fd, rc, errno);

  err = (errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : errno;

  if (rc > 0)
    nb->nb_start += rc;

  return err;
}

static void cl_conn_end(EV_P_ struct cl_conn *cc, int err)
{
  TRACE("cl_conn `%s' END err %d\n", cl_conn_name(cc), err);

  cl_conn_stop(EV_A_ cc);
  if (cc->cc_ops->cc_end_cb != NULL) {
    (*cc->cc_ops->cc_end_cb)(EV_A_ cc, err);
  } else {
    cl_conn_destroy(cc);
    free(cc);
  }
}

static void cl_conn_timer_cb(EV_P_ ev_timer *w, int revents)
{
  struct cl_conn *cc = container_of(w, struct cl_conn, cc_timer_w);

  TRACE("cl_conn `%s' TIMER revents %d\n", cl_conn_name(cc), revents);

  cl_conn_end(EV_A_ cc, ETIMEDOUT);
}

static void cl_conn_up(EV_P_ struct cl_conn *cc, int err)
{
  int events;

  if (err != 0) {
    if (err == CL_CONN_END)
      err = 0;
    cl_conn_end(EV_A_ cc, err);
    return;
  }

  events = 0;

  if (!cc->cc_read_eof)
    events |= EV_READ;

  if (!n_buf_is_empty(&cc->cc_wr_buf))
    events |= EV_WRITE;

  TRACE("cl_conn `%s', old events %d, new events %d\n",
        cl_conn_name(cc), cc->cc_io_w.events, events);

  if (events == 0) {
    cl_conn_end(EV_A_ cc, 0);
    return;
  }

  ev_timer_again(EV_A_ &cc->cc_timer_w);
  if (events != cc->cc_io_w.events) {
    ev_io_stop(EV_A_ &cc->cc_io_w);
    ev_io_set(&cc->cc_io_w, cc->cc_io_w.fd, events);
    ev_io_start(EV_A_ &cc->cc_io_w);
  }
}

static void cl_conn_io_cb(EV_P_ ev_io *w, int revents)
{
  struct cl_conn *cc = container_of(w, struct cl_conn, cc_io_w);
  int err = 0;

  TRACE("cl_conn `%s', IO revents %d\n", cl_conn_name(cc), revents);

  if (revents & EV_ERROR)
    FATAL("cl_conn `%s', EV_ERROR: %m\n", cl_conn_name(cc));

  if (revents & EV_READ) {
    err = cl_conn_rd(EV_A_ cc);
    if (err != 0)
      goto out;
  }

  if (revents & EV_WRITE) {
    err = cl_conn_wr(EV_A_ cc);
    if (err != 0)
      goto out;
  }

 out:
  cl_conn_up(EV_A_ cc, err);
}

int cl_conn_writef(EV_P_ struct cl_conn *cc, const char *fmt, ...)
{
  struct n_buf *nb = &cc->cc_wr_buf;
  int len, err = 0;
  va_list args;
  va_start(args, fmt);

  n_buf_pullup(nb);

  errno = 0;
  len = vsnprintf(nb->nb_buf + nb->nb_end, nb->nb_size - nb->nb_end, fmt, args);
  if (len < 0)
    err = errno != 0 ? errno : EINVAL;
  else if (nb->nb_size - nb->nb_end < len)
    err = ENOBUFS;
  else
    nb->nb_end += len;

  va_end(args);

  cl_conn_up(EV_A_ cc, err);

  return err;
}
