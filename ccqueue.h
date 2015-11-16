#ifndef CCQUEUE_H
#define CCQUEUE_H

#ifdef CCQUEUE
#include "ccsynch.h"

#define EMPTY (void *) -1

typedef struct _node_t {
  struct _node_t * next CACHE_ALIGNED;
  void * volatile data;
} node_t;

typedef struct _queue_t {
  ccsynch_t enq DOUBLE_CACHE_ALIGNED;
  ccsynch_t deq DOUBLE_CACHE_ALIGNED;
  node_t * head DOUBLE_CACHE_ALIGNED;
  node_t * tail DOUBLE_CACHE_ALIGNED;
} queue_t DOUBLE_CACHE_ALIGNED;

typedef struct _handle_t {
  ccsynch_handle_t enq;
  ccsynch_handle_t deq;
  node_t * next;
} handle_t DOUBLE_CACHE_ALIGNED;

#endif

#endif /* end of include guard: CCQUEUE_H */
