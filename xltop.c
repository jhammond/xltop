#include "stddef1.h"
#include <stdio.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <malloc.h>
#include <math.h>
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

struct xl_k {
};

struct xl_host {
  struct hlist_node h_hash_node;
  struct xl_job *h_job;
  char h_name[];
};

struct xl_job {
  struct hlist_node j_hash_node;
  struct list_head j_clus_link;
  char *j_owner, *j_title;
  double j_start;
  size_t j_nr_hosts;
  char j_name[];
};

struct xl_clus {
  struct hlist_node c_hash_node;
  struct list_head c_job_list;
  struct ev_periodic c_w;
  char c_name[];
};

struct xl_fs {
  struct hlist_node f_hash_node;
  struct ev_periodic f_w;
  double f_mds_load[3], f_oss_load[3];
  size_t f_nr_tgts, f_nr_nids;
  char f_name[];
};

static char *r_host = "localhost"; /* XXX */
static long r_port = 9901;
struct curl_x curl_x;

static int top_show_sum;
static const char *top_sort_key; /* TODO */
static size_t top_limit;
static char *top_query = NULL;
static double top_interval = 4;
static struct ev_timer top_timer_w;

static struct hash_table xl_hash_table[NR_X_TYPES];

int curl_x_get_url(struct curl_x *cx, char *url, struct n_buf *nb)
{
  FILE *file = NULL;
  int rc = -1;

  n_buf_destroy(nb);

  file = open_memstream(&nb->nb_buf, &nb->nb_size);
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

  nb->nb_end = nb->nb_size;

  return rc;
}

int curl_x_get(struct curl_x *cx, const char *path, const char *qstr,
               struct n_buf *nb)
{
  char *url = NULL;
  int rc = -1;

  url = strf("http://%s/%s%s%s",
             cx->cx_host, path,
             qstr != NULL ? "?" : "",
             qstr != NULL ? qstr : "");
  if (url == NULL)
    goto out;

  TRACE("url `%s'\n", url);

  if (curl_x_get_url(cx, url, nb) < 0)
    goto out;

  rc = 0;

 out:
  free(url);

  return rc;
}

typedef int (msg_cb_t)(void *, char *, size_t);

static int curl_x_get_iter(const char *path, const char *query,
                           msg_cb_t *cb, void *data)
{
  N_BUF(nb);
  char *msg;
  size_t msg_len;
  int rc = -1;

  if (curl_x_get(&curl_x, path, query, &nb) < 0)
    goto out;

  while (n_buf_get_msg(&nb, &msg, &msg_len) == 0) {
    rc = (*cb)(data, msg, msg_len);
    if (rc < 0)
      goto out;
  }

 out:
  n_buf_destroy(&nb);

  return rc;
}

char *query_escape(const char *s)
{
  char *e = malloc(3 * strlen(s) + 1), *p, x[4];
  if (e == NULL)
    return NULL;

  for (p = e; *s != 0; s++) {
    if (isalnum(*s) || *s == '.' || *s == '-' || *s == '~' || *s == '_') {
      *(p++) = *s;
    } else {
      snprintf(x, sizeof(x), "%%%02hhX", (unsigned char) *s);
      *(p++) = x[0];
      *(p++) = x[1];
      *(p++) = x[2];
    }
  }

  *p = 0;

  return e;
}

int query_add(char **s, const char *f, const char *v)
{
  char *f1 = query_escape(f), *v1 = query_escape(v);
  char *s0 = *s, *s1 = NULL;
  int rc = -1;

  if (f1 == NULL || v1 == NULL)
    goto err;

  if (s0 == NULL)
    s1 = strf("%s=%s", f1, v1);
  else
    s1 = strf("%s&%s=%s", s0, f1, v1);

  if (s1 == NULL)
    goto err;

  rc = 0;

 err:

  free(s0);
  *s = s1;

  return rc;
}

int query_addz(char **s, const char *f, size_t n)
{
  char v[3 * sizeof(n) + 1];

  snprintf(v, sizeof(v), "%zu", n);

  return query_add(s, f, v);
}

