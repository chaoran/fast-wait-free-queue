#ifndef LCRQ_H
#define LCRQ_H

#include "align.h"
#include "hzdptr.h"

#define EMPTY ((void *) -1)

#ifndef LCRQ_RING_SIZE
#define LCRQ_RING_SIZE (1ull << 12)
#endif

DOUBLE_CACHE_ALIGNED struct _RingNode {
  volatile uint64_t val;
  volatile uint64_t idx;
  uint64_t pad[14];
};

DOUBLE_CACHE_ALIGNED struct _RingQueue {
  volatile int64_t head DOUBLE_CACHE_ALIGNED;
  volatile int64_t tail DOUBLE_CACHE_ALIGNED;
  struct _RingQueue *next DOUBLE_CACHE_ALIGNED;
  struct _RingNode array[LCRQ_RING_SIZE];
};

typedef struct {
  struct _RingQueue * volatile head DOUBLE_CACHE_ALIGNED;
  struct _RingQueue * volatile tail DOUBLE_CACHE_ALIGNED;
  int nprocs;
} queue_t;

typedef struct {
  struct _RingQueue * next;
  hzdptr_t hzdptr;
} handle_t;

#endif /* end of include guard: LCRQ_H */
