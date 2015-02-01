#include <stdlib.h>
#include <string.h>
#include "fifo.h"

typedef struct _fifo_node_t {
  size_t index;
  size_t count;
  struct _fifo_node_t * volatile next __attribute__((aligned(64)));
  void * volatile buffer[0] __attribute__((aligned(64)));
} node_t;

typedef fifo_handle_t handle_t;

#define fetch_and_add(p, v) __atomic_fetch_add(p, v, __ATOMIC_RELAXED)
#define add_and_fetch(p, v) __atomic_add_fetch(p, v, __ATOMIC_RELAXED)
#define compare_and_swap __sync_val_compare_and_swap
#define spin_while(cond) while (cond) __asm__ ("pause")

static inline node_t * new_node(size_t index, size_t size)
{
  size = sizeof(node_t) + sizeof(void * [size]);

  node_t * node;

  posix_memalign((void **) &node, 4096, size);
  memset(node, 0, size);

  node->index = index;
  return node;
}

static inline void try_free(node_t * node, size_t index, size_t nprocs)
{
  if (index > node->index) {
    if (add_and_fetch(&node->count, 1) == nprocs) {
      free(node);
    }
  }
}

void fifo_init(fifo_t * fifo, size_t size, size_t width)
{
  fifo->S = size;
  fifo->W = width;
  fifo->P = 0;
  fifo->C = 0;
  fifo->T = new_node(0, size);
}

void fifo_register(const fifo_t * fifo, handle_t * handle)
{
  handle->P = fifo->T;
  handle->C = fifo->T;
}

void fifo_unregister(const fifo_t * fifo, handle_t * handle)
{
  node_t * curr, * next;

  curr = handle->P->index > handle->C->index ? handle->C : handle->P;

  do {
    next = curr->next;
    try_free(curr, curr->index + 1, fifo->W);
  } while ((curr = next));
}

static node_t * update(size_t index, node_t ** handle, int i, fifo_t * fifo)
{
  node_t * node = handle[i];

  if (node->index < index) {
    node_t * prev = node;

    while (prev->index < index - 1) {
      node = prev->next;
      try_free(prev, handle[1 - i]->index, fifo->W);
      prev = node;
    }

    node = prev->next;

    if (!node) {
      node = new_node(index, fifo->S);
      node_t * next;

      if ((next = compare_and_swap(&prev->next, NULL, node))) {
        free(node);
        node = next;
      }
    }

    try_free(prev, handle[1 - i]->index, fifo->W);
    handle[i] = node;
  }

  return node;
}

void fifo_put(fifo_t * fifo, handle_t * handle, void * data)
{
  size_t i  = fetch_and_add(&fifo->P, 1);
  size_t ni = i / fifo->S;
  size_t li = i % fifo->S;

  node_t * node = update(ni, (node_t **) handle, 0, fifo);
  node->buffer[li] = data;
}

void * fifo_get(fifo_t * fifo, handle_t * handle)
{
  size_t i  = fetch_and_add(&fifo->C, 1);
  size_t ni = i / fifo->S;
  size_t li = i % fifo->S;

  node_t * node = update(ni, (node_t **) handle, 1, fifo);

  /** Wait for data. */
  void * data;
  spin_while((data = node->buffer[li]) == NULL);
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

