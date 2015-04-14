#ifndef _CCSYNCH_H_
#define _CCSYNCH_H_

#include <stdlib.h>
#include "align.h"
#include "atomic.h"

typedef struct _ccsynch_node_t {
  struct _ccsynch_node_t * volatile next CACHE_ALIGNED;
  void * volatile data;
  int volatile status CACHE_ALIGNED;
} ccsynch_node_t;

typedef struct _ccsynch_handle_t {
  struct _ccsynch_node_t * next;
} ccsynch_handle_t;

typedef struct _ccsynch_t {
  struct _ccsynch_node_t * volatile tail DOUBLE_CACHE_ALIGNED;
} ccsynch_t;

#define CCSYNCH_WAIT  0x0
#define CCSYNCH_READY 0x1
#define CCSYNCH_DONE  0x3

static inline
void ccsynch_apply(ccsynch_t * synch, ccsynch_handle_t * handle,
    void (*apply)(void *, void *), void * state, void * data)
{
  ccsynch_node_t * next = handle->next;
  next->next = NULL;
  next->status = CCSYNCH_WAIT;

  ccsynch_node_t * curr = swap(&synch->tail, next);
  handle->next = curr;

  int status = curr->status;
  acquire_fence();

  if (status == CCSYNCH_WAIT) {
    curr->data = data;
    release_fence();
    curr->next = next;
    spin_while((status = curr->status) == CCSYNCH_WAIT);
    acquire_fence();
  }

  if (status != CCSYNCH_DONE) {
    apply(state, data);

    curr = next;
    next = curr->next;
    acquire_fence();

    int count = 0;
    const int CCSYNCH_HELP_BOUND = 256;

    while (next && count++ < CCSYNCH_HELP_BOUND) {
      apply(state, curr->data);
      release_fence();
      curr->status = CCSYNCH_DONE;

      curr = next;
      next = curr->next;
      acquire_fence();
    }

    release_fence();
    curr->status = CCSYNCH_READY;
  }
}

void ccsynch_init(ccsynch_t * synch)
{
  ccsynch_node_t * node = align_malloc(sizeof(ccsynch_node_t), CACHE_LINE_SIZE);
  node->next = NULL;
  node->status = CCSYNCH_READY;

  synch->tail = node;
}

void ccsynch_handle_init(ccsynch_handle_t * handle)
{
  handle->next = align_malloc(sizeof(ccsynch_node_t), CACHE_LINE_SIZE);
}

#endif
