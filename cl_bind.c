#include <malloc.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ev.h>
#include "cl_bind.h"
#include "cl_conn.h"
#include "string1.h"
#include "fd.h"
#include "trace.h"
#include "list.h"
#include "container_of.h"

LIST_HEAD(cl_bind_list);

static void cl_bind_io_cb(EV_P_ ev_io *w, int revents);

void cl_bind_init(struct cl_bind *cb, const struct cl_conn_ops *ops)
{
  memset(cb, 0, sizeof(*cb));
  cb->cb_ni_host = strdup("NONE");
  cb->cb_ni_port = strdup("NONE");
  cb->cb_conn_ops = ops;
  INIT_LIST_HEAD(&cb->cb_link);
  ev_init(&cb->cb_io_w, &cl_bind_io_cb);
  cb->cb_io_w.fd = -1;
}

void cl_bind_destroy(struct cl_bind *cb)
{
  free(cb->cb_ni_host);
  cb->cb_ni_host = NULL;

  free(cb->cb_ni_port);
  cb->cb_ni_port = NULL;

  list_del_init(&cb->cb_link);

  if (cb->cb_io_w.fd >= 0)
    close(cb->cb_io_w.fd);
  cb->cb_io_w.fd = -1;
}

static int cl_bind_exists(const struct sockaddr *addr, socklen_t addrlen)
{
  struct cl_bind *cb;

  list_for_each_entry(cb, &cl_bind_list, cb_link) {
    if (addrlen == cb->cb_addrlen &&
        memcmp(addr, cb->cb_addr, cb->cb_addrlen) == 0)
      return 1;
  }

  return 0;
}

typedef struct sockaddr SA;
typedef struct sockaddr_in SIN;
typedef struct sockaddr_in6 SIN6;

int cl_bind_set(struct cl_bind *cb, const char *host, const char *port)
{
  int fd = -1, rc = -1, in_port = -1;
  int backlog = 128; /* HARD */
  struct sockaddr_storage addr;
  socklen_t addrlen = sizeof(addr);

  struct addrinfo ai_hints = {
    .ai_family = AF_INET, /* Still needed? */
    .ai_socktype = SOCK_STREAM,
    .ai_flags = AI_PASSIVE, /* Ignored if bind_host != NULL. */
  };
  struct addrinfo *ai, *ai_list = NULL;

  if (host == NULL)
    host = "0.0.0.0";

  if (port == NULL)
    port = "0";

  TRACE("adding bind `%s', `%s'\n", host, port);

  if (ev_is_active(&cb->cb_io_w))
    FATAL("cl_bind `%s' `%s' already active\n", cb->cb_ni_host, cb->cb_ni_port);

  int gai_rc = getaddrinfo(host, port, &ai_hints, &ai_list);
  if (gai_rc != 0) {
    ERROR("cannot resolve host `%s', port/service `%s': %s\n",
          host, port, gai_strerror(gai_rc));
    goto err;
  }

  for (ai = ai_list; ai != NULL; ai = ai->ai_next) {
    if (cl_bind_exists(ai->ai_addr, ai->ai_addrlen)) {
      TRACE("already bound to host `%s', port/service `%s'\n",
            host, port);
      errno = EEXIST;
      goto err;
    }

    fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (fd < 0)
      continue;

    if (bind(fd, ai->ai_addr, ai->ai_addrlen) == 0)
      break;

    close(fd);
    fd = -1;
  }

  if (fd < 0) {
    ERROR("cannot bind to host `%s', port/service `%s': %m\n", host, port);
    goto err;
  }

  fd_set_nonblock(fd);
  fd_set_cloexec(fd);

  if (listen(fd, backlog) < 0) {
    ERROR("cannot listen on `%s', port/service `%s': %m\n", host, port);
    goto err;
  }

  if (getsockname(fd, (SA *) &addr, &addrlen) < 0) {
    ERROR("cannot get socket address: %m\n");
    goto err;
  }

  struct sockaddr *sa = (SA *) &addr;
  if (sa->sa_family != AF_INET && sa->sa_family != AF_INET6) {
    ERROR("invalid family %d\n", (int) sa->sa_family);
    goto err;
  }

  struct sockaddr_in *sin = (SIN *) &addr;
  ASSERT(offsetof(SIN, sin_port) == offsetof(SIN6, sin6_port));
  in_port = ntohs(sin->sin_port);

  /* TODO Get name info/address in case host is wildcard. */
  free(cb->cb_ni_host);
  cb->cb_ni_host = strdup(host);
  if (cb->cb_ni_host == NULL)
    goto err;

  char in_port_buf[32];
  snprintf(in_port_buf, sizeof(in_port_buf), "%d", in_port);

  free(cb->cb_ni_port);
  cb->cb_ni_port = strdup(in_port_buf);
  if (cb->cb_ni_port == NULL)
    goto err;

  free(cb->cb_addr);
  cb->cb_addr = malloc(addrlen);
  if (cb->cb_addr == NULL)
    goto err;
  memcpy(cb->cb_addr, &addr, addrlen);
  cb->cb_addrlen = addrlen;

  list_add(&cb->cb_link, &cl_bind_list);
  ev_io_init(&cb->cb_io_w, &cl_bind_io_cb, fd, EV_READ);

  TRACE("bound to [%s]:%s\n", cb->cb_ni_host, cb->cb_ni_port);

  rc = 0;

  if (0) {
  err:
    free(cb->cb_ni_host);
    free(cb->cb_ni_port);
    free(cb->cb_addr);
    cb->cb_ni_host = NULL;
    cb->cb_ni_port = NULL;
    cb->cb_addr = NULL;
    cb->cb_addrlen = 0;
    if (fd >= 0)
      close(fd);
  }
  freeaddrinfo(ai_list);

  return rc;
}

void cl_bind_start(EV_P_ struct cl_bind *cb)
{
  ev_io_start(EV_A_ &cb->cb_io_w);
}

static void cl_bind_io_cb(EV_P_ ev_io *w, int revents)
{
  struct cl_bind *cb = container_of(w, struct cl_bind, cb_io_w);
  struct cl_conn *cc = NULL;
  int fd = -1, gni_rc;
  struct sockaddr_storage addr;
  socklen_t addrlen = sizeof(addr);
  char ni_addr[NI_MAXHOST], ni_port[NI_MAXSERV];
  char ni_name[sizeof(ni_addr) + sizeof(ni_port) + 4]; /* [addr]:port */

  fd = accept(w->fd, (struct sockaddr *) &addr, &addrlen);
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
    goto err;

  gni_rc = getnameinfo((struct sockaddr *) &addr, addrlen,
                       ni_addr, sizeof(ni_addr),
                       ni_port, sizeof(ni_port),
                       NI_NUMERICHOST|NI_NUMERICSERV);
  if (gni_rc != 0) {
    ERROR("cannot get name info for server connection: %s\n",
          gai_strerror(gni_rc));
    goto err;
  }

  snprintf(ni_name, sizeof(ni_name), "[%s]:%s", ni_addr, ni_port);

  TRACE("accepted connection from host `%s', port `%s', name `%s'\n",
        ni_addr, ni_port, ni_name);

  cl_conn_init(cc, cb->cb_conn_ops);

  if (cl_conn_set(cc, fd, EV_READ, ni_name) < 0)
    goto err;

  cl_conn_start(EV_A_ cc);

  if (0) {
  err:
    if (fd >= 0)
      close(fd);
    free(cc);
  }
}
