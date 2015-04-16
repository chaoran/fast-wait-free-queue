#include <stdint.h>
#include <stdlib.h>
#include "atomic.h"

// Definition: RING_POW
// --------------------
// The LCRQ's ring size will be 2^{RING_POW}.
#ifndef RING_POW
#define RING_POW        (17)
#endif
#define RING_SIZE       (1ull << RING_POW)

// Definition: HAVE_HPTRS
// --------------------
// Define to enable hazard pointer setting for safe memory
// reclamation.  You'll need to integrate this with your
// hazard pointers implementation.
//#define HAVE_HPTRS

#define __CAS2(ptr, o1, o2, n1, n2)                             \
  ({                                                              \
   char __ret;                                                 \
   __typeof__(o2) __junk;                                      \
   __typeof__(*(ptr)) __old1 = (o1);                           \
   __typeof__(o2) __old2 = (o2);                               \
   __typeof__(*(ptr)) __new1 = (n1);                           \
   __typeof__(o2) __new2 = (n2);                               \
   asm volatile("lock cmpxchg16b %2;setz %1"                   \
     : "=d"(__junk), "=a"(__ret), "+m" (*ptr)     \
     : "b"(__new1), "c"(__new2),                  \
     "a"(__old1), "d"(__old2));                 \
   __ret; })
#define CAS2(ptr, o1, o2, n1, n2)    __CAS2(ptr, o1, o2, n1, n2)


#define BIT_TEST_AND_SET(ptr, b)                                \
  ({                                                              \
   char __ret;                                                 \
   asm volatile("lock btsq $63, %0; setnc %1" : "+m"(*ptr), "=a"(__ret) : : "cc"); \
   __ret;                                                      \
   })


inline int is_empty(uint64_t v) __attribute__ ((pure));
inline uint64_t node_index(uint64_t i) __attribute__ ((pure));
inline uint64_t set_unsafe(uint64_t i) __attribute__ ((pure));
inline uint64_t node_unsafe(uint64_t i) __attribute__ ((pure));
inline uint64_t tail_index(uint64_t t) __attribute__ ((pure));
inline int crq_is_closed(uint64_t t) __attribute__ ((pure));

typedef struct RingNode {
  volatile uint64_t val;
  volatile uint64_t idx;
  uint64_t pad[14];
} RingNode __attribute__ ((aligned (128)));

typedef struct RingQueue {
  volatile int64_t head __attribute__ ((aligned (128)));
  volatile int64_t tail __attribute__ ((aligned (128)));
  struct RingQueue *next __attribute__ ((aligned (128)));
  RingNode array[RING_SIZE];
} RingQueue __attribute__ ((aligned (128)));

typedef struct _lcrq_t {
  RingQueue * volatile head;
  RingQueue * volatile tail;
} lcrq_t;

inline void init_ring(RingQueue *r) {
  int i;

  for (i = 0; i < RING_SIZE; i++) {
    r->array[i].val = -1;
    r->array[i].idx = i;
  }

  r->head = r->tail = 0;
  r->next = NULL;
}

inline int is_empty(uint64_t v)  {
  return (v == (uint64_t)-1);
}


inline uint64_t node_index(uint64_t i) {
  return (i & ~(1ull << 63));
}


inline uint64_t set_unsafe(uint64_t i) {
  return (i | (1ull << 63));
}


inline uint64_t node_unsafe(uint64_t i) {
  return (i & (1ull << 63));
}


inline uint64_t tail_index(uint64_t t) {
  return (t & ~(1ull << 63));
}


inline int crq_is_closed(uint64_t t) {
  return (t & (1ull << 63)) != 0;
}

void lcrq_init(lcrq_t * q)
{
  int i;

  RingQueue *rq = malloc(sizeof(RingQueue));
  init_ring(rq);

  q->head = rq;
  q->tail = rq;
}

inline void fixState(RingQueue *rq) {

  uint64_t t, h, n;

  while (1) {
    uint64_t t = fetch_and_add(&rq->tail, 0);
    uint64_t h = fetch_and_add(&rq->head, 0);

    if (rq->tail != t)
      continue;

    if (h > t) {
      if (compare_and_swap(&rq->tail, &t, h)) break;
      continue;
    }
    break;
  }
}

__thread RingQueue *nrq;
__thread RingQueue *hazardptr;

