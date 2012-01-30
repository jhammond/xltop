#include <stdlib.h>
#include <malloc.h>
#include "string1.h"
#include "trace.h"
#include "hash.h"

#define HASH_SHIFT_MIN 4
#define HASH_SHIFT_MAX (8 * (sizeof(size_t) - 1))

int hash_table_init(struct hash_table *ht, size_t hint)
{
  size_t shift = HASH_SHIFT_MIN, len, i;

  memset(ht, 0, sizeof(*ht));

  len = ((size_t) 1) << shift;
  while (len < 2 * hint && shift < HASH_SHIFT_MAX) {
    shift++;
    len <<= 1;
  }

  ht->ht_table = malloc(len * sizeof(ht->ht_table[0]));
  if (ht->ht_table == NULL)
    return -1;

  for (i = 0; i < len; i++)
    INIT_HLIST_HEAD(&ht->ht_table[i]);

  ht->ht_shift = shift;
  ht->ht_mask = len - 1;

  TRACE("hint %zu, len %zu, shift %zu, mask %zx\n",
        hint, len, ht->ht_shift, ht->ht_mask);

  return 0;
}

size_t str_hash(const char *s, size_t nr_bits)
{
  size_t c, h = 0;

  for (; *s != 0; s++) {
    c = *(unsigned char *) s;
    h = (h + (c << 8) + c) * 11;
  }

  return h;
}

char *str_table_lookup(struct hash_table *ht, const char *key,
                       struct hlist_head **head_ref, size_t str_off)
{
  size_t hash = str_hash(key, ht->ht_shift);
  struct hlist_head *head = ht->ht_table + (hash & ht->ht_mask);
  struct hlist_node *node;

  TRACE("hash %zx, i %8zx, key `%s'\n", hash, hash & ht->ht_mask, key);

  if (head_ref != NULL)
    *head_ref = head;

  hlist_for_each(node, head) {
    char *str = ((char *) node) + str_off;
    if (strcmp(key, str) == 0)
        return str;
  }

  return NULL;
}
