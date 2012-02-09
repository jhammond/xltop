#include <stdlib.h>
#include <arpa/inet.h>
#include "string1.h"
#include "trace.h"
#include "ap_parse.h"

static inline int ap_is_port(const char *s)
{
  char *end;

  if (s == NULL || *s == 0)
    return 0;

  strtol(s, &end, 10);

  return *end == 0;
}

int ap_parse(char *ap_spec, const char **a, const char **p)
{
  char *s, *s1, *s2;
  struct in6_addr in6;
  int n;

  /*
     0 NULL
     1 ""
     2 "9901"
     3 "192.0.43.10"
     4 "192.0.43.10 9901"
     5 "192.0.43.10:9901"
     6 "example.com"
     7 "example.com 9901"
     8 "example.com:9901"
     9 "2001:500:88:200::10"
    10 "2001:500:88:200::10 9901"
    11 "[2001:500:88:200::10]"
    12 "[2001:db8::a00:20ff:fea7:ccea] 9901"
    13 "[2001:db8::a00:20ff:fea7:ccea]:9901"

    NOTE Also accepts [host], [ipv4], [ipv4]:port, ...
  */

  if (ap_spec == NULL)
    return 0; /* 0 */

  TRACE("ap_spec `%s'\n", ap_spec);

  for (s = ap_spec; *s != 0; s++)
    if (*s == '[' || *s == ']')
      *s = ' '; /* { 11, 12, 13 } => { 9, 10 } */

  s = ap_spec;
  n = split(&s, &s1, &s2, (char **) NULL);
  if (n == 0)
    return 0; /* 1 */

  if (s != NULL)
    return -1;

  if (n == 2) {
    if (*s2 == ':')
      s2++;

    if (*s2 == 0)
      return -1; /* Disallow "example.com:". */

    *a = s1;
    *p = s2;
    return 0; /* 4, 7, 10 */
  }

  s = s1; /* s in now ap_spec, but trimmed of space. */
  if (ap_is_port(s)) {
    *p = s;
    return 0; /* 2 */
  }

  if (inet_pton(AF_INET6, s, &in6)) {
    *a = s;
    return 0; /* 9 */
  }

  s1 = strsep(&s, ":");
  if (s == NULL) { /* No ':' found. */
    *a = s1;
    return 0; /* 3, 6 */
  }

  if (*s == 0)
    return -1; /* Disallow "example.com:". */

  *a = s1;
  *p = s;
  return 0; /* 5, 8 */
}
