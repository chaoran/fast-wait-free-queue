#include <stdlib.h>
#include "ccsynch.h"

#define CACHE_ALIGNED __attribute__((aligned(64)))

typedef struct _ccqueue_node_t {
  struct _ccqueue_node_t * volatile next;
  void * data;
} ccqueue_node_t;

typedef struct _ccqueue_t {
  ccsynch_t enq CACHE_ALIGNED;
  ccsynch_t deq CACHE_ALIGNED;
  ccqueue_node_t * volatile head CACHE_ALIGNED;
  ccqueue_node_t * volatile tail CACHE_ALIGNED;
} ccqueue_t;

typedef struct _ccqueue_handle_t {
  ccsynch_handle_t enq;
  ccsynch_handle_t deq;
} ccqueue_handle_t;

static inline
void serialEnqueue(void * state, void * data)
{
  ccqueue_t * queue = (ccqueue_t *) state;

  ccqueue_node_t * node = malloc(sizeof(ccqueue_node_t));
  node->next = NULL;
  node->data = data;

  queue->tail->next = node;
  queue->tail = node;
}

static inline
void serialDequeue(void * state, void * data)
{
  ccqueue_t * queue = (ccqueue_t *) state;
  void ** ptr = (void **) data;

  ccqueue_node_t * node = (ccqueue_node_t *) queue->head;

  if (node->next) {
    queue->head = node->next;
    free(node);
    *ptr = queue->head->data;
  } else {
    *ptr = (void *) -1;
  }
}

void ccqueue_init(ccqueue_t * queue)
{
  ccsynch_init(&queue->enq, &serialEnqueue, queue);
  ccsynch_init(&queue->deq, &serialDequeue, queue);

  ccqueue_node_t * dummy = malloc(sizeof(ccqueue_node_t));
  dummy->data = 0;
  dummy->next = NULL;

  queue->head = dummy;
  queue->tail = dummy;
}

void ccqueue_handle_init(ccqueue_t * queue, ccqueue_handle_t * handle)
{
  ccsynch_handle_init(&handle->enq);
  ccsynch_handle_init(&handle->deq);
}

void ccqueue_enq(ccqueue_t * queue, ccqueue_handle_t * handle, void * data)
{
  ccsynch_apply(&queue->enq, &handle->enq, data);
}

void * ccqueue_deq(ccqueue_t * queue, ccqueue_handle_t * handle)
{
  void * data;
  ccsynch_apply(&queue->deq, &handle->deq, &data);
  return data;
}

#ifdef BENCHMARK

static ccqueue_t queue;
static ccqueue_handle_t ** handles;
static int n = 10000000;

int init(int nprocs)
{
  ccqueue_init(&queue);
  handles = malloc(sizeof(ccqueue_handle_t * [nprocs]));

  n /= nprocs;
  return n;
}

void thread_init(int id)
{
  ccqueue_handle_t * handle = malloc(sizeof(ccqueue_handle_t));
  handles[id] = handle;
  ccqueue_handle_init(&queue, handle);
}

void thread_exit(int id, void * args) {}

int test(int id)
{
  size_t val = id + 1;
  int i;

  ccqueue_handle_t * handle = handles[id];

  for (i = 0; i < n; ++i) {
    ccqueue_enq(&queue, handle, (void *) val);

    do val = (size_t) ccqueue_deq(&queue, handle);
    while (val == -1);
  }

  return val;
}

#endif