static char *make_top_query(char **arg_list, size_t nr_args)
{
  char *q = NULL, *s[2] = { NULL };
  char *x[2] = { NULL };
  size_t d[2] = { 0 };
  int t[2], set[NR_X_TYPES] = { 0 };
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
    t[0] = X_HOST;
    x[0] = val[X_HOST];
    d[0] = 0;
  } else if (val[X_JOB] != NULL) {
    t[0] = X_JOB;
    x[0] = val[X_JOB];
    d[0] = set[X_HOST] ? 1 : 0;
  } else if (val[X_CLUS] != NULL) {
    t[0] = X_CLUS;
    x[0] = val[X_CLUS];
    d[0] = set[X_HOST] ? 2 : (set[X_JOB] ? 1 : 0);
  } else {
    t[0] = X_ALL_0;
    x[0] = "ALL";
    d[0] = set[X_HOST] ? 3 : (set[X_JOB] ? 2 : (set[X_CLUS] ? 1 : 2)); /* Show jobs. */
  }

  if (val[X_SERV] != NULL) {
    t[1] = X_SERV;
    x[1] = val[X_SERV];
    d[1] = 0;
  } else if (val[X_FS] != NULL) {
    t[1] = X_FS;
    x[1] = val[X_FS];
    d[1] = set[X_SERV] ? 1 : 0;
  } else {
    t[1] = X_ALL_1;
    x[1] = "ALL";
    d[1] = set[X_SERV] ? 2 : 1; /* Show filesystems. */
  }

  /* TODO Fully qualify host, serv, job if needed. */

  for (i = 0; i < 2; i++) {
    const char *type_name = x_type_name(t[i]);

    size_t size = strlen(type_name) + strlen(x[i]) + 2;

    s[i] = malloc(size);
    if (s[i] == NULL)
      goto err;

    snprintf(s[i], size, "%s:%s", type_name, x[i]);
  }

  if (query_add(&q, "x0", s[0]) < 0)
    goto err;
  if (query_addz(&q, "d0", d[0]) < 0)
    goto err;

  if (query_add(&q, "x1", s[1]) < 0)
    goto err;
  if (query_addz(&q, "d1", d[1]) < 0)
    goto err;

  if (query_addz(&q, "limit", top_limit) < 0)
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

int get_x_nr_hint(int type, size_t *hint)
{
  N_BUF(nb);
  char *path = NULL;
  int rc = -1;
  char *m, *k, *v;
  size_t m_len, n = 0;

  path = strf("%s/_info", x_type_name(type));
  if (path == NULL)
    goto out;

  if (curl_x_get(&curl_x, path, NULL, &nb) < 0)
    goto out;

  while (n_buf_get_msg(&nb, &m, &m_len) == 0) {
    if (split(&m, &k, &v, (char **) NULL) != 2)
      continue;

    if (strcmp(k, "x_nr:") == 0)
      n = MAX(n, (size_t) strtoul(v, NULL, 0));

    if (strcmp(k, "x_nr_hint:") == 0)
      n = MAX(n, (size_t) strtoul(v, NULL, 0));
  }

  *hint = n;
  TRACE("type `%s', *hint %zu\n", x_type_name(type), *hint);

  if (n != 0)
    rc = 0;

 out:
  n_buf_destroy(&nb);
  free(path);

  return rc;
}

int x_hash_init(int type)
{
  size_t hint = 0;

  if (get_x_nr_hint(type, &hint) < 0)
    return -1;

  if (hash_table_init(&xl_hash_table[type], hint) < 0)
    return -1;

  return 0;
}

#define xl_lookup(p, i, name, xl_type, m_hash_node, m_name)             \
  do {                                                                  \
    struct hash_table *_t = &xl_hash_table[(i)];                        \
    struct hlist_head *head;                                            \
    const char *_name = (name);                                         \
    typeof(xl_type) *_p;                                                \
                                                                        \
    _p = str_table_lookup_entry(_t, _name, &head, xl_type,              \
                                m_hash_node, m_name);                   \
    if (_p == NULL) {                                                   \
      _p = malloc(sizeof(*_p) + strlen(_name) + 1);                     \
      if (_p == NULL)                                                   \
        OOM();                                                          \
      memset(_p, 0, sizeof(*_p));                                       \
      strcpy(_p->m_name, _name);                                        \
      hlist_add_head(&_p->m_hash_node, head);                           \
    }                                                                   \
                                                                        \
    (p) = _p;                                                           \
  } while (0)

static int xl_clus_msg_cb(struct xl_clus *c, char *m, size_t m_len)
{
  char *s_host, *s_job, *owner, *title, *s_start, *s_nr_hosts;
  struct xl_host *h;
  struct xl_job *j;

  if (split(&m, &s_host, &s_job, &owner, &title, &s_start, &s_nr_hosts,
            (char **) NULL) != 6)
    return 0;

  xl_lookup(h, X_HOST, s_host, struct xl_host, h_hash_node, h_name);

  xl_lookup(j, X_JOB, s_job, struct xl_job, j_hash_node, j_name);

  h->h_job = j;

  if (j->j_clus_link.next == NULL) {
    INIT_LIST_HEAD(&j->j_clus_link);
    j->j_owner = strdup(owner);
    j->j_title = strdup(title);
    j->j_start = strtod(s_start, NULL);
    j->j_nr_hosts = strtoul(s_nr_hosts, NULL, 0);
  }

  list_move(&j->j_clus_link, &c->c_job_list);

  return 0;
}

