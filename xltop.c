#include "stddef1.h"
#include <stdio.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <malloc.h>
#include <signal.h>
#include <unistd.h>
#include <ctype.h>
#include <curl/curl.h>
#include <ev.h>
#include "xltop.h"
#include "x_node.h"
#include "hash.h"
#include "list.h"
#include "n_buf.h"
#include "string1.h"
#include "trace.h"

struct curl_x {
  CURL *cx_curl;
  char *cx_host;
  long cx_port;
};

struct query_pair {
  struct list_head qp_link;
  char *qp_field, *qp_value;
};

static char *r_host = "localhost"; /* XXX */
static long r_port = 9901;
struct curl_x curl_x;

static int want_sum;
static const char *top_sort_key;
static size_t top_limit;
static char *top_query = NULL;

static double timer_interval = 4;
static struct ev_timer timer_w;

int vsnprintf1(char **buf, size_t *size, size_t *len,
               const char *fmt, va_list args)
{
  while (1) {
    va_list args_1;
    char *buf_1;
    ssize_t c;
    size_t size_1, len_1;

    va_copy(args_1, args);
    c = vsnprintf(*buf + *len, *len < *size ? *size - *len : 0, fmt, args_1);
    va_end(args_1);

    if (c < 0)
      return -1;

    len_1 = *len + c;
    if (len_1 < *size) {
      *len = len_1;
      return 0;
    }

    size_1 = MAX(len_1 * 2, (size_t) 64);
    buf_1 = realloc(*buf, size_1);
    if (buf_1 == NULL)
      return -1;

    *buf = buf_1;
    *size = size_1;
  }
}

int snprintf1(char **buf, size_t *size, size_t *len, const char *fmt, ...)
{
  va_list args;
  int rc;

  va_start(args, fmt);
  rc = vsnprintf1(buf, size, len, fmt, args);
  va_end(args);

  return rc;
}

int curl_x_get_url(struct curl_x *cx, char *url, char **buf, size_t *len)
{
  FILE *file = NULL;
  int rc = -1;

  file = open_memstream(buf, len);
  if (file == NULL) {
    ERROR("cannot open memory stream: %m\n");
    goto out;
  }

  curl_easy_reset(cx->cx_curl);
  curl_easy_setopt(cx->cx_curl, CURLOPT_URL, url);
  curl_easy_setopt(cx->cx_curl, CURLOPT_PORT, cx->cx_port);
  curl_easy_setopt(cx->cx_curl, CURLOPT_UPLOAD, 0L);
  curl_easy_setopt(cx->cx_curl, CURLOPT_WRITEDATA, file);

#if DEBUG
  curl_easy_setopt(cx->cx_curl, CURLOPT_VERBOSE, 1L);
#endif

  int curl_rc = curl_easy_perform(cx->cx_curl);
  if (curl_rc != 0) {
    ERROR("cannot get `%s': %s\n", url, curl_easy_strerror(rc));
    /* Reset curl... */
    goto out;
  }

  if (ferror(file)) {
    ERROR("error writing to memory stream: %m\n");
    goto out;
  }

  rc = 0;

 out:
  if (file != NULL)
    fclose(file);

  TRACE("*buf `%.40s', *len %zu\n", *buf != NULL ? *buf : "NULL", *len);

  return rc;
}

int curl_x_get_query(struct curl_x *cx, const char *path, const char *qstr,
                     char **buf, size_t *len)
{
  char *url = NULL;
  size_t url_size = 0, url_len = 0;
  int rc = -1;

  if (snprintf1(&url, &url_size, &url_len, "http://%s/%s%s%s", cx->cx_host, path,
                qstr != NULL ? "?" : "", qstr != NULL ? qstr : "") < 0)
    goto out;

  TRACE("url `%s', url_size %zu, url_len %zu\n", url, url_size, url_len);

  if (curl_x_get_url(cx, url, buf, len) < 0)
    goto out;

  rc = 0;

 out:
  free(url);

  return rc;
}

int qstr_add1(char **buf, size_t *size, size_t *len, const char *s)
{
  for (; *s != 0; s++)
    if (isalnum(*s) || *s == '.' || *s == '-' || *s == '~' || *s == '_') {
      if (snprintf1(buf, size, len, "%c", *s) < 0)
        return -1;
    } else {
      if (snprintf1(buf, size, len, "%%%02hhX", (unsigned char) *s) < 0)
        return -1;
    }

  return 0;
}

