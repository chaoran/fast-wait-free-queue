#include <stdlib.h>
#include "atomic.h"
#include "hzdptr.h"

typedef struct _node_t {
  void * data;
  struct _node_t * volatile next;
} node_t;

typedef struct _msqueue_t {
  struct _node_t * volatile head;
  struct _node_t * volatile tail;
} msqueue_t;

void msqueue_init(msqueue_t * q)
{
  node_t * node = malloc(sizeof(node_t));
  node->next = NULL;

  q->head = node;
  q->tail = node;
}

void msqueue_put(msqueue_t * q, hzdptr_t * hzd, void * data)
{
  node_t * node = malloc(sizeof(node_t));
  node->data = data;
  node->next = NULL;

  node_t * tail;
  node_t * next;

  while (1) {
    tail = hzdptr_loadv(hzd, 0, (void * volatile *) &q->tail);
    next = tail->next;

    if (tail != q->tail) {
      continue;
    }

    if (next != NULL) {
      compare_and_swap(&q->tail, tail, next);
      continue;
    }

    if (NULL == compare_and_swap(&tail->next, NULL, node)) break;
  }

  compare_and_swap(&q->tail, tail, node);
}

void * msqueue_get(msqueue_t * q, hzdptr_t * hzd)
{
  void * data;

  node_t * head;
  node_t * tail;
  node_t * next;

  while (1) {
    head = hzdptr_loadv(hzd, 0, (void * volatile *) &q->head);
    tail = q->tail;
    next = hzdptr_load(hzd, 1, (void * volatile *) &head->next);

    if (head != q->head) {
      continue;
    }

    if (next == NULL) {
      return (void *) -1;
    }

    if (head == tail) {
      compare_and_swap(&q->tail, tail, next);
      continue;
    }

    data = next->data;
    if (head == compare_and_swap(&q->head, head, next)) break;
  }

  hzdptr_retire(hzd, head);
  return data;
}

#ifdef BENCHMARK
#include <stdint.h>

static msqueue_t msqueue;
static int n = 10000000;
static hzdptr_t ** hzdptrs;
static _nprocs;

int init(int nprocs)
{
  _nprocs = nprocs;
  msqueue_init(&msqueue);
  hzdptrs = malloc(sizeof(hzdptr_t * [nprocs]));
  n /= nprocs;
  return n;
}

void thread_init(int id) {
  hzdptrs[id] = hzdptr_init(_nprocs, 2);
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

