#include <stdlib.h>
#include "delay.h"
#include "msqueue.h"
#include "primitives.h"

void queue_init(queue_t * q, int nprocs)
{
  node_t * node = malloc(sizeof(node_t));
  node->next = NULL;

  q->head = node;
  q->tail = node;
  q->nprocs = nprocs;
}

void queue_register(queue_t * q, handle_t * th, int id)
{
  hzdptr_init(&th->hzd, q->nprocs, 2);
}

void enqueue(queue_t * q, handle_t * handle, void * data)
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
      CAS(&q->tail, &tail, next);
      continue;
    }

    if (CAS(&tail->next, &next, node)) break;
  }

  CAS(&q->tail, &tail, node);
}

void * dequeue(queue_t * q, handle_t * handle)
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
      CAS(&q->tail, &tail, next);
      continue;
    }

    data = next->data;
    if (CAS(&q->head, &head, next)) break;
  }

  hzdptr_retire(&handle->hzd, head);
  return data;
}

void queue_free(int id, int nprocs) {}
