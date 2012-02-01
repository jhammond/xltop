#ifndef _MEMRAND_H_
#define _MEMRAND_H_

static inline int memrand(void *mem, size_t size)
{
  /* TODO */
  memset(mem, 7, size);

  return 0;
}

#endif
