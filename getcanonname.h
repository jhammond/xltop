#ifndef _GETCANONNAME_H_
#define _GETCANONNAME_H_

/* If host is NULL return the canonname of the current host. */
int getcanonname(const char *host, char *c_name, size_t c_name_size);

#endif
