#include <stdio.h>
#include <stdlib.h>
#include "fifo.h"

typedef char cache_t[64];

typedef struct _fifo_node_t {
  struct _fifo_node_t * next __attribute__((aligned(64)));
  int64_t index __attribute__((aligned(64)));
  size_t  count __attribute__((aligned(64)));
  cache_t buffer[0];
} node_t;

typedef fifo_handle_t handle_t;

#define fetch_and_add(p, v) __atomic_fetch_add(p, v, __ATOMIC_RELAXED)
#define add_and_fetch(p, v) __atomic_add_fetch(p, v, __ATOMIC_RELAXED)

#define load(ptr) (* (void * volatile *) (ptr))
#define store(ptr, v) (*(ptr) = v)

#define compare_and_swap __sync_bool_compare_and_swap
#define spin_while(cond) while (cond) __asm__ ("pause")
#define INVALID ((void *) -1)

static inline node_t * new_node(int64_t index, size_t size)
{
  size *= 64 / sizeof(void *);
  size += sizeof(node_t) / sizeof(void *);

  node_t * node = calloc(size, sizeof(void *));
  node->next = NULL;
  node->index = index;
  node->count = 0;

  return node;
}

static inline void try_free(node_t * node, node_t * alt, size_t size)
{
  if (alt->index > node->index)
    if (add_and_fetch(&node->count, 1) == size)
      free(node);
}

void fifo_init(fifo_t * fifo, size_t size, size_t width)
{
  fifo->S = size;
  fifo->W = width;
  fifo->P = 0;
  fifo->C = 0;
  fifo->T = new_node(-1, size);
}

void fifo_register(const fifo_t * fifo, handle_t handle)
{
  handle[0] = fifo->T;
  handle[1] = fifo->T;
}

static node_t * update(size_t index, handle_t handle, int i, fifo_t * fifo)
{
  node_t * node = handle[i];

  if (node->index < index) {
    node_t * prev = node;

    while (prev->index < index - 1) {
      spin_while((node = load(&prev->next)) == NULL);
      try_free(prev, handle[1 - i], fifo->W);
      prev = node;
    }

    node = prev->next;

    if (!node) {
      if (compare_and_swap(&fifo->T, prev, INVALID)) {
        node = new_node(index, fifo->S);
        fifo->T = node;
        store(&prev->next, node);
      } else {
        spin_while((node = load(&prev->next)) == NULL);
      }
    }

    try_free(prev, handle[1 - i], fifo->W);
    handle[i] = node;
  }

  return node;
}

void fifo_put(fifo_t * fifo, handle_t handle, void * data)
{
  int64_t i  = fetch_and_add(&fifo->P, 1);
  int64_t ni = i / fifo->S;
  int64_t li = i % fifo->S;

  node_t * node = update(ni, handle, 0, fifo);
  store((void **) &node->buffer[li], data);
}

void * fifo_get(fifo_t * fifo, handle_t handle)
{
  int64_t i  = fetch_and_add(&fifo->C, 1);
  int64_t ni = i / fifo->S;
  int64_t li = i % fifo->S;

  node_t * node = update(ni, handle, 1, fifo);

  /** Wait for data. */
  void * data;
  spin_while((data = load((void **) &node->buffer[li])) == NULL);

  return data;
}

#include "test.h"
#include "bench.h"

static fifo_t fifo;

void init()
{
  n /= nprocs;
  fifo_init(&fifo, 1024, nprocs);
}

typedef struct _local_t {
  fifo_handle_t handle;
  void * val;
} local_t;

void prep(int id, void * args)
{
  local_t * locals = (local_t *) args;

  fifo_register(&fifo, locals->handle);
  locals->val = (void *) (long) id + 1;
}

void enqueue(int id, int i, void * args)
{
  local_t * locals = (local_t *) args;
  fifo_put(&fifo, locals->handle, locals->val);
}

void dequeue(int id, int i, void * args)
{
  local_t * locals = (local_t *) args;
  locals->val = fifo_get(&fifo, locals->handle);
}

int verify() {}

