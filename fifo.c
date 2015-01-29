#include <stdlib.h>
#include <string.h>
#include "fifo.h"

typedef struct _fifo_node_t {
  int64_t index;
  size_t  count;
  struct _fifo_node_t * next __attribute__((aligned(64)));
  void * buffer[0] __attribute__((aligned(64)));
} node_t;

typedef fifo_handle_t handle_t;

#define fetch_and_add(p, v) __atomic_fetch_add(p, v, __ATOMIC_RELAXED)
#define add_and_fetch(p, v) __atomic_add_fetch(p, v, __ATOMIC_RELAXED)
#define compare_and_swap __sync_bool_compare_and_swap
#define load(ptr) (* (void * volatile *) (ptr))
#define spin_while(cond) while (cond) __asm__ ("pause")

static inline node_t * new_node(int64_t index, size_t size, node_t ** in)
{
  node_t * node = in ? *in : NULL;

  if (node) {
    *in = NULL;
    node->count = 0;
    node->next  = NULL;
  } else {
    size = sizeof(node_t) + sizeof(void * [size]);

    posix_memalign((void **) &node, 4096, size);
    memset(node, 0, size);
  }

  node->index = index;
  return node;
}

static inline void try_free(node_t * node, node_t * alt, fifo_t * fifo,
    node_t ** out)
{
  if (alt->index > node->index) {
    if (add_and_fetch(&node->count, 1) == fifo->W) {
      if (*out) {
        free(node);
      } else {
        *out = node;
      }
    }
  }
}

void fifo_init(fifo_t * fifo, size_t size, size_t width)
{
  fifo->S = size;
  fifo->W = width;
  fifo->P = 0;
  fifo->C = 0;
  fifo->T = new_node(-1, size, NULL);
}

void fifo_register(const fifo_t * fifo, handle_t * handle)
{
  handle->P = fifo->T;
  handle->C = fifo->T;
  handle->F = NULL;
}

static node_t * update(int64_t index, node_t ** handle, int i, fifo_t * fifo)
{
  node_t * node = handle[i];

  if (node->index < index) {
    node_t * prev = node;

    while (prev->index < index - 1) {
      node = prev->next;
      try_free(prev, handle[1 - i], fifo, &handle[2]);
      prev = node;
    }

    node = prev->next;

    if (!node) {
      node = new_node(index, fifo->S, &handle[2]);

      if (compare_and_swap(&fifo->T, prev, node)) {
        prev->next = node;
      } else {
        handle[2] = node;
        spin_while((node = load(&prev->next)) == NULL);
      }
    }

    try_free(prev, handle[1 - i], fifo, &handle[2]);
    handle[i] = node;
  }

  return node;
}

void fifo_put(fifo_t * fifo, handle_t * handle, void * data)
{
  int64_t i  = fetch_and_add(&fifo->P, 1);
  int64_t ni = i / fifo->S;
  int64_t li = i % fifo->S;

  node_t * node = update(ni, (node_t **) handle, 0, fifo);
  node->buffer[li] = data;
}

void * fifo_get(fifo_t * fifo, handle_t * handle)
{
  int64_t i  = fetch_and_add(&fifo->C, 1);
  int64_t ni = i / fifo->S;
  int64_t li = i % fifo->S;

  node_t * node = update(ni, (node_t **) handle, 1, fifo);

  /** Wait for data. */
  void * data;
  spin_while((data = load(&node->buffer[li])) == NULL);
  node->buffer[li] = NULL;

  return data;
}

typedef fifo_handle_t thread_local_t;

#include "bench.h"

static fifo_t fifo;

void init(int nprocs)
{
  fifo_init(&fifo, 512, nprocs);
}

void thread_init(int id, void * handle)
{
  fifo_register(&fifo, handle);
}

void enqueue(void * val, void * handle)
{
  fifo_put(&fifo, handle, val);
}

void * dequeue(void * handle)
{
  return fifo_get(&fifo, handle);
}

