#include <stddef.h>
#include <stdlib.h>
#include <malloc.h>
#include <errno.h>
#include <signal.h>
#include <ev.h>
#include "confuse.h"
#include "cl_bind.h"
#include "ap_parse.h"
#include "x_node.h"
#include "clus.h"
#include "lnet.h"
#include "serv.h"
#include "user.h"
#include "hash.h"
#include "screen.h"
#include "trace.h"

#define CLTOP_CONF_PATH "cltop.conf"
#define CLTOP_BIND_ADDR "0.0.0.0"
#define CLTOP_BIND_PORT "9901"
#define CLTOP_CLUS_INTERVAL 120.0
#define CLTOP_NR_HOSTS_HINT 4096
#define CLTOP_NR_JOBS_HINT 256
#define CLTOP_SERV_INTERVAL 300.0

#define BIND_CFG_OPTS \
  CFG_STR("bind", NULL, CFGF_NONE),           \
  CFG_STR("bind_host", NULL, CFGF_NONE),    \
  CFG_STR("bind_address", NULL, CFGF_NONE), \
  CFG_STR("bind_service", NULL, CFGF_NONE), \
  CFG_STR("bind_port", NULL, CFGF_NONE)

/* TODO bind_interface. */

int bind_cfg(EV_P_ cfg_t *cfg, char **addr, char **port)
{
  struct cl_bind *cb = NULL;
  char *opt, *dup = NULL;
  int rc = -1;

  opt = cfg_getstr(cfg, "bind");
  if (opt != NULL) {
    dup = strdup(opt);
    if (ap_parse(dup, (const char **) addr, (const char **) port) < 0)
      return -1;
  }

  opt = cfg_getstr(cfg, "bind_host");
  if (opt != NULL)
    *addr = opt;

  opt = cfg_getstr(cfg, "bind_address");
  if (opt != NULL)
    *addr = opt;

  opt = cfg_getstr(cfg, "bind_service");
  if (opt != NULL)
    *port = opt;

  opt = cfg_getstr(cfg, "bind_port");
  if (opt != NULL)
    *port = opt;

  cb = malloc(sizeof(*cb));
  if (cb == NULL)
    FATAL("out of memory\n");

  extern struct cl_conn_ops new_conn_ops;

  cl_bind_init(cb, &new_conn_ops);

  TRACE("adding bind `%s', `%s'\n", *addr, *port);

  if (cl_bind_set(cb, *addr, *port) < 0) {
    if (errno == EEXIST)
      rc = 0;
    else
      ERROR("cannot bind to host/address `%s', service/port `%s': %m\n",
            *addr, *port);
    goto err;
  }

  cl_bind_start(EV_A_ cb);

  rc = 0;
  if (0) {
  err:
    if (cb != NULL)
      cl_bind_destroy(cb);
    free(cb);
  }

  return rc;
}

static cfg_opt_t clus_cfg_opts[] = {
  /* AUTH_CFG_OPTS, */
  BIND_CFG_OPTS,
  CFG_FLOAT("interval", CLTOP_CLUS_INTERVAL, CFGF_NONE),
  CFG_END(),
};

void clus_cfg(EV_P_ cfg_t *cfg, char *addr, char *port)
{
  const char *name = cfg_title(cfg);
  struct clus_node *c;

  if (bind_cfg(EV_A_ cfg, &addr, &port) < 0)
    FATAL("invalid bind option for cluster `%s'\n", name);

  TRACE("clus `%s', addr `%s', port `%s'\n", name, addr, port);

  c = clus_lookup(name, L_CREATE /* |L_EXCLUSIVE */);
  if (c == NULL)
    FATAL("cannot create cluster `%s': %m\n", name);

  c->c_interval = cfg_getfloat(cfg, "interval");

  TRACE("added cluster `%s'\n", name);
}

static cfg_opt_t lnet_cfg_opts[] = {
  CFG_STR_LIST("files", NULL, CFGF_NONE),
  CFG_END(),
};

void lnet_cfg(cfg_t *cfg, size_t hint)
{
  const char *name = cfg_title(cfg);
  struct lnet_struct *l;

  l = lnet_lookup(name, L_CREATE, hint);
  if (l == NULL)
    FATAL("cannot create lnet `%s': %m\n", name);

  size_t i, nr_files = cfg_size(cfg, "files");
  for (i = 0; i < nr_files; i++) {
    const char *path = cfg_getnstr(cfg, "files", i);
    if (lnet_read(l, path) < 0)
      FATAL("cannot read lnet file `%s': %m\n", path);
  }

  TRACE("added lnet `%s'\n", name);
}

static cfg_opt_t fs_cfg_opts[] = {
  /* AUTH_CFG_OPTS, */
  BIND_CFG_OPTS,
  CFG_STR("lnet", NULL, CFGF_NONE),
  CFG_STR_LIST("servs", NULL, CFGF_NONE),
  CFG_FLOAT("interval", CLTOP_SERV_INTERVAL, CFGF_NONE),
  CFG_END(),
};

