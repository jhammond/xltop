#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ev.h>
#include "trace.h"
#include "cl_conn.h"
#include "string1.h"
#include "container_of.h"
#include "fd.h"

static int msg_cb(EV_P_ struct cl_conn *cc, char *msg, size_t msg_len)
{
  TRACE("cl_conn `%s' MSG `%s', len %zu\n", cl_conn_name(cc), msg, msg_len);

  return 0;
}

static int ctl_1_cb(EV_P_ struct cl_conn *cc, char *msg, size_t msg_len)
{
  TRACE("cl_conn `%s' CTL_1 `%s', len %zu\n", cl_conn_name(cc), msg, msg_len);

  return 0;
}

static int ctl_2_cb(EV_P_ struct cl_conn *cc, char *msg, size_t msg_len)
{
  TRACE("cl_conn `%s' CTL_2 `%s', len %zu\n", cl_conn_name(cc), msg, msg_len);

  return 0;
}

static int ctl_3_cb(EV_P_ struct cl_conn *cc, char *msg, size_t msg_len)
{
  TRACE("cl_conn `%s' CTL_3 `%s', len %zu\n", cl_conn_name(cc), msg, msg_len);

  return EPERM;
}

static int ctl_echo_cb(EV_P_ struct cl_conn *cc, char *msg, size_t msg_len)
{
  TRACE("cl_conn `%s' CTL_3 `%s', len %zu\n", cl_conn_name(cc), msg, msg_len);

  cl_conn_writef(EV_A_ cc, "%cECHO %s\n", CL_CONN_CTL_CHAR, msg);

  return 0;
}

static void end_cb(EV_P_ struct cl_conn *cc, int err)
{
  TRACE("cl_conn `%s' CLOSE %d `%s'\n", cl_conn_name(cc), err, strerror(err));
  cl_conn_destroy(cc);
  free(cc);
}

static struct cl_conn_ctl ctl_table[] = {
  { .cc_ctl_cb = &ctl_1_cb, .cc_ctl_name = "CTL_1" },
  { .cc_ctl_cb = &ctl_2_cb, .cc_ctl_name = "CTL_2" },
  { .cc_ctl_cb = &ctl_3_cb, .cc_ctl_name = "CTL_3" },
  { .cc_ctl_cb = &ctl_echo_cb, .cc_ctl_name = "ECHO" },
};

static struct cl_conn_ops ops = {
  .cc_msg_cb = &msg_cb,
  .cc_ctl = ctl_table,
  .cc_nr_ctl = sizeof(ctl_table) / sizeof(ctl_table[0]),
  .cc_end_cb = &end_cb,
  .cc_timeout = 10,
  .cc_rd_buf_size = 4096,
  .cc_wr_buf_size = 4096,
};

static void listen_cb(EV_P_ ev_io *w, int revents)
{
  struct cl_conn *cc = NULL;
  int fd = -1, gni_rc;
  struct sockaddr_storage addr;
  socklen_t addr_len = sizeof(addr);
  char cc_host[NI_MAXHOST], cc_serv[NI_MAXSERV];
  char cc_name[sizeof(cc_host) + sizeof(cc_serv) + 4];

  /* TODO Handle EV_ERROR in revents. */

  fd = accept(w->fd, (struct sockaddr *) &addr, &addr_len);
  if (fd < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK)
      return;
    ERROR("cannot accept connections: %m\n");
    /* ... */
    goto err;
  }
  fd_set_nonblock(fd);
  fd_set_cloexec(fd);

  cc = malloc(sizeof(*cc));
  if (cc == NULL)
    OOM();

  gni_rc = getnameinfo((struct sockaddr *) &addr, addr_len,
                       cc_host, sizeof(cc_host),
                       cc_serv, sizeof(cc_serv),
                       NI_NUMERICHOST|NI_NUMERICSERV);
  if (gni_rc != 0) {
    ERROR("cannot get name info for server connection: %s\n",
          gai_strerror(gni_rc));
    goto err;
  }
  TRACE("received connection from host `%s', port `%s'\n",
        cc_host, cc_serv);

  snprintf(cc_name, sizeof(cc_name), "[%s]:%s", cc_host, cc_serv);
  cl_conn_init(cc, &ops);
  cl_conn_set(cc, fd, EV_READ|EV_WRITE, cc_name);
  cl_conn_start(EV_A_ cc);

  if (0) {
  err:
    if (fd >= 0)
      close(fd);
    free(cc);
  }
}

int main(int argc, char *argv[])
{
  const char *bind_host = "localhost", *bind_port = "0";
  struct addrinfo *info, *list, hints = {
    .ai_family = AF_INET, /* Still needed. */
    .ai_socktype = SOCK_STREAM,
    .ai_flags = AI_PASSIVE, /* Ignored if bind_host != NULL. */
  };

  int gai_rc = getaddrinfo(bind_host, bind_port, &hints, &list);
  if (gai_rc != 0)
      FATAL("cannot resolve host `%s', service `%s': %s\n",
            bind_host, bind_port, gai_strerror(gai_rc));

  int lfd = -1;
  for (info = list; info != NULL; info = info->ai_next) {
    lfd = socket(info->ai_family, info->ai_socktype, info->ai_protocol);
    if (lfd < 0)
      continue;

    if (bind(lfd, info->ai_addr, info->ai_addrlen) == 0)
      break;

    close(lfd);
    lfd = -1;
  }
  freeaddrinfo(list);

  if (lfd < 0)
    FATAL("cannot bind to host `%s', service `%s': %m\n", bind_host, bind_port);

  signal(SIGPIPE, SIG_IGN);

  fd_set_nonblock(lfd);
  fd_set_cloexec(lfd);

  if (listen(lfd, 128) < 0) /* HARD */
    FATAL("cannot listen on `%s', service `%s': %m\n", bind_host, bind_port);

  struct ev_io listen_w;
  ev_io_init(&listen_w, &listen_cb, lfd, EV_READ);
  ev_io_start(EV_DEFAULT_ &listen_w);

  ev_run(EV_DEFAULT_ 0);

  if (lfd > 0)
    shutdown(lfd, SHUT_RDWR);

  return 0;
}
