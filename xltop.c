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

static const char *r_host = "localhost"; /* XXX */
static long r_port;
static CURL *r_curl;
static const char *r_sort;

static char *top_url = NULL;

static double timer_interval = 4;
static struct ev_timer timer_w;

static int get(const char *url, char **buf, size_t *len)
{
  FILE *file = NULL;
  int rc = -1;

  file = open_memstream(buf, len);
  if (file == NULL) {
    ERROR("cannot open memory stream: %m\n");
    goto out;
  }

  curl_easy_reset(r_curl);
  curl_easy_setopt(r_curl, CURLOPT_URL, url);
  curl_easy_setopt(r_curl, CURLOPT_PORT, r_port);
  curl_easy_setopt(r_curl, CURLOPT_WRITEDATA, file);

#if DEBUG
  curl_easy_setopt(r_curl, CURLOPT_VERBOSE, 1L);
#endif

  int curl_rc = curl_easy_perform(r_curl);
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

  return rc;
}

static void timer_cb(EV_P_ ev_timer *w, int revents)
{
  /* double now = ev_now(EV_A); */
  char *top_buf = NULL;
  size_t top_len = 0;

  if (get(top_url, &top_buf, &top_len) < 0)
    FATAL("cannot get `%s'\n", top_url);

  TRACE("top_buf `%.40s', top_len %zd\n", top_buf, top_len);

  free(top_buf);
}

static int make_top_url(char **url, char **arg_list, size_t nr_args)
{
  char *x0 = NULL, *x1 = NULL, *t0 = NULL, *t1 = NULL;
  size_t d0 = 0, d1 = 0;

  int clus_set = 0, job_set = 0, host_set = 0;
  char *clus_arg = NULL, *job_arg = NULL, *host_arg = NULL;

  int fs_set = 0, serv_set = 0;
  char *fs_arg = NULL, *serv_arg = NULL;

  size_t i;
  for (i = 0; i < nr_args; i++) {
    char *arg = arg_list[i];
    char *type = strsep(&arg, "=:");

    if (type == NULL)
      continue;

    if (strcmp(type, "clus") == 0) {
      clus_set = 1;
      clus_arg = arg;
    } else if (strcmp(type, "job") == 0) {
      job_set = 1;
      job_arg = arg;
    } else if (strcmp(type, "host") == 0) {
      host_set = 1;
      host_arg = arg;
    } else if (strcmp(type, "fs") == 0) {
      fs_set = 1;
      fs_arg = arg;
    } else if (strcmp(type, "serv") == 0) {
      serv_set = 1;
      serv_arg = arg;
    } else {
      FATAL("unrecognized type `%s'\n", type);
    }
  }

  if (host_arg != NULL) {
    t0 = "host";
    x0 = host_arg;
    d0 = 0;
  } else if (job_arg != NULL) {
    t0 = "job";
    x0 = job_arg;
    d0 = host_set ? 1 : 0;
  } else if (clus_arg != NULL) {
    t0 = "clus";
    x0 = clus_arg;
    d0 = host_set ? 2 : (job_set ? 1 : 0);
  }

  if (t0 == NULL) {
    t0 = "all_0";
    x0 = "ALL";
    d0 = 2; /* Show jobs. */
  }

  if (serv_arg != NULL) {
    t1 = "serv";
    x1 = serv_arg;
    d1 = 0;
  } else if (fs_arg != NULL) {
    t1 = "fs";
    x1 = fs_arg;
    d1 = serv_set ? 1 : 0;
  }

  if (t1 == NULL) {
    t1 = "all_1";
    x1 = "ALL";
    d1 = 1; /* Show filesystems. */
  }

  if (asprintf(url, "http://%s/top?"
               "x0=%s:%s&d0=%zu&"
               "x1=%s:%s&d1=%zu&"
               /* limit */
               /* sort */
               ,
               r_host, t0, x0, d0, t1, x1, d1) < 0)
    return -1;

  TRACE("url `%s'\n", *url);

  return 0;
}

static void do_top(char **arg_list, size_t nr_args)
{
  if (make_top_url(&top_url, arg_list, nr_args) < 0)
    FATAL("out of memory\n");

  signal(SIGPIPE, SIG_IGN);

  ev_timer_init(&timer_w, &timer_cb, 0.1, timer_interval);

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
  const char *o_host = NULL, *o_port = NULL, *conf_path = NULL;

  struct option opts[] = {
    { "conf",        1, NULL, 'c' },
    { "help",        0, NULL, 'h' },
    { "interval",    1, NULL, 'i' },
    { "limit",       1, NULL, 'l' },
    { "remote-port", 1, NULL, 'p' },
    { "remote-host", 1, NULL, 'r' },
    { "sort",        1, NULL, 's' },
    { NULL,          0, NULL,  0  },
  };

  /* Show rate or show sum. */
  /* Sort spec depends on which. */
  /* Limit.  Scrolling. */

  int c;
  while ((c = getopt_long(argc, argv, "c:hi:p:r:s:", opts, 0)) > 0) {
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
    case 'l':
      /* ... */
      break;
    case 'p':
      o_port = optarg;
      break;
    case 'r':
      o_host = optarg;
      break;
    case 's':
      r_sort = optarg;
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

  r_curl = curl_easy_init();
  if (r_curl == NULL)
    FATAL("cannot initialize curl handle: %m\n");

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

  if (r_curl != NULL)
    curl_easy_cleanup(r_curl);

  curl_global_cleanup();

  return 0;
}