void fs_cfg(EV_P_ cfg_t *cfg, char *addr, char *port)
{
  const char *name = cfg_title(cfg);
  const char *lnet_name = cfg_getstr(cfg, "lnet");
  struct x_node *x;
  double interval;
  struct lnet_struct *l;
  size_t i, nr_servs;

  if (bind_cfg(EV_A_ cfg, &addr, &port) < 0)
    FATAL("fs `%s': invalid bind option\n", name); /* XXX */

  x = x_lookup(X_FS, name, L_CREATE);
  if (x == NULL)
    FATAL("fs `%s': cannot create filesystem: %m\n", name);

  l = lnet_lookup(lnet_name, 0, 0);
  if (l == NULL)
    FATAL("fs `%s': unknown lnet `%s': %m\n",
          name, lnet_name != NULL ? lnet_name : "-");

  interval = cfg_getfloat(cfg, "interval");
  if (interval <= 0)
    FATAL("fs `%s': invalid interval %lf\n", name, interval);

  nr_servs = cfg_size(cfg, "servs");
  if (nr_servs == 0)
    FATAL("fs `%s': no servers given\n", name);

  for (i = 0; i < nr_servs; i++) {
    const char *serv_name = cfg_getnstr(cfg, "servs", i);
    struct serv_node *s;

    s = serv_create(serv_name, x, l);
    if (s == NULL)
      FATAL("fs `%s': cannot create server `%s': %m\n", name, serv_name);

    /* TODO AUTH */

    s->s_interval = interval;
    s->s_offset = (i * interval) / nr_servs;
  }
}

static cfg_opt_t user_domain_cfg_opts[] = {
  /* AUTH_CFG_OPTS, */
  BIND_CFG_OPTS,
  CFG_END(),
};

void user_domain_cfg(EV_P_ cfg_t *cfg, char *addr, char *port)
{
  const char *name = cfg_title(cfg);
  struct user_domain *ud;

  if (bind_cfg(EV_A_ cfg, &addr, &port) < 0)
    FATAL("user domain `%s': invalid bind option\n", name);

  /* TODO AUTH. */

  ud = user_domain_lookup(name, L_CREATE);
  if (ud == NULL)
    FATAL("cannot create user domain `%s': %m\n", name);
}

int main(int argc, char *argv[])
{
  char *bind_addr = CLTOP_BIND_ADDR;
  char *bind_port = CLTOP_BIND_PORT;
  char *conf_path = CLTOP_CONF_PATH;

  cfg_opt_t main_cfg_opts[] = {
    BIND_CFG_OPTS,
    CFG_FLOAT("tick", K_TICK, CFGF_NONE), /* TODO */
    CFG_FLOAT("window", K_WINDOW, CFGF_NONE), /* TODO */
    CFG_INT("nr_hosts_hint", CLTOP_NR_HOSTS_HINT, CFGF_NONE),
    CFG_INT("nr_jobs_hint", CLTOP_NR_JOBS_HINT, CFGF_NONE),
    CFG_SEC("cluster", clus_cfg_opts, CFGF_MULTI|CFGF_TITLE),
    CFG_SEC("lnet", lnet_cfg_opts, CFGF_MULTI|CFGF_TITLE),
    CFG_SEC("fs", fs_cfg_opts, CFGF_MULTI|CFGF_TITLE),
    CFG_SEC("user_domain", user_domain_cfg_opts, CFGF_MULTI|CFGF_TITLE),
    CFG_END()
  };

  cfg_t *main_cfg = cfg_init(main_cfg_opts, 0);
  int cfg_rc = cfg_parse(main_cfg, conf_path);
  if (cfg_rc == CFG_FILE_ERROR) {
    errno = ENOENT;
    FATAL("cannot open `%s': %m\n", conf_path);
  } else if (cfg_rc == CFG_PARSE_ERROR) {
    FATAL("error parsing `%s'\n", conf_path);
  }

  if (bind_cfg(EV_DEFAULT_ main_cfg, &bind_addr, &bind_port) < 0)
    FATAL("%s: invalid bind config\n", conf_path);

  size_t nr_host_hint = cfg_getint(main_cfg, "nr_hosts_hint");
  size_t nr_job_hint = cfg_getint(main_cfg, "nr_jobs_hint");
  size_t nr_clus = cfg_size(main_cfg, "cluster");
  size_t nr_fs = cfg_size(main_cfg, "fs");
  size_t nr_serv = 0;

  size_t i;
  for (i = 0; i < nr_fs; i++)
    nr_serv += cfg_size(cfg_getnsec(main_cfg, "fs", i), "servs");

  x_types[X_HOST].x_nr_hint = nr_host_hint;
  x_types[X_JOB].x_nr_hint = nr_job_hint;
  x_types[X_CLUS].x_nr_hint = nr_clus;
  x_types[X_SERV].x_nr_hint = nr_serv;
  x_types[X_FS].x_nr_hint = nr_fs;

  if (x_types_init() < 0)
    FATAL("cannot initialize x_types: %m\n");

  for (i = 0; i < nr_clus; i++)
    clus_cfg(EV_DEFAULT_
             cfg_getnsec(main_cfg, "cluster", i),
             bind_addr, bind_port);

  size_t nr_lnet = cfg_size(main_cfg, "lnet");
  for (i = 0; i < nr_lnet; i++)
    lnet_cfg(cfg_getnsec(main_cfg, "lnet", i), nr_host_hint);

  for (i = 0; i < nr_fs; i++)
    fs_cfg(EV_DEFAULT_
           cfg_getnsec(main_cfg, "fs", i),
           bind_addr, bind_port);

  size_t nr_domain = cfg_size(main_cfg, "user_domain");
  if (user_init(nr_domain) < 0)
    FATAL("cannot initialize user info: %m\n");

  for (i = 0; i < nr_domain; i++)
    user_domain_cfg(EV_DEFAULT_
                    cfg_getnsec(main_cfg, "user_domain", i),
                    bind_addr, bind_port);

  cfg_free(main_cfg);

  if (screen_init() < 0)
    FATAL("cannot initialize screen: %m\n");

  screen_start(EV_DEFAULT);

  signal(SIGPIPE, SIG_IGN);

  ev_run(EV_DEFAULT_ 0);

  screen_stop(EV_DEFAULT);

  return 0;
}
