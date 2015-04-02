#include <stdlib.h>

struct _node_t;
typedef struct _node_t * abaptr_t;
#include "abaptr.h"

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

void msqueue_put(msqueue_t * q, void * data)
{
  node_t * node = malloc(sizeof(node_t));
  node->data = data;
  node->next = NULL;

  node_t * tail;
  node_t * next;

  while (1) {
    tail = q->tail;
    next = abaptr(tail)->next;

    if (tail == q->tail) {
      if (abaptr(next) == NULL) {
        if (abaptr_cas(&abaptr(tail)->next, next, node)) break;
      } else {
        abaptr_cas(&q->tail, tail, abaptr(next));
      }
    }
  }

  abaptr_cas(&q->tail, tail, node);
}

void * msqueue_get(msqueue_t * q)
{
  void * data;

  node_t * head;
  node_t * tail;
  node_t * nextptr;

  while (1) {
    head = q->head;
    tail = q->tail;
    nextptr = abaptr(abaptr(head)->next);

    if (head == q->head) {
      if (abaptr(head) == abaptr(tail)) {
        if (nextptr == NULL) {
          return (void *) -1;
        }

        abaptr_cas(&q->tail, tail, nextptr);
      } else {
        /** This check is not in the paper. But it seems necessary. */
        if (nextptr == NULL) continue;

        data = nextptr->data;

        if (abaptr_cas(&q->head, head, nextptr)) break;
      }
    }
  }

  free(abaptr(head));
  return data;
}

#ifdef BENCHMARK
#include <stdint.h>

static msqueue_t msqueue;
static int n = 10000000;

int init(int nprocs)
{
  msqueue_init(&msqueue);
  n /= nprocs;
  return n;
}

void thread_init(int id, void * local) {};
void thread_exit(int id, void * local) {};

int test(int id)
{
  void * val = (void *) (intptr_t) (id + 1);
  int i;

  for (i = 0; i < n; ++i) {
    msqueue_put(&msqueue, val);

    do {
      val = msqueue_get(&msqueue);
    } while (val == (void *) -1);
  }

  return (int) (intptr_t) val;
}

#endif

