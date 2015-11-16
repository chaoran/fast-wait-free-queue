#ifndef MSQUEUE_H
#define MSQUEUE_H

#ifdef MSQUEUE
#include "align.h"
#include "hzdptr.h"

#define EMPTY (void *) -1

typedef struct _node_t {
  struct _node_t * volatile next DOUBLE_CACHE_ALIGNED;
  void * data DOUBLE_CACHE_ALIGNED;
} node_t DOUBLE_CACHE_ALIGNED;

typedef struct _queue_t {
  struct _node_t * volatile head DOUBLE_CACHE_ALIGNED;
  struct _node_t * volatile tail DOUBLE_CACHE_ALIGNED;
  int nprocs;
} queue_t DOUBLE_CACHE_ALIGNED;

typedef struct _handle_t {
  hzdptr_t hzd;
} handle_t DOUBLE_CACHE_ALIGNED;

#endif

#endif /* end of include guard: MSQUEUE_H */
