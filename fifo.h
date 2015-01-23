#ifndef FIFO_H
#define FIFO_H

#define FIFO_CACHE_LINE_SIZE 64

typedef struct _fifo_handle_t {
  struct _fifo_handle_t * next;
  char flag;
  void * data;
} fifo_handle_t __attribute__((aligned(FIFO_CACHE_LINE_SIZE)));

typedef struct {
  fifo_handle_t * P __attribute__((aligned(FIFO_CACHE_LINE_SIZE)));
  fifo_handle_t * C __attribute__((aligned(FIFO_CACHE_LINE_SIZE)));
  fifo_handle_t * H __attribute__((aligned(FIFO_CACHE_LINE_SIZE)));
} fifo_t;

void fifo_put  (fifo_t * fifo, void * data);
void fifo_atake(fifo_t * fifo, fifo_handle_t * node);
int  fifo_abort(fifo_handle_t * node, void ** data);

static inline void fifo_handle_init(fifo_handle_t * node) {
  node->flag = 0;
}

static inline int fifo_test(fifo_handle_t * node, void ** ptr)
{
  __asm__ ("pause": : :"memory");

  int succeed = __atomic_load_n(&node->flag, __ATOMIC_ACQUIRE);

  if (succeed) {
    *ptr = node->data;
  }

  return succeed;
}

static inline void * fifo_take(fifo_t * fifo)
{
  fifo_handle_t node;

  fifo_atake(fifo, &node);

  void * data;
  while (!fifo_test(&node, &data));

  return data;
}

#endif /* end of include guard: FIFO_H */
