#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "align.h"
#include "atomic.h"
#include "hzdptr.h"

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
} RingNode DOUBLE_CACHE_ALIGNED;

typedef struct RingQueue {
  volatile int64_t head DOUBLE_CACHE_ALIGNED;
  volatile int64_t tail DOUBLE_CACHE_ALIGNED;
  struct RingQueue *next DOUBLE_CACHE_ALIGNED;
  RingNode array[RING_SIZE];
} RingQueue DOUBLE_CACHE_ALIGNED;

typedef struct _lcrq_t {
  RingQueue * volatile head DOUBLE_CACHE_ALIGNED;
  RingQueue * volatile tail DOUBLE_CACHE_ALIGNED;
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
    uint64_t t = rq->tail;
    uint64_t h = rq->head;

    if (rq->tail != t)
      continue;

    if (h > t) {
      if (compare_and_swap(&rq->tail, &t, h)) break;
      continue;
    }
    break;
  }
}

typedef struct _lcrq_handle_t {
  RingQueue * next;
  hzdptr_t hzdptr;
} lcrq_handle_t;

inline int close_crq(RingQueue *rq, const uint64_t t, const int tries) {
  uint64_t tt = t + 1;

  if (tries < 10)
    return compare_and_swap(&rq->tail, &tt, tt|(1ull<<63));
  else
    return atomic_btas(&rq->tail, 63);
}

static void lcrq_put(lcrq_t * q, lcrq_handle_t * handle, uint64_t arg) {
  int try_close = 0;

  while (1) {
    RingQueue *rq = hzdptr_setv(&q->tail, &handle->hzdptr, 0);
    RingQueue *next = rq->next;

    if (next != NULL) {
      compare_and_swap(&q->tail, &rq, next);
      continue;
    }

    uint64_t t = fetch_and_add(&rq->tail, 1);

    if (crq_is_closed(t)) {
      RingQueue * nrq;
alloc:
      nrq = handle->next;

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
        handle->next = NULL;
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
            atomic_dcas(cell, &val, &idx, arg, t)) {
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

static uint64_t lcrq_get(lcrq_t * q, lcrq_handle_t * handle) {
  while (1) {
    RingQueue *rq = hzdptr_setv(&q->head, &handle->hzdptr, 0);
    RingQueue *next;

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
          if (atomic_dcas(cell, &val, &cell_idx, -1, unsafe | h + RING_SIZE))
            return val;
        } else {
          if (atomic_dcas(cell, &val, &cell_idx, val, set_unsafe(idx))) {
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
          if (atomic_dcas(cell, &val, &cell_idx, val, unsafe | h + RING_SIZE))
            break;
        } else if (t < h + 1 || r > 200000 || crq_closed) {
          if (atomic_dcas(cell, &val, &idx, val, h + RING_SIZE)) {
            if (r > 200000 && tt > RING_SIZE)
              atomic_btas(&rq->tail, 63);
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
      if (tail_index(rq->tail) <= h + 1) {
        if (compare_and_swap(&q->head, &rq, next)) {
          hzdptr_retire(&handle->hzdptr, rq);
        }
      }
    }
  }
}

#ifdef BENCHMARK
#include <stdint.h>

static lcrq_t queue;
static int n = 10000000;
static lcrq_handle_t ** handles;
static int NPROCS;

int init(int nprocs)
{
  NPROCS = nprocs;
  lcrq_init(&queue);
  handles = malloc(sizeof(lcrq_handle_t * [nprocs]));
  n /= nprocs;
  return n;
}

void thread_init(int id)
{
  handles[id] = malloc(sizeof(lcrq_handle_t) + hzdptr_size(NPROCS, 1));
  hzdptr_init(&handles[id]->hzdptr, NPROCS, 1);
}

int test(int id)
{
  uint64_t val = id + 1;
  int i;

  for (i = 0; i < n; ++i) {
    lcrq_put(&queue, handles[id], val);

    do val = lcrq_get(&queue, handles[id]);
    while (val == (uint64_t) -1);
  }

  return (int) val;
}

void thread_exit(int id) {}

#endif