inline int close_crq(RingQueue *rq, const uint64_t t, const int tries) {
  uint64_t tt = t + 1;

  if (tries < 10)
    return compare_and_swap(&rq->tail, &tt, tt|(1ull<<63));
  else
    return BIT_TEST_AND_SET(&rq->tail, 63);
}

static void lcrq_put(lcrq_t * q, uint64_t arg) {
  int try_close = 0;

  while (1) {
    RingQueue *rq = q->tail;

#ifdef HAVE_HPTRS
    SWAP(&hazardptr, rq);
    if (q->tail != rq)
      continue;
#endif

    RingQueue *next = rq->next;

    if (next != NULL) {
      compare_and_swap(&q->tail, &rq, next);
      continue;
    }

    uint64_t t = fetch_and_add(&rq->tail, 1);

    if (crq_is_closed(t)) {
alloc:
      if (nrq == NULL) {
        nrq = malloc(sizeof(RingQueue));
        init_ring(nrq);
      }

      // Solo enqueue
      nrq->tail = 1;
      nrq->array[0].val = (uint64_t) arg;
      nrq->array[0].idx = 0;

      if (compare_and_swap(&rq->next, &next, nrq)) {
        compare_and_swap(&q->tail, &rq, nrq);
        nrq = NULL;
        return;
      }
      continue;
    }

    RingNode* cell = &rq->array[t & (RING_SIZE-1)];

    uint64_t idx = cell->idx;
    uint64_t val = cell->val;

    if (is_empty(val)) {
      if (node_index(idx) <= t) {
        if ((!node_unsafe(idx) || rq->head < t) &&
            CAS2((uint64_t*)cell, -1, idx, arg, t)) {
          return;
        }
      }
    }

    uint64_t h = rq->head;

    if ((int64_t)(t - h) >= (int64_t)RING_SIZE &&
        close_crq(rq, t, ++try_close)) {
      goto alloc;
    }
  }
}

static uint64_t lcrq_get(lcrq_t * q) {
  while (1) {
    RingQueue *rq = q->head;
    RingQueue *next;

#ifdef HAVE_HPTRS
    SWAP(&hazardptr, rq);
    if (head != rq)
      continue;
#endif

    uint64_t h = fetch_and_add(&rq->head, 1);

    RingNode* cell = &rq->array[h & (RING_SIZE-1)];

    uint64_t tt;
    int r = 0;

    while (1) {

      uint64_t cell_idx = cell->idx;
      uint64_t unsafe = node_unsafe(cell_idx);
      uint64_t idx = node_index(cell_idx);
      uint64_t val = cell->val;

      if (idx > h) break;

      if (!is_empty(val)) {
        if (idx == h) {
          if (CAS2((uint64_t*)cell, val, cell_idx, -1, unsafe | h + RING_SIZE))
            return val;
        } else {
          if (CAS2((uint64_t*)cell, val, cell_idx, val, set_unsafe(idx))) {
            break;
          }
        }
      } else {
        if ((r & ((1ull << 10) - 1)) == 0)
          tt = rq->tail;

        // Optimization: try to bail quickly if queue is closed.
        int crq_closed = crq_is_closed(tt);
        uint64_t t = tail_index(tt);

        if (unsafe) { // Nothing to do, move along
          if (CAS2((uint64_t*)cell, val, cell_idx, val, unsafe | h + RING_SIZE))
            break;
        } else if (t < h + 1 || r > 200000 || crq_closed) {
          if (CAS2((uint64_t*)cell, val, idx, val, h + RING_SIZE)) {
            if (r > 200000 && tt > RING_SIZE)
              BIT_TEST_AND_SET(&rq->tail, 63);
            break;
          }
        } else {
          ++r;
        }
      }
    }

    if (tail_index(rq->tail) <= h + 1) {
      fixState(rq);
      // try to return empty
      next = rq->next;
      if (next == NULL)
        return -1;  // EMPTY
      if (tail_index(rq->tail) <= h + 1)
        compare_and_swap(&q->head, &rq, next);
    }
  }
}

#ifdef BENCHMARK
#include <stdint.h>

static lcrq_t queue;
static int n = 10000000;

int init(int nprocs)
{
  lcrq_init(&queue);
  n /= nprocs;
  return n;
}

int test(int id)
{
  uint64_t val = id + 1;
  int i;

  for (i = 0; i < n; ++i) {
    lcrq_put(&queue, val);

    do val = lcrq_get(&queue);
    while (val == (uint64_t) -1);
  }

  return (int) val;
}

void thread_init(int id) {}
void thread_exit(int id) {}

#endif
