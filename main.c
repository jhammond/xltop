#include <stddef.h>
#include <stdlib.h>
#include <malloc.h>
#include <errno.h>
#include <signal.h>
#include <ev.h>
#include "ap_parse.h"
#include "x_botz.h"
#include "confuse.h"
#include "x_node.h"
#include "clus.h"
#include "fs.h"
#include "lnet.h"
#include "serv.h"
#include "screen.h"
#include "xltop.h"
#include "trace.h"

#define CLTOP_CONF_PATH "cltop.conf"
#define CLTOP_BIND_ADDR "0.0.0.0"
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

int bind_cfg(cfg_t *cfg, const char *addr, const char *port)
{
  struct ap_struct ap;
  char *opt;

  opt = cfg_getstr(cfg, "bind");
  if (opt != NULL) {
    if (ap_parse(&ap, opt, addr, port) < 0)
      return -1;
    addr = ap.ap_addr;
    port = ap.ap_port;
  }

  opt = cfg_getstr(cfg, "bind_host");
  if (opt != NULL)
    addr = opt;

  opt = cfg_getstr(cfg, "bind_address");
  if (opt != NULL)
    addr = opt;

  opt = cfg_getstr(cfg, "bind_service");
  if (opt != NULL)
    port = opt;

  opt = cfg_getstr(cfg, "bind_port");
  if (opt != NULL)
    port = opt;

  if (evx_listen_add_name(&x_listen.bl_listen, addr, port, 0) < 0) {
    ERROR("cannot bind to host/address `%s', service/port `%s': %m\n",
          addr, port);
    return -1;
  }

  return 0;
}

static cfg_opt_t clus_cfg_opts[] = {
  /* AUTH_CFG_OPTS, */
  BIND_CFG_OPTS,
  CFG_STR_LIST("domains", NULL, CFGF_NONE),
  CFG_FLOAT("interval", CLTOP_CLUS_INTERVAL, CFGF_NONE),
  CFG_FLOAT("offset", 0, CFGF_NONE),
  CFG_END(),
};

