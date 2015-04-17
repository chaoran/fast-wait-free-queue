#include <stdint.h>
#include <stdlib.h>
#include "align.h"
#include "ccsynch.h"

typedef struct _node_t {
  struct _node_t * next CACHE_ALIGNED;
  void * volatile data;
} node_t;

typedef struct _ccqueue_t {
  ccsynch_t enq DOUBLE_CACHE_ALIGNED;
  ccsynch_t deq DOUBLE_CACHE_ALIGNED;
  node_t * head DOUBLE_CACHE_ALIGNED;
  node_t * tail DOUBLE_CACHE_ALIGNED;
} ccqueue_t;

typedef struct _handle_t {
  ccsynch_handle_t enq;
  ccsynch_handle_t deq;
  node_t * next;
} handle_t;

static inline
void serialEnqueue(void * state, void * data)
{
  node_t * volatile * tail = (node_t **) state;
  node_t * node = (node_t *) data;

  (*tail)->next = node;
  *tail = node;
}

static inline
void serialDequeue(void * state, void * data)
{
  node_t * volatile * head = (node_t **) state;
  node_t ** ptr = (node_t **) data;

  node_t * node = *head;
  node_t * next = node->next;

  if (next) {
    node->data = next->data;
    *head = next;
  } else {
    node->data = (void *) -1;
  }

  *ptr = node;
}

void ccqueue_init(ccqueue_t * queue)
{
  ccsynch_init(&queue->enq);
  ccsynch_init(&queue->deq);

  node_t * dummy = align_malloc(sizeof(node_t), CACHE_LINE_SIZE);
  dummy->data = 0;
  dummy->next = NULL;

  queue->head = dummy;
  queue->tail = dummy;
}

void ccqueue_handle_init(ccqueue_t * queue, handle_t * handle)
{
  ccsynch_handle_init(&handle->enq);
  ccsynch_handle_init(&handle->deq);

  handle->next = align_malloc(sizeof(node_t), CACHE_LINE_SIZE);
}

void ccqueue_enq(ccqueue_t * queue, handle_t * handle, void * data)
{
  node_t * node = handle->next;
  node->data = data;
  node->next = NULL;

  ccsynch_apply(&queue->enq, &handle->enq, &serialEnqueue, &queue->tail, node);
}

void * ccqueue_deq(ccqueue_t * queue, handle_t * handle)
{
  node_t * node;
  ccsynch_apply(&queue->deq, &handle->deq, &serialDequeue, &queue->head, &node);

  void * data = node->data;
  handle->next = node;
  return data;
}

#ifdef BENCHMARK

static ccqueue_t queue;
static handle_t ** handles;
static int n = 10000000;

int init(int nprocs)
{
  ccqueue_init(&queue);
  handles = malloc(sizeof(handle_t * [nprocs]));

  n /= nprocs;
  return n;
}

void thread_init(int id)
{
  handle_t * handle = malloc(sizeof(handle_t));
  handles[id] = handle;
  ccqueue_handle_init(&queue, handle);
}

void thread_exit(int id, void * args) {}

int test(int id)
{
  intptr_t val = id + 1;
  int i;

  handle_t * handle = handles[id];

  for (i = 0; i < n; ++i) {
    ccqueue_enq(&queue, handle, (void *) val);

    do val = (intptr_t) ccqueue_deq(&queue, handle);
    while (val == -1);
  }

  return (int) val;
}

#endif