static void xl_clus_cb(EV_P_ struct ev_periodic *w, int revents)
{
  struct xl_clus *c = container_of(w, struct xl_clus, c_w);
  struct xl_job *j, *j_tmp;
  char *path = NULL;
  LIST_HEAD(tmp_list);

  TRACE("clus `%s', now %.0f\n", c->c_name, ev_now(EV_A));

  list_splice_init(&c->c_job_list, &tmp_list);

  path = strf("clus/%s", c->c_name);
  if (path == NULL)
    OOM();

  curl_x_get_iter(path, NULL, (msg_cb_t *) &xl_clus_msg_cb, c);

  free(path);

  list_for_each_entry_safe(j, j_tmp, &tmp_list, j_clus_link) {
    hlist_del(&j->j_hash_node);
    list_del(&j->j_clus_link);
    free(j->j_owner);
    free(j->j_title);
    free(j);
  }
}

int xl_clus_add(EV_P_ const char *name)
{
  N_BUF(nb);
  int rc = -1;
  char *info_path = NULL, *m, *k, *v;
  size_t m_len;
  struct xl_clus *c = NULL;
  double c_int = -1, c_off = -1;

  xl_lookup(c, X_CLUS, name, struct xl_clus, c_hash_node, c_name);

  if (c->c_hash_node.next != NULL)
    return 0;

  info_path = strf("clus/%s/_info", name);
  if (info_path == NULL)
    OOM();

  if (curl_x_get(&curl_x, info_path, NULL, &nb) < 0)
    goto err;

  while (n_buf_get_msg(&nb, &m, &m_len) == 0) {
    if (split(&m, &k, &v, (char **) NULL) != 2)
      continue;

    if (strcmp(k, "interval:") == 0)
      c_int = strtod(v, NULL);
    else if (strcmp(k, "offset:") == 0)
      c_off = strtod(v, NULL);
  }

  if (c_int < 0 || c_off < 0)
    goto err;

  INIT_LIST_HEAD(&c->c_job_list);
  c_off = fmod(c_off + 1, c_int); /* XXX */
  ev_periodic_init(&c->c_w, &xl_clus_cb, c_off, c_int, NULL);
  ev_periodic_start(EV_A_ &c->c_w);
  ev_feed_event(EV_A_ &c->c_w, 0);

  rc = 0;

  if (0) {
  err:
    if (c != NULL)
      hlist_del(&c->c_hash_node);
    free(c);
  }

  n_buf_destroy(&nb);
  free(info_path);

  return rc;
}

static int xl_clus_init(EV_P)
{
  N_BUF(nb);
  int rc = -1;
  char *m, *name;
  size_t m_len;

  if (x_hash_init(X_CLUS) < 0)
    FATAL("cannot initialize clus table\n");

  if (curl_x_get(&curl_x, "clus", NULL, &nb) < 0)
    goto out;

  while (n_buf_get_msg(&nb, &m, &m_len) == 0) {
    if (split(&m, &name, (char **) NULL) != 1)
      continue;

    if (xl_clus_add(EV_A_ name) < 0)
      goto out;
  }

  rc = 0;

 out:
  n_buf_destroy(&nb);

  return rc;
}

static int xl_fs_msg_cb(struct xl_fs *f, char *msg, size_t msg_len)
{
  char *s_serv;
  struct serv_status ss = {};
  int i;

  if (split(&msg, &s_serv, (char **) NULL) != 1 || msg == NULL)
    return 0;

  if (sscanf(msg, SCN_SERV_STATUS_FMT, SCN_SERV_STATUS_ARG(ss)) !=
      NR_SCN_SERV_STATUS_ARGS)
    return 0;

  TRACE("serv `%s', status "PRI_SERV_STATUS_FMT"\n",
        s_serv, PRI_SERV_STATUS_ARG(ss));

  /* Cheezy. */
  if (strncmp(s_serv, "mds", 3) == 0)
    for (i = 0; i < 3; i++)
      f->f_mds_load[i] = MAX(f->f_mds_load[i], ss.ss_load[i]);
  else
    for (i = 0; i < 3; i++)
      f->f_oss_load[i] = MAX(f->f_oss_load[i], ss.ss_load[i]);

  f->f_nr_tgts += ss.ss_nr_tgts;
  f->f_nr_nids = MAX(f->f_nr_nids, ss.ss_nr_nids);

  return 0;
}

