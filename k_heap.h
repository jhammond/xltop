#ifndef _K_HEAP_H_
#define _K_HEAP_H_

struct k_heap {
  struct k_node **h_k;
  size_t h_count;
  size_t h_limit;
};

typedef int (k_heap_cmp_t)(struct k_heap *, struct k_node *, struct k_node *);

int k_heap_init(struct k_heap *h, size_t limit);
void k_heap_destroy(struct k_heap *h);
void k_heap_top(struct k_heap *h, struct x_node *x0, size_t d0,
                struct x_node *x1, size_t d1, k_heap_cmp_t *cmp);

void k_heap_order(struct k_heap *h, k_heap_cmp_t *cmp);

#endif
