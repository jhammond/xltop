#ifndef _TRACE_H_
#define _TRACE_H_
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#if DEBUG
#define TRACE(fmt,args...) do {                    \
    int _saved_errno = errno;                      \
    fprintf(stderr, "%s:%d: "fmt, __func__, __LINE__, ##args);   \
    errno = _saved_errno;                          \
  } while (0)
#else
static inline void TRACE(const char *fmt, ...) { } /* attribute() */
#endif

#define ERROR(fmt,args...) do {                                         \
    int _saved_errno = errno;                                           \
    fprintf(stderr, "%s: "fmt, program_invocation_short_name, ##args);  \
    errno = _saved_errno;                                               \
  } while (0)

#define FATAL(fmt,args...) do { \
    ERROR(fmt, ##args);         \
    exit(1);                    \
  } while (0)

#define ASSERT(c) do { \
  if (!(c)) \
    FATAL("failed assertion `%s'\n", #c); \
  } while (0)

#define OOM() FATAL("out of memory\n");

#endif
