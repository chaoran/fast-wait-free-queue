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
#define release(ptr, val) __atomic_store_n(ptr, val, __ATOMIC_RELEASE)
#define lock(p) spin_while(__atomic_test_and_set(p, __ATOMIC_ACQUIRE))
#define unlock(p) __atomic_clear(p, __ATOMIC_RELEASE)

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
void cleanup(fifo_t * fifo, node_t * head)
{
  size_t  index = fifo->head.index;
  int threshold = 2 * fifo->nprocs;

  if (index != -1 && head->id - index > threshold &&
      index == compare_and_swap(&fifo->head.index, index, -1)) {
    node_t * curr = fifo->head.node;
    handle_t * p;

    for (p = fifo->plist; p != NULL && curr != head; p = p->next) {
      head = check(&p->hazard, NULL, head);
      head = check(&p->enq, &p->hazard, head);
      head = check(&p->deq, &p->hazard, head);
    }

    while (curr != head) {
      node_t * next = curr->next;
      free(curr);
      curr = next;
    }

    fifo->head.node = head;
    release(&fifo->head.index, head->id);
  }
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
node_t * acquire(node_t * volatile * pnode, node_t * volatile * phazard)
{
  node_t * node = *pnode;
  node_t * temp;

  do {
    temp = node;
    *phazard = node;
    mfence();
    node = *pnode;
  } while (node != temp);

  return node;
}

void fifo_put(fifo_t * fifo, handle_t * handle, void * data)
{
  node_t * node = acquire(&handle->enq, &handle->hazard);

  size_t i  = fetch_and_add(&fifo->enq, 1);
  size_t s  = fifo->size;
  size_t ni = i / s;
  size_t li = i % s;

  if (node->id != ni) {
    node = handle->enq = update(node, ni, s, &handle->winner);
  }

  node->buffer[li].data = data;
  release(&handle->hazard, NULL);
}

void * fifo_get(fifo_t * fifo, handle_t * handle)
{
  node_t * node = acquire(&handle->deq, &handle->hazard);

  size_t i  = fetch_and_add(&fifo->deq, 1);
  size_t s  = fifo->size;
  size_t ni = i / s;
  size_t li = i % s;

  if (node->id != ni) {
    node = handle->deq = update(node, ni, s, &handle->winner);
  }

  void * val;
  spin_while(NULL == (val = node->buffer[li].data));

  if (handle->winner) {
    cleanup(fifo, node);
    handle->winner = 0;
  }

  release(&handle->hazard, NULL);
  return val;
}

void fifo_init(fifo_t * fifo, size_t size, size_t width)
{
  fifo->lock = 0;
  fifo->size = size;
  fifo->nprocs = width;

  fifo->head.index = 0;
  fifo->head.node = new_node(0, size);
  fifo->enq = 0;
  fifo->deq = 0;

  fifo->plist = NULL;
}

void fifo_register(fifo_t * fifo, handle_t * me)
{
  me->enq = fifo->head.node;
  me->deq = fifo->head.node;
  me->hazard = NULL;
  me->winner = 0;

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

  fifo->nprocs -= 1;

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
  fifo_init(&fifo, 511, nprocs);
}

void thread_init(int id, void * handle)
{
  int i;
  void * mem[100];

  for (i = 0; i < 100; ++i) {
    mem[i] = new_node(0, 511);
  }
  for (i = 0; i < 100; ++i) {
    free(mem[i]);
  }

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
