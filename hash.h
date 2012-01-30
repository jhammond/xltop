#ifndef _HASH_H_
#define _HASH_H_
#include <stddef.h>
#include "list.h"

struct hash_table {
  struct hlist_head *ht_table;
  size_t ht_shift, ht_mask;
};

int hash_table_init(struct hash_table *ht, size_t hint);

size_t str_hash(const char *s, size_t nr_bits);

char *str_table_lookup(struct hash_table *ht, const char *key,
                       struct hlist_head **head_ref, size_t str_off);

#define str_table_lookup_entry(ht, key, head_ref, type, m_node, m_str)  \
  ({                                                                    \
    char *_str = str_table_lookup((ht), (key), (head_ref),              \
                                  offsetof(type, m_str) - offsetof(type, m_node)); \
    _str != NULL ? container_of(_str, type, m_str[0]) : NULL;           \
  })

static inline size_t pair_hash(size_t h0, size_t h1, size_t nr_bits)
{
  size_t GOLDEN_RATIO_PRIME = 0x9e37fffffffc0001UL;

  h0 += (h1 ^ GOLDEN_RATIO_PRIME) / 128;
  h0 = h0 ^ ((h0 ^ GOLDEN_RATIO_PRIME) >> nr_bits);
  return h0;
}

#endif
