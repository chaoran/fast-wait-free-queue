#include <stdlib.h>
#include <assert.h>
#include "delay.h"
#include "ccqueue.h"

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
    node = (void *) -1;
  }

  *ptr = node;
}

void queue_init(queue_t * queue, int nprocs)
{
  ccsynch_init(&queue->enq);
  ccsynch_init(&queue->deq);

  node_t * dummy = align_malloc(CACHE_LINE_SIZE, sizeof(node_t));
  dummy->data = 0;
  dummy->next = NULL;

  queue->head = dummy;
  queue->tail = dummy;
}

void queue_register(queue_t * queue, handle_t * handle, int id)
{
  ccsynch_handle_init(&handle->enq);
  ccsynch_handle_init(&handle->deq);

  handle->next = align_malloc(CACHE_LINE_SIZE, sizeof(node_t));
}

void enqueue(queue_t * queue, handle_t * handle, void * data)
{
  node_t * node = handle->next;

  if (node) handle->next = NULL;
  else node = align_malloc(CACHE_LINE_SIZE, sizeof(node_t));

  node->data = data;
  node->next = NULL;

  ccsynch_apply(&queue->enq, &handle->enq, &serialEnqueue, &queue->tail, node);
}

void * dequeue(queue_t * queue, handle_t * handle)
{
  node_t * node;
  ccsynch_apply(&queue->deq, &handle->deq, &serialDequeue, &queue->head, &node);

  void * data;

  if (node == (void *) -1) {
    data = (void *) -1;
  } else {
    data = node->data;
    if (handle->next) free(node);
    else handle->next = node;
  }

  return data;
}

void queue_free(int id, int nprocs) {}
