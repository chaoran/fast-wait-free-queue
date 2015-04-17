#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fifo.h"
#include "atomic.h"
#include "hzdptr.h"

typedef union {
  void * volatile data;
  char padding[CACHE_LINE_SIZE];
} cache_t;

typedef struct _fifo_node_t {
  struct _fifo_node_t * volatile next CACHE_ALIGNED;
  size_t id CACHE_ALIGNED;
  cache_t buffer[0] CACHE_ALIGNED;
} node_t;

typedef fifo_handle_t handle_t;

static inline
node_t * new_node(size_t id, size_t size)
{
  size = sizeof(node_t) + sizeof(cache_t [size]);

  node_t * node = align_malloc(size, PAGE_SIZE);
  memset(node, 0, size);

  node->id = id;
  release_fence();
  return node;
}

static inline
node_t * check(node_t ** pnode, node_t * volatile * phazard,
    node_t * to)
{
  node_t * node = *pnode;

  if (phazard) {
    if (node->id < to->id) {
      int succ = compare_and_swap(pnode, &node, to);
      acquire_fence();
      node_t * hazard = *phazard;

      if (hazard) {
        node = hazard;
      } else if (succ) {
        node = to;
      }

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
void cleanup(fifo_t * fifo, node_t * head, handle_t * handle)
{
  size_t  index = fifo->head.index;
  if (index == -1) return;

  int threshold = 2 * fifo->nprocs;
  if (head->id - index < threshold) return;

  if (compare_and_swap(&fifo->head.index, &index, -1)) {
    acquire_fence();
    node_t * curr = fifo->head.node;
    handle_t * p;

    for (p = handle->next; p != handle && curr != head; p = p->next) {
      head = check(&p->hazard, NULL, head);
      head = check(&p->enq, &p->hazard, head);
      head = check(&p->deq, &p->hazard, head);
    }

    if (curr != head) {
      fifo->head.node = head;
      release_fence();
      fifo->head.index = head->id;

      node_t * next = handle->retired;
      handle->retired = curr;
      for (; curr->next != head; curr = curr->next);
      curr->next = next;
    } else {
      fifo->head.index = head->id;
    }
  }
}

static inline
node_t * locate(node_t * node, size_t to, size_t size, handle_t * handle)
{
  size_t i;
  for (i = node->id; i < to; ++i) {
    node_t * prev = node;
    node = prev->next;

    if (node) continue;

    node_t * next = handle->retired;

    if (next) {
      handle->retired = next->next;
      next->next = NULL;
      next->id = i + 1;
      release_fence();
    } else {
      next = new_node(i + 1, size);
    }

    if (compare_and_swap(&prev->next, &node, next)) {
      node = next;
      handle->winner = 1;
    } else {
      next->next = handle->retired;
      handle->retired = next;
    }
  }

  return node;
}

void fifo_put(fifo_t * fifo, handle_t * handle, void * data)
{
  node_t * node = _hzdptr_setv(&handle->enq, &handle->hazard);

  size_t i  = fetch_and_add(&fifo->enq, 1);
  acquire_fence();

  size_t s  = fifo->size;
  size_t ni = i / s;
  size_t li = i % s;

  if (node->id != ni) {
    node = handle->enq = locate(node, ni, s, handle);
  }

  node->buffer[li].data = data;
  handle->hazard = NULL;
}

void * fifo_get(fifo_t * fifo, handle_t * handle)
{
  node_t * node = _hzdptr_setv(&handle->deq, &handle->hazard);

  size_t i  = fetch_and_add(&fifo->deq, 1);
  acquire_fence();

  size_t s  = fifo->size;
  size_t ni = i / s;
  size_t li = i % s;

  if (node->id != ni) {
    node = handle->deq = locate(node, ni, s, handle);
  }

  void * val;
  spin_while(NULL == (val = node->buffer[li].data));
  node->buffer[li].data = NULL;
  handle->hazard = NULL;

  if (handle->winner) {
    cleanup(fifo, node, handle);
    handle->winner = 0;
  }

  return val;
}

void fifo_init(fifo_t * fifo, size_t size, size_t width)
{
  fifo->size = size;
  fifo->nprocs = width;

  fifo->head.index = 0;
  fifo->head.node = new_node(0, size);

  fifo->enq = 0;
  fifo->deq = 0;
}

void fifo_register(fifo_t * fifo, handle_t * me)
{
  me->enq = fifo->head.node;
  me->deq = fifo->head.node;
  me->hazard = NULL;
  me->winner = 0;
  me->retired = NULL;

  _hzdptr_enlist((hzdptr_t *) me);
}

#ifdef BENCHMARK
#include <stdint.h>

static fifo_t fifo;
static fifo_handle_t ** handles;
static int n = 10000000;

int init(int nprocs)
{
  fifo_init(&fifo, 510, nprocs);
  handles = malloc(sizeof(fifo_handle_t * [nprocs]));

  n /= nprocs;
  return n;
}

void thread_init(int id)
{
  fifo_handle_t * handle = malloc(sizeof(fifo_handle_t));
  handles[id] = handle;
  fifo_register(&fifo, handle);
}

void thread_exit(int id) {}

int test(int id)
{
  void * val = (void *) (intptr_t) (id + 1);
  int i;

  for (i = 0; i < n; ++i) {
    fifo_put(&fifo, handles[id], val);
    val = fifo_get(&fifo, handles[id]);
  }

  return (int) (intptr_t) val;
}

#endif
