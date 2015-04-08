#ifndef _CCSTACK_H_
#define _CCSTACK_H_

#include "ccsynch.h"

#define CACHE_ALIGNED __attribute__((aligned(64)))

typedef struct ccqueue_node_t {
  void * data;
  volatile struct ccqueue_node_t * next;
} ccqueue_node_t;

typedef struct _ccqueue_t {
  ccsynch_t enq CACHE_ALIGNED;
  ccsynch_t deq CACHE_ALIGNED;
  volatile ccqueue_node_t * tail CACHE_ALIGNED;
  volatile ccqueue_node_t * head CACHE_ALIGNED;
} ccqueue_t;

typedef struct _ccqueue_handle_t {
  ccsynch_handle_t enq;
  ccsynch_handle_t deq;
} ccqueue_handle_t;

static inline
void serialEnqueue(void *state, void * arg) {
  ccqueue_t *queue = (ccqueue_t *) state;
  ccqueue_node_t *node;

  node = malloc(sizeof(ccqueue_node_t));
  node->next = NULL;
  node->data = arg;
  queue->tail->next = node;
  queue->tail = node;
}

static inline
void serialDequeue(void *state, void * arg) {
  ccqueue_t * queue = (ccqueue_t *) state;
  void ** ptr = (void **) arg;
  ccqueue_node_t *node = (ccqueue_node_t *)queue->head;

  if (queue->head->next != NULL){
    queue->head = queue->head->next;
    free(node);
    *ptr = queue->head->data;
  } else {
    *ptr = (void *) -1;
  }
}

static inline
void ccqueue_init(ccqueue_t *queue)
{
  ccsynch_init(&queue->enq, &serialEnqueue, queue);
  ccsynch_init(&queue->deq, &serialDequeue, queue);

  ccqueue_node_t * dummy = malloc(sizeof(ccqueue_node_t));
  dummy->data = 0;
  dummy->next = NULL;

  queue->head = dummy;
  queue->tail = dummy;
}

static inline
void ccqueue_handle_init(ccqueue_t *queue, ccqueue_handle_t *handle)
{
  ccsynch_handle_init(&handle->enq);
  ccsynch_handle_init(&handle->deq);
}

static inline
void ccqueue_enq(ccqueue_t *queue, ccqueue_handle_t *handle, void * arg)
{
  ccsynch_apply(&queue->enq, &handle->enq, arg);
}

static inline
void * ccqueue_deq(ccqueue_t *queue, ccqueue_handle_t *handle)
{
  void * data;
  ccsynch_apply(&queue->deq, &handle->deq, &data);
  return data;
}

#endif
