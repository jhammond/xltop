#include "stddef1.h"
#include <stdio.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <malloc.h>
#include <signal.h>
#include <unistd.h>
#include <curl/curl.h>
#include <ev.h>
#include "xltop.h"
#include "hash.h"
#include "list.h"
#include "n_buf.h"
#include "string1.h"
#include "trace.h"

#define NR_LXT_HINT 16
#define NR_NIDS_HINT 4096
#define LXT_STATS_BUF_SIZE 4096

#define P_FMT PRI_STATS_FMT("%"PRId64)
#define P_ARG PRI_STATS_ARG

#define TRACE_BUF(b,n) \
  TRACE("%s `%.40s', %s %zu\n", #b, ((b) != NULL ? (b) : "NULL"), #n, (n))

typedef int64_t lc_t; /* _s64 in Lustre source. */

/* TODO Remove stale entries from l_hash_table. */

static inline lc_t strtolc(const char *s)
{
  return strtoll(s, NULL, 10);
}

static char *s_name;
static char host_name[HOST_NAME_MAX + 1];
static const char *r_host = "localhost"; /* XXX */
static long r_port = 9901; /* XXX */
static CURL *r_curl;

static struct serv_status serv_status;

static struct ev_periodic clock_w;

static inline int debug_nid(const char *nid)
{
#ifdef DEBUG_NIDS
  return strstr(DEBUG_NIDS, nid) != NULL;
#else
  return 0;
#endif
}

static size_t nr_nids_hint = NR_NIDS_HINT;
static struct hash_table nid_hash_table;

static LIST_HEAD(lxt_list);
static struct hash_table lxt_hash_table;

struct nid_stats {
  struct hlist_node ns_hash_node;
  lc_t ns_stats[NR_STATS];
  double ns_time;
  char ns_nid[];
};

static const char *top_dir_path[] = {
  "/proc/fs/lustre/mdt",
  "/proc/fs/lustre/obdfilter",
};

struct lxt {
  struct hash_table l_hash_table;
  struct hlist_node l_hash_node;
  struct list_head l_link;
  char l_name[];
};

static struct nid_stats *
nid_stats_lookup(struct hash_table *t, const char *nid)
{
  struct hlist_head *head;
  struct nid_stats *ns;

  ns = str_table_lookup_entry(t, nid, &head, struct nid_stats,
                              ns_hash_node, ns_nid);
  if (ns != NULL)
    goto out;

  ns = malloc(sizeof(*ns) + strlen(nid) + 1);
  if (ns == NULL)
    goto out;

  memset(ns, 0, sizeof(*ns));
  strcpy(ns->ns_nid, nid);
  hlist_add_head(&ns->ns_hash_node, head);

 out:
  return ns;
}

static void nid_stats_delete(struct nid_stats *ns)
{
  TRACE("deleting nid_stats `%s'\n", ns->ns_nid);

  hlist_del(&ns->ns_hash_node);
  free(ns);
}

/* Assumes we're already in the target directory. */
static int nid_stats_read(const char *nid, lc_t *stats)
{
  char buf[LXT_STATS_BUF_SIZE];
  struct n_buf nb = {
    .nb_buf = buf,
    .nb_size = sizeof(buf),
  };
  char path[256];
  int rc = -1, fd = -1, err = 0, eof = 0;

  memset(stats, 0, NR_STATS * sizeof(*stats));

  snprintf(path, sizeof(path), "%s/stats", nid);
  fd = open(path, O_RDONLY);
  if (fd < 0) {
    ERROR("cannot open %s: %m\n", path);
    goto err;
  }

  n_buf_fill(&nb, fd, &err, &eof);
  if (err != 0) {
    ERROR("error reading from `%s': %m\n", path);
    goto err;
  }

  char *msg;
  size_t msg_len;

  while (n_buf_get_msg(&nb, &msg, &msg_len) == 0) {
    char *ctr, *req, *ign, *unit, *min, *max, *sum;
    int n = split(&msg, &ctr, &req, &ign, &unit, &min, &max, &sum,
                  (char **) NULL);

    if (n < 4 || strcmp(ctr, "ping") == 0)
      continue;

    if (strcmp(unit, "[reqs]") == 0)
      stats[STAT_NR_REQS] += strtolc(req);

    if (n < 7 || strcmp(unit, "[bytes]") != 0)
      continue;

    if (strcmp(ctr, "write_bytes") == 0)
      stats[STAT_WR_BYTES] += strtolc(sum);
    else if (strcmp(ctr, "read_bytes") == 0)
      stats[STAT_RD_BYTES] += strtolc(sum);
  }

  rc = 0;
 err:

  if (!(fd < 0))
    close(fd);

  return rc;
}

static void lxt_delete(struct lxt *l)
{
  struct hash_table *t = &l->l_hash_table;
  struct hlist_node *node, *tmp;
  struct nid_stats *ns;
  size_t i;

  TRACE("deleting lxt `%s'\n", l->l_name);

  serv_status.ss_nr_tgts--;

  for (i = 0; i < (1ULL << t->t_shift); i++)
    hlist_for_each_entry_safe(ns, node, tmp, t->t_table + i, ns_hash_node)
      nid_stats_delete(ns);
  free(t->t_table);

  hlist_del(&l->l_hash_node);
  list_del(&l->l_link);
  free(l);
}

struct lxt *lxt_lookup(const char *name)
{
  struct hlist_head *head;
  struct lxt *l = NULL;

  l = str_table_lookup_entry(&lxt_hash_table, name, &head,
                             struct lxt, l_hash_node, l_name);
  if (l != NULL)
    return l;

  TRACE("creating lxt `%s'\n", name);

  l = malloc(sizeof(*l) + strlen(name) + 1);
  if (l == NULL)
    goto err;

  memset(l, 0, sizeof(*l));
  strcpy(l->l_name, name);

  size_t hint = MAX(serv_status.ss_nr_nids, nr_nids_hint);

  if (hash_table_init(&l->l_hash_table, hint) < 0)
    goto err;

  hlist_add_head(&l->l_hash_node, head);
  list_add(&l->l_link, &lxt_list);

  serv_status.ss_nr_tgts++;

  return l;

 err:
  if (l != NULL) {
    free(l->l_hash_table.t_table);
    free(l);
  }

  return NULL;
}

static void lxt_collect_nid(struct lxt *l, const char *nid, double now)
{
  struct nid_stats *ts, *ns;
  lc_t stats[NR_STATS];
  int i;

  if (debug_nid(nid)) {
    char cwd[256];
    getcwd(cwd, sizeof(cwd));
    TRACE("nid `%s' now %f, cwd `%s'\n", nid, now, cwd);
  }

  if (nid_stats_read(nid, stats) < 0)
    return;

  ts = nid_stats_lookup(&l->l_hash_table, nid);
  if (ts == NULL)
    return;

  if (debug_nid(nid))
    TRACE("ts time %f\n"
          "ts old stats "P_FMT"\n"
          "ts new stats "P_FMT"\n",
          ts->ns_time, P_ARG(ts->ns_stats), P_ARG(stats));

  /* Need to handle stale case? */
  if (ts->ns_time == 0)
    goto out;

  ns = nid_stats_lookup(&nid_hash_table, nid);
  if (ns == NULL)
    goto out;

  if (ns->ns_time == 0)
    serv_status.ss_nr_nids++;

  if (debug_nid(nid))
    TRACE("ns time %f, old stats "P_FMT"\n",
          ns->ns_time, P_ARG(ns->ns_stats));

  if (ns->ns_time != now) {
    ns->ns_time = now;
    memset(ns->ns_stats, 0, sizeof(ns->ns_stats));
  }

  for (i = 0; i < NR_STATS; i++)
    ns->ns_stats[i] += stats[i] - ts->ns_stats[i];

  if (debug_nid(nid))
    TRACE("ns time %f, new stats "P_FMT"\n",
          ns->ns_time, P_ARG(ns->ns_stats));

 out:
  memcpy(ts->ns_stats, stats, sizeof(stats));
  ts->ns_time = now;
}

static int lxt_collect(struct lxt *l, const char *exp_dir_path, double now)
{
  DIR *exp_dir = NULL;
  struct dirent *de;
  int rc = -1;

  if (chdir(exp_dir_path) < 0) {
    ERROR("cannot chdir to `%s': %m\n", exp_dir_path);
    goto err;
  }

  exp_dir = opendir(".");
  if (exp_dir == NULL) {
    ERROR("cannot open `%s': %m\n", ".");
    goto err;
  }

  while ((de = readdir(exp_dir)) != NULL)
    if (de->d_type == DT_DIR && de->d_name[0] != '.')
      lxt_collect_nid(l, de->d_name, now);

  rc = 0;

 err:
  if (exp_dir != NULL)
    closedir(exp_dir);

  return rc;
}

static void collect_all(double now)
{
  struct lxt *l, *l_tmp;
  LIST_HEAD(tmp_list);
  size_t i;

  list_splice_init(&lxt_list, &tmp_list);

  ASSERT(list_empty(&lxt_list));

  for (i = 0; i < sizeof(top_dir_path) / sizeof(top_dir_path[0]); i++) {
    DIR *top_dir = NULL;

    top_dir = opendir(top_dir_path[i]);
    if (top_dir == NULL) {
      ERROR("cannot open `%s': %m\n", top_dir_path[i]);
      goto next;
    }

    struct dirent *de;
    while ((de = readdir(top_dir)) != NULL) {
      char exp_dir_path[256];

      if (de->d_type != DT_DIR || de->d_name[0] == '.')
        continue;

      TRACE("de_name `%s'\n", de->d_name);

      snprintf(exp_dir_path, sizeof(exp_dir_path), "%s/%s/exports",
               top_dir_path[i], de->d_name);

      l = lxt_lookup(de->d_name);
      if (l == NULL)
        continue;

      if (lxt_collect(l, exp_dir_path, now) == 0)
        list_move(&l->l_link, &lxt_list);
    }

  next:
    if (top_dir != NULL)
      closedir(top_dir);

    chdir("/");
  }

  /* Kill all lxt's we didn't just see. */
  list_for_each_entry_safe(l, l_tmp, &tmp_list, l_link)
    lxt_delete(l);

  ASSERT(list_empty(&tmp_list));
}

static inline int stats_are_zero(lc_t *s)
{
  size_t j;
  for (j = 0; j < NR_STATS; j++)
    if (s[j] != 0)
      return 0;
  return 1;
}

static int print_stats(char **buf, size_t *len, double now)
{
  struct hash_table *t = &nid_hash_table;
  struct hlist_node *node, *tmp;
  struct nid_stats *ns;
  FILE *file = NULL;
  int rc = -1;

  TRACE("now %f\n", now);

  file = open_memstream(buf, len);
  if (file == NULL) {
    ERROR("cannot open memory stream: %m\n");
    goto out;
  }

  size_t i;
  for (i = 0; i < (1ULL << t->t_shift); i++) {
    hlist_for_each_entry_safe(ns, node, tmp, t->t_table + i, ns_hash_node) {

      if (debug_nid(ns->ns_nid))
        TRACE("nid `%s', time %f, stats "P_FMT"\n",
              ns->ns_nid, ns->ns_time, P_ARG(ns->ns_stats));

      if (ns->ns_time != now) {
        nid_stats_delete(ns);
        serv_status.ss_nr_nids--;
        continue;
      }

      if (stats_are_zero(ns->ns_stats))
        continue;

      fprintf(file, "%s "P_FMT"\n", ns->ns_nid, P_ARG(ns->ns_stats));
    }
  }

  TRACE("nr_nids %zu\n\n", serv_status.ss_nr_nids);

  if (ferror(file)) {
    ERROR("error writing to memory stream: %m\n");
    goto out;
  }

  rc = 0;

 out:
  if (file != NULL)
    fclose(file);

  TRACE_BUF(*buf, *len);

  return rc;
}

static int upload(const char *url, char *buf[2], size_t len[2])
{
  FILE *file[2] = { NULL, NULL };
  int rc = -1;

  /* fmemopen() fails when buffer size is zero. */
  if (buf[0] == NULL || len[0] == 0) {
    file[0] = fopen("/dev/null", "r");
    if (file[0] == NULL) {
      ERROR("cannot open `/dev/null' for reading: %m\n");
      goto out;
    }
  } else {
    file[0] = fmemopen(buf[0], len[0], "r");
    if (file[0] == NULL) {
      ERROR("cannot open memory stream: %m\n");
      TRACE_BUF(buf[0], len[0]);
      goto out;
    }
  }

  file[1] = open_memstream(&buf[1], &len[1]);
  if (file[1] == NULL) {
    ERROR("cannot open memory stream: %m\n");
    goto out;
  }

  curl_easy_reset(r_curl);
  curl_easy_setopt(r_curl, CURLOPT_URL, url);
  curl_easy_setopt(r_curl, CURLOPT_PORT, r_port);
  curl_easy_setopt(r_curl, CURLOPT_UPLOAD, 1L);
  curl_easy_setopt(r_curl, CURLOPT_READDATA, file[0]);
  curl_easy_setopt(r_curl, CURLOPT_INFILESIZE_LARGE, (curl_off_t) len[0]);
  curl_easy_setopt(r_curl, CURLOPT_WRITEDATA, file[1]);

#if DEBUG
  curl_easy_setopt(r_curl, CURLOPT_VERBOSE, 1L);
#endif

  int curl_rc = curl_easy_perform(r_curl);
  if (curl_rc != 0) {
    ERROR("cannot upload to `%s': %s\n", url, curl_easy_strerror(rc));
    /* Reset curl... */
    goto out;
  }

  if (ferror(file[1])) {
    ERROR("error writing to memory stream: %m\n");
    goto out;
  }

  rc = 0;

 out:
  if (file[0] != NULL)
    fclose(file[0]);

  if (file[1] != NULL)
    fclose(file[1]);

  return rc;
}

static int send_serv_status(double now, double *interval, double *offset)
{
  char url[1024], *buf[2] = { NULL, NULL };
  size_t len[2] = { 0, 0 };
  FILE *info_file = NULL;
  struct sysinfo si;
  int rc = -1;

  snprintf(url, sizeof(url), "http://%s/serv/%s/_status", r_host, s_name);

  if (sysinfo(&si) < 0) {
    ERROR("cannot get current sysinfo: %m\n");
    memset(&si, 0, sizeof(si));
  }

  serv_status.ss_time = now;
  serv_status.ss_uptime = si.uptime;
  serv_status.ss_load[0] = si.loads[0] / 65536.0;
  serv_status.ss_load[1] = si.loads[1] / 65536.0;
  serv_status.ss_load[2] = si.loads[2] / 65536.0;

  serv_status.ss_total_ram  = si.totalram  * si.mem_unit;
  serv_status.ss_free_ram   = si.freeram   * si.mem_unit;
  serv_status.ss_shared_ram = si.sharedram * si.mem_unit;
  serv_status.ss_buffer_ram = si.bufferram * si.mem_unit;
  serv_status.ss_total_swap = si.totalswap * si.mem_unit;
  serv_status.ss_free_swap  = si.freeswap  * si.mem_unit;

  serv_status.ss_nr_tasks = si.procs;

  info_file = open_memstream(&buf[0], &len[0]);
  if (info_file == NULL) {
    ERROR("cannot open memory stream: %m\n");
    goto out;
  }

  fprintf(info_file, PRI_SERV_STATUS_FMT"\n", PRI_SERV_STATUS_ARG(serv_status));
  fclose(info_file);
  info_file = NULL;

  TRACE_BUF(buf[0], len[0]);

  if (upload(url, buf, len) < 0)
    goto out;

  TRACE_BUF(buf[1], len[1]);

  if (buf[1] == NULL || len[1] == 0)
    goto out;

  if (sscanf(buf[1], "%lf %lf", interval, offset) != 2)
    goto out;

  rc = 0;

 out:
  if (info_file != NULL)
    fclose(info_file);
  free(buf[0]);
  free(buf[1]);

  return rc;
}

static void send_stats(double now)
{
  char url[1024], *buf[2] = { NULL, NULL };
  size_t len[2] = { 0, 0 };

  snprintf(url, sizeof(url), "http://%s/serv/%s", r_host, s_name);

  if (print_stats(&buf[0], &len[0], now) < 0)
    goto out;

  TRACE_BUF(buf[0], len[0]);

  if (upload(url, buf, len) < 0)
    goto out;

  TRACE_BUF(buf[1], len[1]);

 out:
  free(buf[0]);
  free(buf[1]);
}

static void clock_cb(EV_P_ ev_periodic *w, int revents)
{
  double now = ev_now(EV_A);
  double c_interval, c_offset;

  collect_all(now);

  send_stats(now);

  if (send_serv_status(now, &c_interval, &c_offset) < 0)
    return;

  if (w->interval != c_interval || w->offset != c_offset) {
    TRACE("setting interval to %f, offset %f\n", c_interval, c_offset);
    w->interval = c_interval;
    w->offset = c_offset;
    ev_periodic_again(EV_A_ w);
  }
}

static void usage(int status)
{
  fprintf(status == 0 ? stdout : stderr,
          "Usage: %s [OPTIONS]...\n"
          /* ... */
          "\nOPTIONS:\n"
          " -a, --addr=ADDR  \n"
          /* ... */
          ,
          program_invocation_short_name);

  exit(status);
}

int main(int argc, char *argv[])
{
  const char *o_host = NULL, *o_port = NULL, *conf_path = NULL;
  double interval = 20 /* XXX */, offset = 0;

  struct option opts[] = {
    { "conf",        1, NULL, 'c' },
    { "help",        0, NULL, 'h' },
    { "interval",    1, NULL, 'i' },
    { "nr-nids",     1, NULL, 'n' },
    { "offset",      1, NULL, 'o' },
    { "remote-port", 1, NULL, 'p' },
    { "remote-host", 1, NULL, 'r' },
    { "server-name", 1, NULL, 's' },
    { NULL,          0, NULL,  0  },
  };

  int c;
  while ((c = getopt_long(argc, argv, "c:hi:n:o:p:r:s:", opts, 0)) > 0) {
    switch (c) {
    case 'c':
      conf_path = optarg;
      break;
    case 'h':
      usage(0);
      break;
    case 'i':
      interval = strtod(optarg, NULL);
      if (interval <= 0)
        FATAL("invalid interval `%s'\n", optarg);
      break;
    case 'n':
      nr_nids_hint = strtoul(optarg, NULL, 0);
      break;
    case 'o':
      offset = strtod(optarg, NULL);
      break;
    case 'p':
      o_port = optarg;
      break;
    case 'r':
      o_host = optarg;
      break;
    case 's':
      s_name = optarg;
      break;
    case '?':
      FATAL("Try `%s --help' for more information.\n", program_invocation_short_name);
    }
  }

  if (conf_path != NULL)
    /* TODO */;

  if (o_host != NULL)
    r_host = o_host;

  if (o_port != NULL)
    r_port = strtol(o_port, NULL, 0);

  if (!(0 <= offset && offset < interval))
    FATAL("invalid offset %f, must be nonnegative and less than interval\n",
          offset);

  if (gethostname(host_name, sizeof(host_name)) < 0)
    FATAL("cannot get host name: %m\n");

  if (s_name == NULL)
    s_name = host_name;

  int curl_rc = curl_global_init(CURL_GLOBAL_NOTHING);
  if (curl_rc != 0)
    FATAL("cannot initialize curl: %s\n", curl_easy_strerror(curl_rc));

  r_curl = curl_easy_init();
  if (r_curl == NULL)
    FATAL("cannot initialize curl handle: %m\n");

  ev_periodic_init(&clock_w, &clock_cb, offset, interval, NULL);

  ev_periodic_start(EV_DEFAULT_ &clock_w);

  if (hash_table_init(&nid_hash_table, nr_nids_hint) < 0)
    FATAL("cannot initialize nid hash: %m\n");

  if (hash_table_init(&lxt_hash_table, NR_LXT_HINT) < 0)
    FATAL("cannot initialize target hash: %m\n");

#if ! DEBUG
  if (daemon(0, 0) < 0)
    FATAL("cannot daemonize: %m\n");
#endif

  signal(SIGPIPE, SIG_IGN);

  ev_run(EV_DEFAULT_ 0);

  if (r_curl != NULL)
    curl_easy_cleanup(r_curl);
  curl_global_cleanup();

  return 0;
}
