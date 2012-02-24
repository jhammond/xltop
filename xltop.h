#ifndef _XLTOP_H_
#define _XLTOP_H_
#include <sys/sysinfo.h>
#include "string1.h"

/* Common defines for xltopd, servd, ... */

#define PRI_SYSINFO_FMT \
  "%ld %lu %lu %lu %lu %lu %lu %lu %lu %lu %hu %lu %lu %u"

#define PRI_SYSINFO_ARG(i)                              \
  (i).uptime, (i).loads[0], (i).loads[1], (i).loads[2], \
  (i).totalram, (i).freeram, (i).sharedram, (i).bufferram, \
  (i).totalswap, (i).freeswap, (i).procs, \
  (i).totalhigh, (i).freehigh, (i).mem_unit

#define SCN_SYSINFO_FMT PRI_SYSINFO_FMT

#define SCN_SYSINFO_ARG(i) \
  &(i).uptime, &(i).loads[0], &(i).loads[1], &(i).loads[2], \
  &(i).totalram, &(i).freeram, &(i).sharedram, &(i).bufferram, \
  &(i).totalswap, &(i).freeswap, &(i).procs, \
  &(i).totalhigh, &(i).freehigh, &(i).mem_unit

struct serv_info {
  struct sysinfo si_sysinfo;
  double si_time;
  size_t si_nr_tgts;
  size_t si_nr_nids;
};

#define PRI_SERV_INFO_FMT PRI_SYSINFO_FMT" %zu %zu"

#define PRI_SERV_INFO_ARG(i) \
  PRI_SYSINFO_ARG((i).si_sysinfo), (i).si_nr_tgts, (i).si_nr_nids

#define SCN_SERV_INFO_FMT PRI_SERV_INFO_FMT

#define SCN_SERV_INFO_ARG(i) \
  SCN_SYSINFO_ARG((i).si_sysinfo), &((i).si_nr_tgts), &((i).si_nr_nids)

#endif