int qstr_add(char **buf, size_t *size, size_t *len, const char *f, const char *v)
{
  int rc = -1;

  if (*buf != NULL && strlen(*buf) != 0) {
    if (snprintf1(buf, size, len, "&") < 0)
      goto out;
  }

  if (qstr_add1(buf, size, len, f) < 0)
    goto out;

  if (snprintf1(buf, size, len, "=") < 0)
    goto out;

  if (qstr_add1(buf, size, len, v) < 0)
    goto out;

  rc = 0;

 out:
  return rc;
}

int qstr_addz(char **buf, size_t *size, size_t *len, const char *f, size_t n)
{
  char v[3 * sizeof(n) + 1];

  snprintf(v, sizeof(v), "%zu", n);

  return qstr_add(buf, size, len, f, v);
}

static char *make_top_query(char **arg_list, size_t nr_args)
{
  char *q = NULL, *s[2] = { NULL };
  size_t q_size = 0, q_len = 0;

  char *x[2] = { NULL }, *t[2] = { NULL };
  size_t d[2] = { 0 };

  int set[NR_X_TYPES] = { 0 };
  char *val[NR_X_TYPES] = { NULL };

  size_t i;
  for (i = 0; i < nr_args; i++) {
    char *arg = arg_list[i];
    char *type_arg = strsep(&arg, "=:");
    int type;

    if (type_arg == NULL)
      continue;

    TRACE("type `%s', name `%s'\n", type_arg, arg);

    type = x_str_type(type_arg);
    if (type < 0) {
      ERROR("unrecognized type `%s'\n", type_arg);
      goto err;
    }

    set[type] = 1;
    if (arg != NULL)
      val[type] = arg;
  }

  if (val[X_HOST] != NULL) {
    t[0] = "host";
    x[0] = val[X_HOST];
    d[0] = 0;
  } else if (val[X_JOB] != NULL) {
    t[0] = "job";
    x[0] = val[X_JOB];
    d[0] = set[X_HOST] ? 1 : 0;
  } else if (val[X_CLUS] != NULL) {
    t[0] = "clus";
    x[0] = val[X_CLUS];
    d[0] = set[X_HOST] ? 2 : (set[X_JOB] ? 1 : 0);
  } else {
    t[0] = "all_0";
    x[0] = "ALL";
    d[0] = set[X_HOST] ? 3 : (set[X_JOB] ? 2 : (set[X_CLUS] ? 1 : 2)); /* Show jobs. */
  }

  if (val[X_SERV] != NULL) {
    t[1] = "serv";
    x[1] = val[X_SERV];
    d[1] = 0;
  } else if (val[X_FS] != NULL) {
    t[1] = "fs";
    x[1] = val[X_FS];
    d[1] = set[X_SERV] ? 1 : 0;
  } else {
    t[1] = "all_1";
    x[1] = "ALL";
    d[1] = set[X_SERV] ? 2 : 1; /* Show filesystems. */
  }

  for (i = 0; i < 2; i++) {
    size_t size = strlen(t[i]) + strlen(x[i]) + 2;

    s[i] = malloc(size);

    if (s[i] == NULL)
      goto err;
    snprintf(s[i], size, "%s:%s", t[i], x[i]);
  }

  if (qstr_add(&q, &q_size, &q_len, "x0", s[0]) < 0)
    goto err;
  if (qstr_addz(&q, &q_size, &q_len, "d0", d[0]) < 0)
    goto err;

  if (qstr_add(&q, &q_size, &q_len, "x1", s[1]) < 0)
    goto err;
  if (qstr_addz(&q, &q_size, &q_len, "d1", d[1]) < 0)
    goto err;

  if (qstr_addz(&q, &q_size, &q_len, "limit", top_limit) < 0)
    goto err;

  TRACE("q `%s'\n", q);

  if (0) {
  err:
    free(q);
    q = NULL;
  }

  free(s[0]);
  free(s[1]);

  return q;
}