static void xl_fs_cb(EV_P_ struct ev_periodic *w, int revents)
{
  struct xl_fs *f = container_of(w, struct xl_fs, f_w);
  char *status_path = NULL;

  TRACE("fs `%s', now %.0f\n", f->f_name, ev_now(EV_A));

  status_path = strf("fs/%s/_status", f->f_name);
  if (status_path == NULL)
    OOM();

  curl_x_get_iter(status_path, NULL, (msg_cb_t *) &xl_fs_msg_cb, f);

  printf("fs %s, load MDS %.2f %.2f %.2f, OSS %.2f %.2f %.2f, tgts %zu, nids %zu\n",
         f->f_name, f->f_mds_load[0], f->f_mds_load[1], f->f_mds_load[2],
         f->f_oss_load[0], f->f_oss_load[1], f->f_oss_load[2],
         f->f_nr_tgts, f->f_nr_nids);

  free(status_path);
}

int xl_fs_add(EV_P_ const char *name)
{
  struct xl_fs *f;
  double f_int = 30; /* XXX */

  xl_lookup(f, X_FS, name, struct xl_fs, f_hash_node, f_name);

  if (f->f_hash_node.next != NULL)
    return 0;

  ev_periodic_init(&f->f_w, &xl_fs_cb, 0, f_int, NULL);
  ev_periodic_start(EV_A_ &f->f_w);
  ev_feed_event(EV_A_ &f->f_w, 0);

  return 0;
}

static int xl_fs_init(EV_P)
{
  N_BUF(nb);
  int rc = -1;
  char *m, *name;
  size_t m_len;

  if (x_hash_init(X_FS) < 0)
    FATAL("cannot initialize fs table\n");

  if (curl_x_get(&curl_x, "fs", NULL, &nb) < 0)
    goto out;

  while (n_buf_get_msg(&nb, &m, &m_len) == 0) {
    if (split(&m, &name, (char **) NULL) != 1)
      continue;

    if (xl_fs_add(EV_A_ name) < 0)
      goto out;
  }

  rc = 0;

 out:
  n_buf_destroy(&nb);

  return rc;
}

static int top_msg_cb(EV_P_ char *msg, size_t msg_len)
{
  char *t[2], *x[2];
  struct k_node k;

  if (split(&msg, &x[0], &x[1], (char **) NULL) != 2 || msg == NULL)
    return 0;

  t[0] = strsep(&x[0], ":");
  t[1] = strsep(&x[1], ":");

  if (t[0] == NULL || t[1] == NULL || x[0] == NULL || x[1] == NULL)
    return 0;

  if (sscanf(msg, "%lf "SCN_K_STATS_FMT, &k.k_t, SCN_K_STATS_ARG(&k)) !=
      1 + NR_K_STATS)
    return 0;

  printf("%s %s "PRI_STATS_FMT("%f")"\n",
         x[0], x[1], PRI_STATS_ARG(k.k_rate));

  return 0;
}

static void top_timer_cb(EV_P_ ev_timer *w, int revents)
{
  static int nr_errs = 0;
  double now = ev_now(EV_A);

  TRACE("begin, now %f\n", now);

  if (curl_x_get_iter("top", top_query, (msg_cb_t *) &top_msg_cb, EV_A) < 0) {
    if (nr_errs++ > 3)
      FATAL("cannot GET /top?%s\n", top_query); /* XXX */
    return;
  }
  nr_errs = 0;

  TRACE("end\n\n\n\n");
}

static void top_init(EV_P_ char **arg_list, size_t nr_args)
{
  if (x_hash_init(X_HOST) < 0)
    FATAL("cannot initialize host table\n");

  if (x_hash_init(X_JOB) < 0)
    FATAL("cannot initialize job table\n");

  if (xl_fs_init(EV_A) < 0)
    FATAL("cannot initialize fs data\n");

  if (xl_clus_init(EV_A) < 0)
    FATAL("cannot initialize cluster data\n");

  top_query = make_top_query(arg_list, nr_args);

  if (top_query == NULL)
    OOM();

  signal(SIGPIPE, SIG_IGN);

  ev_timer_init(&top_timer_w, &top_timer_cb, 0.1, top_interval);

  ev_timer_start(EV_A_ &top_timer_w);

  ev_run(EV_A_ 0);
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
      top_interval = strtod(optarg, NULL);
      if (top_interval <= 0)
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
      top_show_sum = 1;
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

  if (top_interval <= 0)
    FATAL("invalid interval %f, must be positive\n",
          top_interval);

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
    top_init(EV_DEFAULT_ argv + optind, argc - optind);
  else if (strcmp(cmd, "top") == 0)
    top_init(EV_DEFAULT_ args, nr_args);
  else if (strcmp(cmd, "status") == 0)
    do_status(args, nr_args);
  else
    FATAL("unrecognized command `%s'\n", cmd);

  if (curl_x.cx_curl != NULL)
    curl_easy_cleanup(curl_x.cx_curl);

  curl_global_cleanup();

  return 0;
}
