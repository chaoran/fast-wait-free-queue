#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "fifo.h"

typedef union {
  void * volatile data;
  char padding[64];
} cache_t;

typedef struct _fifo_node_t {
  struct _fifo_node_t * volatile next __attribute__((aligned(64)));
  size_t id;
  cache_t buffer[0] __attribute__((aligned(64)));
} node_t;

typedef fifo_handle_t handle_t;
typedef fifo_request_t request_t;

#define fetch_and_add(p, v) __atomic_fetch_add(p, v, __ATOMIC_RELAXED)
#define compare_and_swap __sync_val_compare_and_swap
#define test_and_set(p) __atomic_test_and_set(p, __ATOMIC_RELAXED)
#define store(p, v) __atomic_store_n(p, v, __ATOMIC_RELEASE)
#define load(p) __atomic_load_n(p, __ATOMIC_ACQUIRE)
#define spin_while(cond) while (cond) __asm__ ("pause")
#define fence() __asm__ ( "mfence" : : : "memory" );
#define release_fence() __atomic_thread_fence(__ATOMIC_RELEASE)
#define lock(p) spin_while(__atomic_test_and_set(p, __ATOMIC_ACQUIRE))
#define unlock(p) __atomic_clear(p, __ATOMIC_RELEASE)

#define ENQ (0)
#define DEQ (1)
#define ALT(i) (1 - i)

static inline
node_t * new_node(size_t id, size_t size)
{
  size = sizeof(node_t) + sizeof(cache_t [size]);

  node_t * node;

  posix_memalign((void **) &node, 4096, size);
  memset(node, 0, size);

  node->id = id;
  return node;
}

static inline
node_t * cleanup(handle_t * plist, handle_t * rlist, node_t * from, node_t * to)
{
  size_t bar = to->id;
  size_t min = from->id;

  /** Scan plist to find the lowest bar. */
  handle_t * p;

  for (p = plist; p != NULL; p = p->next) {
    int i;
    for (i = 0; i < 2; ++i) {
      node_t * node = p->node[i];

      if (node->id < bar && p->hazard == NULL) {
        node_t * prev = compare_and_swap(&p->node[i], node, to);
        release_fence();
        node_t * curr = load(&p->hazard);

        if (curr) {
          node = curr;
        } else {
          if (prev == node) {
            node = to;
          } else {
            node = prev;
          }
        }
      }

      if (node->id < bar) {
        bar = node->id;
        to = node;

        if (bar <= min) return to;
      }
    }
  }

  while (from != to) {
    node_t * next = from->next;
    free(from);
    from = next;
  }

  return to;
}

static inline
node_t * update(node_t * node, size_t to, size_t size)
{
  size_t i;
  node_t * prev;
  node_t * next = NULL;

  for (i = node->id; i < to; ++i) {
    prev = node;
    node = prev->next;

    if (!node) {
      if (!next) next = new_node(i + 1, size);

      if (NULL == (node = compare_and_swap(&prev->next, NULL, next))) {
        node = next;
        next = NULL;
      }
    }
  }

  if (next) free(next);
  return node;
}

static inline
void * volatile * acquire(fifo_t * fifo, handle_t * handle, int op)
{
  node_t * node;
  node_t * curr = handle->node[op];

  do {
    node = curr;
    store(&handle->hazard, node);
    fence();
    curr = load(&handle->node[op]);
  } while (node != curr);

  size_t s  = fifo->S;
  size_t i  = fetch_and_add(&fifo->tail[op].index, 1);
  size_t ni = i / s;
  size_t li = i % s;

  if (node->id != ni) {
    node = update(node, ni, s);

    /**
     * @note
     * This is a harmless data race. Even if we overwrite the local
     * pointer set by a CAS, we are still safe. Because the other thread
     * will see the hazard pointer and avoid touching any node that is
     * newer than the hazard node.
     */
    handle->node[op] = node;
  }

  return &node->buffer[li].data;
}

static inline
void release(fifo_t * fifo, handle_t * handle)
{
  const int threshold = 1 * fifo->W;

  node_t * node = handle->node[0]->id < handle->node[1]->id ?
    handle->node[0] : handle->node[1];

  /** Do nothing if we haven't reach threshold. */
  if (node->id - fifo->head.index > threshold) {
    node_t * head = fifo->head.node;

    if (head && head == compare_and_swap(&fifo->head.node, head, NULL)) {
      head = cleanup(fifo->plist, handle, head, node);

      fifo->head.node = head;
      store(&fifo->head.index, head->id);
    }
  }

  store(&handle->hazard, NULL);
}

void * fifo_test(fifo_t * fifo, handle_t * handle)
{
  void * val = *handle->ptr;

  if (val) {
    release(fifo, handle);
    handle->ptr = NULL;
  }

  return val;
}

void fifo_put(fifo_t * fifo, handle_t * handle, void * data)
{
  void * volatile * ptr = acquire(fifo, handle, ENQ);
  *ptr = data;
  release(fifo, handle);
}

void fifo_aget(fifo_t * fifo, handle_t * handle)
{
  handle->ptr = acquire(fifo, handle, DEQ);
}

void * fifo_get(fifo_t * fifo, handle_t * handle)
{
  fifo_aget(fifo, handle);

  void * val;
  spin_while((val = fifo_test(fifo, handle)) == NULL);

  return val;
}

void fifo_init(fifo_t * fifo, size_t size, size_t width)
{
  fifo->lock = 0;
  fifo->S = size;
  fifo->W = width;

  fifo->head.index = 0;
  fifo->head.node = new_node(0, size);
  fifo->tail[ENQ].index = 0;
  fifo->tail[DEQ].index = 0;

  fifo->plist = NULL;
}

void fifo_register(fifo_t * fifo, handle_t * me)
{
  me->node[ENQ]  = fifo->head.node;
  me->node[DEQ]  = fifo->head.node;
  me->hazard = NULL;

  handle_t * curr = fifo->plist;

  do {
    me->next = curr;
    curr = compare_and_swap(&fifo->plist, curr, me);
  } while (me->next != curr);
}

void fifo_unregister(fifo_t * fifo, handle_t * me)
{
  /** Remove myself from plist. */
  lock(&fifo->lock);

  fifo->W -= 1;

  handle_t * p = fifo->plist;

  if (p == me) {
    fifo->plist = me->next;
  } else {
    while (p->next != me) p = p->next;
    p->next = me->next;
  }

  unlock(&fifo->lock);
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