void clus_cfg(EV_P_ cfg_t *cfg, char *addr, char *port)
{
  const char *name = cfg_title(cfg);
  struct clus_node *c;

  if (bind_cfg(cfg, addr, port) < 0)
    FATAL("invalid bind option for cluster `%s'\n", name);

  c = clus_lookup(name, L_CREATE /* |L_EXCLUSIVE */);
  if (c == NULL)
    FATAL("cannot create cluster `%s': %m\n", name);

  c->c_interval = cfg_getfloat(cfg, "interval");
  /* TODO offset. */

  size_t i, nr_domains = cfg_size(cfg, "domains");
  for (i = 0; i < nr_domains; i++) {
    const char *domain = cfg_getnstr(cfg, "domains", i);
    if (clus_add_domain(c, domain) < 0)
      FATAL("cannot add domain `%s' to cluster `%s': %m\n", domain, name);
  }

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

  if (bind_cfg(cfg, addr, port) < 0)
    FATAL("fs `%s': invalid bind option\n", name); /* XXX */

  x = x_lookup(X_FS, name, x_all[1], L_CREATE);
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

int main(int argc, char *argv[])
{
  char *bind_addr = CLTOP_BIND_ADDR;
  char *bind_port = XLTOP_BIND_PORT;
  char *conf_path = CLTOP_CONF_PATH;

  cfg_opt_t main_cfg_opts[] = {
    BIND_CFG_OPTS,
    CFG_FLOAT("tick", K_TICK, CFGF_NONE),
    CFG_FLOAT("window", K_WINDOW, CFGF_NONE),
    CFG_INT("nr_hosts_hint", CLTOP_NR_HOSTS_HINT, CFGF_NONE),
    CFG_INT("nr_jobs_hint", CLTOP_NR_JOBS_HINT, CFGF_NONE),
    CFG_SEC("clus", clus_cfg_opts, CFGF_MULTI|CFGF_TITLE),
    CFG_SEC("lnet", lnet_cfg_opts, CFGF_MULTI|CFGF_TITLE),
    CFG_SEC("fs", fs_cfg_opts, CFGF_MULTI|CFGF_TITLE),
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

  k_tick = cfg_getfloat(main_cfg, "tick");
  if (k_tick <= 0)
    FATAL("%s: tick must be positive\n", conf_path);

  k_window = cfg_getfloat(main_cfg, "window");
  if (k_window <= 0)
    FATAL("%s: window must be positive\n", conf_path);

  size_t nr_host_hint = cfg_getint(main_cfg, "nr_hosts_hint");
  size_t nr_job_hint = cfg_getint(main_cfg, "nr_jobs_hint");
  size_t nr_clus = cfg_size(main_cfg, "clus");
  size_t nr_fs = cfg_size(main_cfg, "fs");
  size_t nr_serv = 0;
  size_t nr_domain = 0;

  size_t i;
  for (i = 0; i < nr_fs; i++)
    nr_serv += cfg_size(cfg_getnsec(main_cfg, "fs", i), "servs");

  for (i = 0; i < nr_clus; i++)
    nr_domain += cfg_size(cfg_getnsec(main_cfg, "clus", i), "domains");

  x_types[X_HOST].x_nr_hint = nr_host_hint;
  x_types[X_JOB].x_nr_hint = nr_job_hint;
  x_types[X_CLUS].x_nr_hint = nr_clus;
  x_types[X_SERV].x_nr_hint = nr_serv;
  x_types[X_FS].x_nr_hint = nr_fs;

  if (x_types_init() < 0)
    FATAL("cannot initialize x_types: %m\n");

  size_t nr_listen_entries = nr_clus + nr_serv + 128; /* XXX */
  if (botz_listen_init(&x_listen, nr_listen_entries) < 0)
    FATAL("%s: cannot initialize listener\n", conf_path);

  x_listen.bl_conn_timeout = 600; /* XXX */

  if (bind_cfg(main_cfg, bind_addr, bind_port) < 0)
    FATAL("%s: invalid bind config\n", conf_path);

  for (i = 0; i < X_NR_TYPES; i++)
    if (x_dir_init(i, NULL) < 0)
      FATAL("cannot initialize type resources: %m\n");

  extern const struct botz_entry_ops top_entry_ops; /* MOVEME */
  if (botz_add(&x_listen, "top", &top_entry_ops, NULL) < 0) {
    ERROR("cannot add listen entry `%s': %m\n", "top");
    return -1;
  }

  if (serv_type_init() < 0)
    FATAL("cannot initialize serv type: %m\n");

  if (clus_type_init(nr_domain) < 0)
    FATAL("cannot initialize default cluster: %m\n");

  if (fs_type_init() < 0)
    FATAL("cannot initialize fs type: %m\n");

  for (i = 0; i < nr_clus; i++)
    clus_cfg(EV_DEFAULT_
             cfg_getnsec(main_cfg, "clus", i),
             bind_addr, bind_port);

  size_t nr_lnet = cfg_size(main_cfg, "lnet");
  for (i = 0; i < nr_lnet; i++)
    lnet_cfg(cfg_getnsec(main_cfg, "lnet", i), nr_host_hint);

  for (i = 0; i < nr_fs; i++)
    fs_cfg(EV_DEFAULT_
           cfg_getnsec(main_cfg, "fs", i),
           bind_addr, bind_port);

  cfg_free(main_cfg);

  if (screen_init() < 0)
    FATAL("cannot initialize screen: %m\n");

  evx_listen_start(EV_DEFAULT_ &x_listen.bl_listen);

  screen_start(EV_DEFAULT);

  signal(SIGPIPE, SIG_IGN);

  ev_run(EV_DEFAULT_ 0);

  screen_stop(EV_DEFAULT);

  return 0;
}
