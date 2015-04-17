#include <stdlib.h>
#include "align.h"
#include "atomic.h"
#include "hzdptr.h"

typedef struct _node_t {
  void * data DOUBLE_CACHE_ALIGNED;
  struct _node_t * volatile next;
} node_t;

typedef struct _msqueue_t {
  struct _node_t * volatile head DOUBLE_CACHE_ALIGNED;
  struct _node_t * volatile tail DOUBLE_CACHE_ALIGNED;
} msqueue_t;

void msqueue_init(msqueue_t * q)
{
  node_t * node = align_malloc(sizeof(node_t), CACHE_LINE_SIZE);
  node->next = NULL;

  q->head = node;
  q->tail = node;
}

void msqueue_put(msqueue_t * q, hzdptr_t * hzd, void * data)
{
  node_t * node = align_malloc(sizeof(node_t), CACHE_LINE_SIZE);
  node->data = data;
  node->next = NULL;

  node_t * tail;
  node_t * next;

  while (1) {
    tail = hzdptr_setv(&q->tail, hzd, 0);
    next = tail->next;

    if (tail != q->tail) {
      continue;
    }

    if (next != NULL) {
      compare_and_swap(&q->tail, &tail, next);
      continue;
    }

    if (compare_and_swap(&tail->next, &next, node)) break;
  }

  compare_and_swap(&q->tail, &tail, node);
}

void * msqueue_get(msqueue_t * q, hzdptr_t * hzd)
{
  void * data;

  node_t * head;
  node_t * tail;
  node_t * next;

  while (1) {
    head = hzdptr_setv(&q->head, hzd, 0);
    tail = q->tail;
    next = hzdptr_set(&head->next, hzd, 1);

    if (head != q->head) {
      continue;
    }

    if (next == NULL) {
      return (void *) -1;
    }

    if (head == tail) {
      compare_and_swap(&q->tail, &tail, next);
      continue;
    }

    data = next->data;
    if (compare_and_swap(&q->head, &head, next)) break;
  }

  hzdptr_retire(hzd, head);
  return data;
}

#ifdef BENCHMARK
#include <stdint.h>

static msqueue_t msqueue;
static int n = 10000000;
static hzdptr_t ** hzdptrs;
static int _nprocs;

int init(int nprocs)
{
  _nprocs = nprocs;
  msqueue_init(&msqueue);
  hzdptrs = malloc(sizeof(hzdptr_t * [nprocs]));
  n /= nprocs;
  return n;
}

void thread_init(int id) {
  hzdptrs[id] = malloc(sizeof(hzdptr_t) + hzdptr_size(_nprocs, 2));
  hzdptr_init(hzdptrs[id], _nprocs, 2);
};

void thread_exit(int id, void * local) {};

int test(int id)
{
  void * val = (void *) (intptr_t) (id + 1);
  int i;

  for (i = 0; i < n; ++i) {
    msqueue_put(&msqueue, hzdptrs[id], val);

    do val = msqueue_get(&msqueue, hzdptrs[id]);
    while (val == (void *) -1);
  }

  return (int) (intptr_t) val;
}

#endif

