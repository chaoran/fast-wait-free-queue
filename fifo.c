#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fifo.h"

typedef union {
  void * volatile data;
  char padding[FIFO_CACHELINE_SIZE];
} cache_t;

typedef struct _fifo_node_t {
  struct _fifo_node_t * volatile next FIFO_CACHELINE_ALIGNED;
  size_t id;
  cache_t buffer[0] FIFO_CACHELINE_ALIGNED;
} node_t;

typedef fifo_handle_t handle_t;

#define fetch_and_add(p, v) __atomic_fetch_add(p, v, __ATOMIC_RELAXED)
#define compare_and_swap __sync_val_compare_and_swap
#define spin_while(cond) while (cond) __asm__ ("pause")
#define mfence() __atomic_thread_fence(__ATOMIC_SEQ_CST)
#define cfence() __atomic_thread_fence(__ATOMIC_ACQ_REL)
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
node_t * check(node_t ** pnode, node_t * volatile * phazard,
    node_t * to)
{
  node_t * node = *pnode;

  if (phazard) {
    if (node->id < to->id) {
      node_t * curr = compare_and_swap(pnode, node, to);
      cfence();
      node_t * hazard = *phazard;
      node = hazard ? hazard : (curr == node ? to : curr);

      if (node->id < to->id) {
        to = node;
      }
    }
  } else {
    if (node && node->id < to->id) {
      to = node;
    }
  }

  return to;
}

static inline
node_t * cleanup(handle_t * plist, handle_t * rlist, node_t * from, node_t * to)
{
  handle_t * p;

  for (p = plist; p != NULL && from != to; p = p->next) {
    to = check(&p->hazard, NULL, to);
    to = check(&p->node[0], &p->hazard, to);
    to = check(&p->node[1], &p->hazard, to);
  }

  while (from != to) {
    node_t * next = from->next;
    free(from);
    from = next;
  }

  return to;
}

static inline
node_t * update(node_t * node, size_t to, size_t size, int * winner)
{
  size_t i;

  for (i = node->id; i < to; ++i) {
    node_t * prev = node;
    node = prev->next;

    if (!node) {
      node_t * next = new_node(i + 1, size);
      node = compare_and_swap(&prev->next, NULL, next);

      if (node) free(next);
      else {
        node = next;
        *winner = 1;
      }
    }
  }

  return node;
}

static inline
void * volatile * acquire(fifo_t * fifo, handle_t * handle, int op,
    int * winner)
{
  node_t * node;
  node_t * curr = handle->node[op];

  do {
    node = curr;
    handle->hazard = node;
    mfence();
    curr = handle->node[op];
  } while (node != curr);

  size_t s  = fifo->S;
  size_t i  = fetch_and_add(&fifo->tail[op].index, 1);
  size_t ni = i / s;
  size_t li = i % s;

  if (node->id != ni) {
    handle->node[op] = node = update(node, ni, s, winner);
  }

  return &node->buffer[li].data;
}

static inline
void release(fifo_t * fifo, handle_t * handle, int winner)
{
  const int threshold = 2 * fifo->W;
  handle->hazard = NULL;

  if (winner) {
    node_t * node = handle->node[0]->id < handle->node[1]->id ?
      handle->node[0] : handle->node[1];

    /* Do nothing if we haven't reach threshold. */
    size_t index = fifo->head.index;

    if (index != -1 && node->id - index > threshold) {
      if (index == compare_and_swap(&fifo->head.index, index, -1)) {
        node_t * head = fifo->head.node;
        head = cleanup(fifo->plist, handle, head, node);

        fifo->head.node = head;
        __atomic_store_n(&fifo->head.index, head->id, __ATOMIC_RELEASE);
      }
    }
  }
}

void fifo_put(fifo_t * fifo, handle_t * handle, void * data)
{
  int winner = 0;
  void * volatile * ptr = acquire(fifo, handle, ENQ, &winner);
  *ptr = data;
  release(fifo, handle, winner);
}

void * fifo_get(fifo_t * fifo, handle_t * handle)
{
  int winner = 0;
  void * volatile * ptr = acquire(fifo, handle, DEQ, &winner);

  void * val;
  spin_while(NULL == (val = *ptr));

  release(fifo, handle, winner);
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
  me->advanced = 0;

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