static void top_timer_cb(EV_P_ ev_timer *w, int revents)
{
  static int nr_errs = 0;

  double now = ev_now(EV_A);
  char *buf = NULL;
  size_t len = 0;

  TRACE("begin, now %f\n", now);

  if (curl_x_get_query(&curl_x, "top", top_query, &buf, &len) < 0) {
    if (nr_errs++ > 3)
      FATAL("cannot get top\n"); /* XXX */
    goto out;
  }
  nr_errs = 0;

  TRACE("buf `%.40s', len %zd\n", buf, len);

  fwrite(buf, 1, len, stdout);
  fflush(stdout);

 out:
  free(buf);

  TRACE("end\n\n\n\n");
}

static void do_top(char **arg_list, size_t nr_args)
{
  top_query = make_top_query(arg_list, nr_args);

  if (top_query == NULL)
    FATAL("out of memory\n");

  signal(SIGPIPE, SIG_IGN);

  ev_timer_init(&timer_w, &top_timer_cb, 0.1, timer_interval);

  ev_timer_start(EV_DEFAULT_ &timer_w);

  ev_run(EV_DEFAULT_ 0);
}

static void do_status(char **args, size_t nr_args)
{
  /* TODO */
}

static void usage(int status)
{
  fprintf(status == 0 ? stdout : stderr,
          "Usage: %s [OPTIONS]...\n"
          /* ... */
          "\nOPTIONS:\n"
          " -c, --conf=FILE\n"
          /* ... */
          ,
          program_invocation_short_name);

  exit(status);
}

int main(int argc, char *argv[])
{
  char *o_host = NULL, *o_port = NULL, *conf_path = NULL;

  struct option opts[] = {
    { "conf",        1, NULL, 'c' },
    { "help",        0, NULL, 'h' },
    { "interval",    1, NULL, 'i' },
    { "sort-key",    1, NULL, 'k' },
    { "limit",       1, NULL, 'l' },
    { "remote-port", 1, NULL, 'p' },
    { "remote-host", 1, NULL, 'r' },
    { "sum",         1, NULL, 's' },
    { NULL,          0, NULL,  0  },
  };

  /* Show rate or show sum. */
  /* Sort spec depends on which. */
  /* Limit.  Scrolling. */

  int c;
  while ((c = getopt_long(argc, argv, "c:hi:k:l:p:r:s:", opts, 0)) > 0) {
    switch (c) {
    case 'c':
      conf_path = optarg;
      break;
    case 'h':
      usage(0);
      break;
    case 'i':
      timer_interval = strtod(optarg, NULL);
      if (timer_interval <= 0)
        FATAL("invalid interval `%s'\n", optarg);
      break;
    case 'k':
      top_sort_key = optarg;
      break;
    case 'l':
      top_limit = strtoul(optarg, NULL, 0);
      break;
    case 'p':
      o_port = optarg;
      break;
    case 'r':
      o_host = optarg;
      break;
    case 's':
      want_sum = 1;
      break;
    case '?':
      FATAL("Try `%s --help' for more information.\n", program_invocation_short_name);
    }
  }

  r_port = strtol(XLTOP_BIND_PORT, NULL, 0);

  if (conf_path != NULL)
    /* TODO */;

  if (o_host != NULL)
    r_host = o_host;

  if (o_port != NULL)
    r_port = strtol(o_port, NULL, 0);

  if (timer_interval <= 0)
    FATAL("invalid interval %f, must be positive\n",
          timer_interval);

  int curl_rc = curl_global_init(CURL_GLOBAL_NOTHING);
  if (curl_rc != 0)
    FATAL("cannot initialize curl: %s\n", curl_easy_strerror(curl_rc));

  curl_x.cx_curl = curl_easy_init();
  if (curl_x.cx_curl == NULL)
    FATAL("cannot initialize curl handle: %m\n");

  curl_x.cx_host = r_host;
  curl_x.cx_port = r_port;

  char *cmd = argv[optind], **args = argv + optind + 1;
  size_t nr_args = argc - optind - 1;

  if (strcmp(program_invocation_short_name, "xltop") == 0 || argc <= optind)
    do_top(argv + optind, argc - optind);
  else if (strcmp(cmd, "top") == 0)
    do_top(args, nr_args);
  else if (strcmp(cmd, "status") == 0)
    do_status(args, nr_args);
  else
    FATAL("unrecognized command `%s'\n", cmd);

  if (curl_x.cx_curl != NULL)
    curl_easy_cleanup(curl_x.cx_curl);

  curl_global_cleanup();

  return 0;
}
