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

static inline void * fifo_test(fifo_handle_t * node)
{
  __asm__ ("pause": : :"memory");
  return node->data;
}

static inline void * fifo_take(fifo_t * fifo)
{
  fifo_handle_t node;

  fifo_atake(fifo, &node);

  void * data;
  while (!(data = fifo_test(&node)));

  return data;
}

#endif /* end of include guard: FIFO_H */
