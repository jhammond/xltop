#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include "string1.h"
#include "trace.h"
#include "hash.h"
#include "x_node.h"
#include "sub.h"
#include "user.h"

void sub_cb(struct sub_node *s, struct k_node *k,
            struct x_node *x0, struct x_node *x1,
            double now, double *d)
{
  printf("%s %s %s, origin %s %s, now %f, rate %12.3f %12.3f %12.3f\n",
         (const char *) s->s_u, k->k_x[0]->x_name, k->k_x[1]->x_name,
         x0->x_name, x1->x_name, now,
         k->k_rate[0], k->k_rate[1], k->k_rate[2]);
}

int main(int argc, char *argv[])
{
  char n0[256], n1[256];
  double now, d[NR_STATS];
  struct x_node *x0, *x1, *j0, *f0;
  struct sub_node *s0, *s1, *s2;

  x_ops_init();

  x0 = x_lookup(X_HOST, "h0", L_CREATE);
  x1 = x_lookup(X_SERV, "s1", L_CREATE);
  j0 = x_lookup(X_JOB, "j0", L_CREATE);
  f0 = x_lookup(X_FS, "f0", L_CREATE);

  x_set_parent(x0, j0);
  x_set_parent(x1, f0);

  s0 = malloc(sizeof(*s0));
  sub_init(s0, x0, x1, &sub_cb, "h0->s1");

  s1 = malloc(sizeof(*s1));
  sub_init(s1, j0, f0, &sub_cb, "j0->f0");

  s2 = malloc(sizeof(*s2));
  sub_init(s2, x_top[0], x_top[1], &sub_cb, "ALL->ALL");

  while (scanf("%255s %255s %lf %lf %lf %lf\n",
               n0, n1, &now, &d[0], &d[1], &d[2]) == 6) {
    x0 = x_lookup(X_HOST, n0, L_CREATE);
    x1 = x_lookup(X_SERV, n1, L_CREATE);

    x_update(x0, x1, now, d);
  }

  return 0;
}
