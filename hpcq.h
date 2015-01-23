#ifndef HPCQ_H
#define HPCQ_H

#define HPCQ_CACHE_LINE_SIZE 64

typedef struct _hpcq_handle_t {
  struct _hpcq_handle_t * next;
  void * data;
  char padding[HPCQ_CACHE_LINE_SIZE - sizeof(void *) * 2];
} hpcq_handle_t;

typedef struct {
  hpcq_handle_t * P __attribute__((aligned(HPCQ_CACHE_LINE_SIZE)));
  hpcq_handle_t * C __attribute__((aligned(HPCQ_CACHE_LINE_SIZE)));
  hpcq_handle_t * H __attribute__((aligned(HPCQ_CACHE_LINE_SIZE)));
} hpcq_t;

void hpcq_put  (hpcq_t * hpcq, hpcq_handle_t * node);
void hpcq_atake(hpcq_t * hpcq, hpcq_handle_t * node);

static inline void * hpcq_test(hpcq_handle_t * node)
{
  __asm__ ("pause": : :"memory");
  return node->data;
}

static inline void * hpcq_take(hpcq_t * hpcq)
{
  hpcq_handle_t node __attribute__((aligned(HPCQ_CACHE_LINE_SIZE)));

  hpcq_atake(hpcq, &node);

  void * data;
  while (!(data = hpcq_test(&node)));

  return data;
}

#endif /* end of include guard: HPCQ_H */
