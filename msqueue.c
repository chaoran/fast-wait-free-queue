#include <stdlib.h>
#include "align.h"
#include "delay.h"
#include "atomic.h"
#include "hzdptr.h"

typedef struct _node_t {
  struct _node_t * volatile next DOUBLE_CACHE_ALIGNED;
  void * data DOUBLE_CACHE_ALIGNED;
} node_t;

typedef struct _msqueue_t {
  struct _node_t * volatile head DOUBLE_CACHE_ALIGNED;
  struct _node_t * volatile tail DOUBLE_CACHE_ALIGNED;
} msqueue_t;

typedef struct _handle_t {
  hzdptr_t hzd;
} handle_t;

void msqueue_init(msqueue_t * q)
{
  node_t * node = malloc(sizeof(node_t));
  node->next = NULL;

  q->head = node;
  q->tail = node;
}

void msqueue_put(msqueue_t * q, handle_t * handle, void * data)
{
  node_t * node = malloc(sizeof(node_t));

  node->data = data;
  node->next = NULL;

  node_t * tail;
  node_t * next;

  while (1) {
    tail = hzdptr_setv(&q->tail, &handle->hzd, 0);
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

void * msqueue_get(msqueue_t * q, handle_t * handle)
{
  void * data;

  node_t * head;
  node_t * tail;
  node_t * next;

  while (1) {
    head = hzdptr_setv(&q->head, &handle->hzd, 0);
    tail = q->tail;
    next = hzdptr_set(&head->next, &handle->hzd, 1);

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

  hzdptr_retire(&handle->hzd, head);
  return data;
}

void * init(int nprocs)
{
  msqueue_t * q = align_malloc(sizeof(msqueue_t), PAGE_SIZE);
  msqueue_init(q);
  return q;
}

void * thread_init(int nprocs, int id, void * q)
{
  size_t size = sizeof(handle_t) + hzdptr_size(nprocs, 2);
  handle_t * th = align_malloc(size, PAGE_SIZE);
  hzdptr_init(&th->hzd, nprocs, 2);
  return th;
};

void enqueue(void * q, void * th, void * val)
{
  msqueue_put((msqueue_t *) q, (handle_t *) th, val);
}

void * dequeue(void * q, void * th)
{
  return msqueue_get((msqueue_t *) q, (handle_t *) th);
}

void * EMPTY = (void *) -1;
