#ifndef CLH_H
#define CLH_H

#include "align.h"
#include "atomic.h"

typedef struct _clh_node_t {
  struct _clh_node_t * next CACHE_ALIGNED;
  volatile int flag CACHE_ALIGNED;
} clh_node_t;

typedef struct _clh_lock_t {
  clh_node_t * tail DOUBLE_CACHE_ALIGNED;
} clh_lock_t;

typedef struct _clh_handle_t {
  clh_node_t * node DOUBLE_CACHE_ALIGNED;
} clh_handle_t;

#define CLH_FLAG_WAIT  0
#define CLH_FLAG_READY 1

extern void clh_lock_init(clh_lock_t * lock);

static inline
void clh_lock(clh_lock_t * lock, clh_handle_t * handle)
{
  clh_node_t * next = handle->node;
  next->flag = CLH_FLAG_WAIT;
  release_fence();

  clh_node_t * node = swap(&lock->tail, next);
  node->next = next;

  spin_while(node->flag == CLH_FLAG_WAIT);
  acquire_fence();

  handle->node = node;
}

static inline
void clh_unlock(clh_lock_t * lock, clh_handle_t * handle)
{
  handle->node->next->flag = CLH_FLAG_READY;
  release_fence();
}

#endif /* end of include guard: CLH_H */
