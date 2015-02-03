#include <stdlib.h>
#include <string.h>
#include "fifo.h"

typedef union {
  void * volatile data;
  char padding[64];
} cache_t;

typedef struct _fifo_node_t {
  size_t count;
  struct _fifo_node_t * volatile next __attribute__((aligned(64)));
  cache_t buffer[0] __attribute__((aligned(64)));
  /*void * volatile buffer[0] __attribute__((aligned(64)));*/
} node_t;

typedef fifo_handle_t handle_t;

#define fetch_and_add(p, v) __atomic_fetch_add(p, v, __ATOMIC_RELAXED)
#define add_and_fetch(p, v) __atomic_add_fetch(p, v, __ATOMIC_RELAXED)
#define compare_and_swap __sync_val_compare_and_swap
#define spin_while(cond) while (cond) __asm__ ("pause")

static inline node_t * new_node(size_t size)
{
  size = sizeof(node_t) + sizeof(cache_t [size]);

  node_t * node;

  posix_memalign((void **) &node, 4096, size);
  memset(node, 0, size);

  return node;
}

static inline void try_free(node_t * node, size_t nprocs)
{
  if (add_and_fetch(&node->count, 1) == nprocs) {
    free(node);
  }
}

void fifo_init(fifo_t * fifo, size_t size, size_t width)
{
  fifo->S = size;
  fifo->W = width;
  fifo->P = 0;
  fifo->C = 0;
  fifo->T = new_node(size);
}

void fifo_register(const fifo_t * fifo, handle_t * handle)
{
  handle->P.index = 0;
  handle->P.node  = fifo->T;
  handle->C.index = 0;
  handle->C.node  = fifo->T;
}

void fifo_unregister(const fifo_t * fifo, handle_t * handle)
{
  node_t * curr, * next;

  curr = handle->P.index > handle->C.index ? handle->C.node : handle->P.node;

  do {
    next = curr->next;
    try_free(curr, fifo->W);
  } while ((curr = next));
}

static node_t * update(node_t * node, size_t to, size_t from, size_t other,
    fifo_t * fifo)
{
  size_t i;
  node_t * prev;
  node_t * next = NULL;

  for (i = from; i < to; ++i) {
    prev = node;
    node = prev->next;

    if (!node) {
      if (!next) {
        next = new_node(fifo->S);
      }

      if (NULL == (node = compare_and_swap(&prev->next, NULL, next))) {
        node = next;
        next = NULL;
      }
    }

    if (i < other) try_free(prev, fifo->W);
  }

  if (next) free(next);
  return node;
}

void fifo_put(fifo_t * fifo, handle_t * handle, void * data)
{
  size_t i  = fetch_and_add(&fifo->P, 1);
  size_t ni = i / fifo->S;
  size_t li = i % fifo->S;

  node_t * node = handle->P.node;
  size_t  index = handle->P.index;

  if (index != ni) {
    node = handle->P.node = update(node, ni, index, handle->C.index, fifo);
    handle->P.index = ni;
  }

  node->buffer[li].data = data;
}

void * fifo_get(fifo_t * fifo, handle_t * handle)
{
  size_t i  = fetch_and_add(&fifo->C, 1);
  size_t ni = i / fifo->S;
  size_t li = i % fifo->S;

  node_t * node = handle->C.node;
  size_t  index = handle->C.index;

  if (index != ni) {
    node = handle->C.node = update(node, ni, index, handle->P.index, fifo);
    handle->C.index = ni;
  }

  /** Wait for data. */
  void * data;
  spin_while((data = node->buffer[li].data) == NULL);
  node->buffer[li].data = NULL;

  return data;
}

typedef fifo_handle_t thread_local_t;

#include "bench.h"

static fifo_t fifo;

void init(int nprocs)
{
  fifo_init(&fifo, 510, nprocs);
}

void thread_init(int id, void * handle)
{
  fifo_register(&fifo, handle);
}

void thread_exit(int id, void * handle)
{
  fifo_unregister(&fifo, handle);
}

void enqueue(void * val, void * handle)
{
  fifo_put(&fifo, handle, val);
}

void * dequeue(void * handle)
{
  return fifo_get(&fifo, handle);
}

