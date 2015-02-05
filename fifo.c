#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fifo.h"

typedef union {
  void * volatile data;
  char padding[64];
} cache_t;

typedef struct _fifo_node_t {
  struct {
    struct _fifo_node_t * next;
    char volatile flag;
  } retired __attribute__((aligned(64)));
  struct _fifo_node_t * volatile next __attribute__((aligned(64)));
  size_t id;
  cache_t buffer[0] __attribute__((aligned(64)));
} node_t;

typedef struct _fifo_pair_t pair_t;
typedef fifo_handle_t handle_t;

#define fetch_and_add(p, v) __atomic_fetch_add(p, v, __ATOMIC_RELAXED)
#define compare_and_swap __sync_val_compare_and_swap
#define test_and_set(p) __atomic_test_and_set(p, __ATOMIC_RELAXED)
#define acquire_fence() __atomic_thread_fence(__ATOMIC_ACQUIRE)
#define release_fence() __atomic_thread_fence(__ATOMIC_RELEASE)
#define spin_while(cond) while (cond) __asm__ ("pause")

static inline node_t * new_node(size_t id, size_t size)
{
  size = sizeof(node_t) + sizeof(cache_t [size]);

  node_t * node;

  posix_memalign((void **) &node, 4096, size);
  memset(node, 0, size);

  node->id = id;
  return node;
}

static inline int push(handle_t * list, node_t * node)
{
  if (list->tail == NULL) {
    list->head = node;
    list->tail = node;
  } else {
    list->tail->retired.next = node;
    list->tail = node;
  }

  return ++list->count;
}

static inline void clean(handle_t * list, size_t hazard)
{
  node_t * curr = list->head;

  while ((curr = list->head)) {
    if (curr->id < hazard) {
      list->head = curr->retired.next;
      free(curr);
    } else {
      return;
    }
  }

  if (list->head == NULL) {
    list->tail = NULL;
  }
}

static inline size_t scan(handle_t * plist, size_t lowest)
{
  size_t hazard = -1;

  handle_t * p;

  for (p = plist; hazard > lowest && p != NULL; p = p->next) {
    if (p->hazard < hazard) {
      hazard = p->hazard;
    }
  }

  return hazard;
}

static inline void try_free(node_t * node, handle_t * handle, fifo_t * fifo)
{
  if (test_and_set(&node->retired.flag)) {
    int count = push(handle, node);

    if (count >= 1 * fifo->W) {
      size_t lowest = handle->head->id;
      size_t hazard = scan(fifo->plist, lowest);

      if (hazard > lowest) {
        clean(handle, hazard);
      }
    }
  }
}

static inline node_t * update(node_t * node, size_t to, size_t size)
{
  size_t i;
  node_t * prev;
  node_t * next = NULL;

  for (i = node->id; i < to; ++i) {
    prev = node;
    node = prev->next;

    if (!node) {
      if (!next) {
        next = new_node(i + 1, size);
      }

      if (NULL == (node = compare_and_swap(&prev->next, NULL, next))) {
        node = next;
        next = NULL;
      }
    }
  }

  if (next) free(next);
  return node;
}

static inline void * volatile * acquire(pair_t * pair,
    fifo_t * fifo, handle_t * handle)
{
  node_t * node = pair->node;
  size_t id = node->id;

  handle->hazard = id;
  acquire_fence();

  size_t i  = fetch_and_add(&pair->index, 1);
  size_t ni = i / fifo->S;
  size_t li = i % fifo->S;

  if (id != ni) {
    node_t * prev = node;
    node = update(prev, ni, fifo->S);

    if (prev == compare_and_swap(&pair->node, prev, node)) {
      try_free(prev, handle, fifo);
    }

    handle->hazard = id;
    acquire_fence();
  }

  return &node->buffer[li].data;
}

static inline void release(handle_t * handle)
{
  handle->hazard = -1;
  release_fence();
}

void fifo_put(fifo_t * fifo, handle_t * handle, void * data)
{
  void * volatile * ptr = acquire(&fifo->P, fifo, handle);

  *ptr = data;

  release(handle);
}

void * fifo_get(fifo_t * fifo, handle_t * handle)
{
  void * volatile * ptr = acquire(&fifo->C, fifo, handle);

  /** Wait for data. */
  void * data;
  spin_while((data = *ptr) == NULL);

  release(handle);
  return data;
}

void fifo_init(fifo_t * fifo, size_t size, size_t width)
{
  fifo->S = size;
  fifo->W = width;

  node_t * node = new_node(0, size);

  fifo->P.index = 0;
  fifo->P.node  = node;
  fifo->C.index = 0;
  fifo->C.node  = node;

  fifo->plist = NULL;
}

void fifo_register(fifo_t * fifo, handle_t * me)
{
  me->head = NULL;
  me->tail = NULL;
  me->hazard = -1;
  me->count  =  0;

  handle_t * curr = fifo->plist;

  do {
    me->next = curr;
  } while (me->next != (curr = compare_and_swap(&fifo->plist, curr, me)));
}

void fifo_unregister(fifo_t * fifo, handle_t * handle)
{
  while (handle->head) {
    size_t lowest = handle->head->id;
    size_t hazard = scan(fifo->plist, lowest);

    if (hazard > lowest) {
      clean(handle, hazard);
    }
  }
}

#ifdef BENCHMARK

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

#endif
