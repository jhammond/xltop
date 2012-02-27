#ifndef _XLTOP_H_
#define _XLTOP_H_
#include <sys/sysinfo.h>
#include "string1.h"

/* Common defines for xltopd, servd, ... */

#define STAT_WR_BYTES 0
#define STAT_RD_BYTES 1
#define STAT_NR_REQS  2
#define NR_STATS 3 /* MOVEME */

#define PRI_STATS_FMT(s) s" "s" "s
#define PRI_STATS_ARG(v) (v)[0], (v)[1], (v)[2]

struct serv_status {
  double ss_time, ss_uptime;
  double ss_load[3];
  size_t ss_total_ram, ss_free_ram, ss_shared_ram, ss_buffer_ram;
  size_t ss_total_swap, ss_free_swap;
  size_t ss_nr_tasks;
  size_t ss_nr_tgts, ss_nr_nids;
};

#define PRI_SERV_STATUS_FMT \
  "%f %f %.2f %.2f %.2f %zu %zu %zu %zu %zu %zu %zu %zu %zu"

#define PRI_SERV_STATUS_ARG(s) \
  (s).ss_time, (s).ss_uptime, (s).ss_load[0], (s).ss_load[1], (s).ss_load[2], \
  (s).ss_total_ram, (s).ss_free_ram, (s).ss_shared_ram, (s).ss_buffer_ram, \
  (s).ss_total_swap, (s).ss_free_swap, (s).ss_nr_tasks, (s).ss_nr_tgts, \
  (s).ss_nr_nids

#define SCN_SERV_STATUS_FMT \
  "%lf %lf %lf %lf %lf %zu %zu %zu %zu %zu %zu %zu %zu %zu"

#define SCN_SERV_STATUS_ARG(s) \
  &(s).ss_time, &(s).ss_uptime, &(s).ss_load[0], &(s).ss_load[1], \
  &(s).ss_load[2], &(s).ss_total_ram, &(s).ss_free_ram, &(s).ss_shared_ram, \
  &(s).ss_buffer_ram, &(s).ss_total_swap, &(s).ss_free_swap, &(s).ss_nr_tasks, \
  &(s).ss_nr_tgts, &(s).ss_nr_nids

#define NR_SCN_SERV_STATUS_ARGS 14

#endif
