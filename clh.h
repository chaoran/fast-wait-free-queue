#ifndef CLH_H
#define CLH_H

#include "align.h"
#include "atomic.h"

typedef struct _clh_node_t {
  struct _clh_node_t * next CACHE_ALIGNED;
  volatile int flag CACHE_ALIGNED;
} clh_node_t;

typedef struct _clh_lock_t {
  clh_node_t * volatile tail DOUBLE_CACHE_ALIGNED;
} clh_lock_t;

typedef struct _clh_t {
  clh_lock_t * lock;
  clh_node_t * node;
} clh_t;

#define CLH_FLAG_WAIT  0
#define CLH_FLAG_READY 1

extern void clh_init(clh_lock_t * lock, clh_t * clh);
extern void clh_lock_init(clh_lock_t * lock);

static inline
void clh_lock(clh_t * clh)
{
  clh_node_t * next = clh->node;
  next->flag = CLH_FLAG_WAIT;
  release_fence();

  clh_node_t * node = swap(&clh->lock->tail, next);
  node->next = next;

  spin_while(node->flag == CLH_FLAG_WAIT);
  acquire_fence();

  clh->node = node;
}

static inline
void clh_unlock(clh_t * clh)
{
  clh->node->next->flag = CLH_FLAG_READY;
  release_fence();
}

#endif /* end of include guard: CLH_H */
