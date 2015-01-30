#include <stdlib.h>

struct _node_t;

typedef struct __attribute__((__aligned__(16))) {
  struct _node_t * ptr;
  size_t count;
} pointer_t;

typedef struct _node_t {
  void * data;
  volatile pointer_t next;
} node_t;

typedef struct _msqueue_t {
  volatile pointer_t head;
  volatile pointer_t tail;
} msqueue_t;

static inline
int compare_and_swap(volatile pointer_t * ptr, pointer_t cmp, void * val)
{
  char success;

  __asm__ __volatile__ ( "lock; cmpxchg16b %1\n\t setz %0"
      : "=q" (success), "=m" (*ptr), "+&a" (cmp.ptr), "+&d" (cmp.count)
      : "b" (val), "c" (cmp.count + 1)
      : "cc" );

  return success;
}

static inline
int is_equal(pointer_t p1, pointer_t p2)
{
  return p1.ptr == p2.ptr && p1.count == p2.count;
}

void msqueue_init(msqueue_t * q)
{
  node_t * node = malloc(sizeof(node_t));
  node->next.ptr = NULL;
  node->next.count = 0;

  q->head.ptr = node;
  q->head.count = 0;
  q->tail.ptr = node;
  q->tail.count = 100;
}

void msqueue_put(msqueue_t * q, void * data)
{
  node_t * node = malloc(sizeof(node_t));
  node->data = data;
  node->next.ptr = NULL;

  pointer_t tail;
  pointer_t next;

  while (1) {
    tail = q->tail;
    next = tail.ptr->next;

    if (is_equal(tail, q->tail)) {
      if (next.ptr == NULL) {
        if (compare_and_swap(&tail.ptr->next, next, node)) break;
      } else {
        compare_and_swap(&q->tail, tail, next.ptr);
      }
    }
  }

  compare_and_swap(&q->tail, tail, node);
}

void * msqueue_get(msqueue_t * q)
{
  void * data;

  pointer_t head;
  pointer_t tail;
  node_t *  nextptr;

  while (1) {
    head = q->head;
    tail = q->tail;
    nextptr = head.ptr->next.ptr;

    if (is_equal(head, q->head)) {
      if (head.ptr == tail.ptr) {
        if (nextptr == NULL) {
          return (void *) -1;
        }

        compare_and_swap(&q->tail, tail, nextptr);
      } else {
        /** This check is not in the paper. But it seems necessary. */
        if (nextptr == NULL) continue;

        data = nextptr->data;

        if (compare_and_swap(&q->head, head, nextptr)) break;
      }
    }
  }

  free(head.ptr);
  return data;
}

#ifdef BENCHMARK

static msqueue_t msqueue;
typedef int thread_local_t;

#include "bench.h"

void init(int nprocs)
{
  msqueue_init(&msqueue);
}

void thread_init(int id, void * local) {};
void thread_exit(int id, void * local) {};

void enqueue(void * val, void * local)
{
  msqueue_put(&msqueue, val);
}

void * dequeue(void * local)
{
  void * val;

  do {
    val = msqueue_get(&msqueue);
  } while (val == (void *) -1);

  return val;
}

#endif

