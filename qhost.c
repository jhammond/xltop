#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include "string1.h"
#include "trace.h"
#define IDLE_JOBID "IDLE" /* FIXME MOVEME */

/* This must stay aligned with clus_msg_cb() in clus.c.
   Our output:
     HOST.DOMAIN JOBID@CLUSTER OWNER TITLE START_EPOCH
   Or if host is idle:
     HOST.DOMAIN
*/

/* `qhost -j' output.
 *  2 header lines:

HOSTNAME                ARCH         NCPU  LOAD  MEMTOT  MEMUSE  SWAPTO  SWAPUS
-------------------------------------------------------------------------------

 * N bogus host lines:

global                  -               -     -       -       -       -       -
c99-102                 -               -     -       -       -       -       -
c99-103                 -               -     -       -       -       -       -
c99-104                 -               -     -       -       -       -       -
...

 * Real host lines:

i101-101                lx24-amd64     16 15.98   31.4G    3.4G     0.0     0.0
   job-ID  prior   name       user         state submit/start at     queue      master ja-task-ID
   ----------------------------------------------------------------------------------------------
   2320964 0.02563 STDIN      ahkulahl     r     01/24/2012 14:01:08 long@i101- SLAVE
                                                                     long@i101- SLAVE
                                                                     long@i101- SLAVE
   ...

 * Idle host lines:
i110-412                lx24-amd64     16  7.04   31.4G    1.2G     0.0     0.0
i111-101                lx24-amd64     16 16.00   31.4G    3.0G     0.0     0.0
   2320242 0.02579 STDIN      ahkulahl     r     01/24/2012 04:43:37 long@i111- SLAVE
   ...
*/

/* dd is .DOMAIN */

int qhost_j_filter(FILE *in, FILE *out, const char *dd, const char *at)
{
  char *host_line = NULL, *job_line = NULL, *tmp_line;
  size_t host_line_size = 0, job_line_size = 0, tmp_line_size;

  while (getline(&host_line, &host_line_size, in) >= 0) {
    char *s0, *host, *arch, *ncpu;
    int found = 0;

    if (!isalpha(*host_line))
      continue;

    s0 = host_line;
    if (split(&s0, &host, &arch, &ncpu, NULL) != 3)
      continue;

    if (atoi(ncpu) == 0)
      continue;

    /* OK, looks like a real host. */
    while (getline(&job_line, &job_line_size, in) >= 0) {
      char *s1, *jobid, *pri, *owner, *title, *r;
      struct tm st_tm;

      /* But does it have a real job? */
      s1 = job_line;
      if (isalpha(*s1)) {
        /* Done with current host, job_line is actually the next host. */
        if (!found)
          fprintf(out, "%s%s %s%s\n", host, dd, IDLE_JOBID, at);
        break;
      }

      if (split(&s1, &jobid, &pri, &title, &owner, &r, NULL) != 5 || s1 == NULL)
        continue;

      if (strcmp(r, "r") != 0)
        continue;

      if (strptime(s1, "%m/%d/%Y %T", &st_tm) == NULL)
        continue;

      /* OK seems legit. */
      found = 1;
      fprintf(out, "%s%s %s%s %s %s %lld\n", host, dd, jobid, at, owner, title,
              (long long) mktime(&st_tm));
    }

    tmp_line = host_line;
    tmp_line_size = host_line_size;
    host_line = job_line;
    host_line_size = job_line_size;
    job_line = tmp_line;
    job_line_size = tmp_line_size;
  }

  free(host_line);
  free(job_line);

  if (fflush(out) < 0)
    return -1;

  return 0;
}

int main(int argc, char *argv[])
{
  /* PATH=$PATH:XXX */
  /* SGE_CELL=default */
  /* SGE_EXECD_PORT=537 */
  /* SGE_QMASTER_PORT=536 */
  /* SGE_ROOT=XXX */
  /* SGE_CLUSTER_NAME=XXX */

  /* XXX Cluster name != SGE_CLUSTER_NAME */

  const char *dd = ".ranger.tacc.utexas.edu";
  const char *at = "@ranger.tacc.utexas.edu";

  FILE *in = stdin, *out = stdout;

  /* TODO setvbuf() */

#if 0
  const char *cluster_name = NULL;
  if (cluster_name == NULL)
    cluster_name = getenv("SGE_CLUSTER_NAME");

  if (cluster_name == NULL)
    FATAL("cannot determine SGE cluster name\n");
#endif

  if (qhost_j_filter(in, out, dd, at) < 0)
    return 1;

  return 0;
}
