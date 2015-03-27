#include <stdint.h>
#include <stdlib.h>

// Definition: RING_POW
// --------------------
// The LCRQ's ring size will be 2^{RING_POW}.
#ifndef RING_POW
#define RING_POW        (17)
#endif
#define RING_SIZE       (1ull << RING_POW)

// Definition: RING_STATS
// --------------------
// Define to collect statistics about CRQ closes and nodes
// marked unsafe.
//#define RING_STATS

// Definition: HAVE_HPTRS
// --------------------
// Define to enable hazard pointer setting for safe memory
// reclamation.  You'll need to integrate this with your
// hazard pointers implementation.
//#define HAVE_HPTRS

#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)

#define CASPTR(A, B, C) __sync_bool_compare_and_swap((long *)A, (long)B, (long)C)
#define FAA64(A, B) __sync_fetch_and_add(A, B)
#define CAS64(A, B, C) __sync_bool_compare_and_swap(A, B, C)
#define StorePrefetch(A) __builtin_prefetch((const void *)A, 1, 3);

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

#ifdef DEBUG
#define CAS2(ptr, o1, o2, n1, n2)                               \
  ({                                                              \
   int res;                                                    \
   res = __CAS2(ptr, o1, o2, n1, n2);                          \
   __executed_cas[__stats_thread_id].v++;                      \
   __failed_cas[__stats_thread_id].v += 1 - res;               \
   res;                                                        \
   })
#else
#define CAS2(ptr, o1, o2, n1, n2)    __CAS2(ptr, o1, o2, n1, n2)
#endif


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

RingQueue *head;
RingQueue *tail;

inline void init_ring(RingQueue *r) {
  int i;

  for (i = 0; i < RING_SIZE; i++) {
    r->array[i].val = -1;
    r->array[i].idx = i;
  }

  r->head = r->tail = 0;
  r->next = NULL;
}

int FULL;


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


static void SHARED_OBJECT_INIT() {
  int i;

  RingQueue *rq = malloc(sizeof(RingQueue));
  init_ring(rq);
  head = tail = rq;

  if (FULL) {
    // fill ring 
    for (i = 0; i < RING_SIZE/2; i++) {
      rq->array[i].val = 0;
      rq->array[i].idx = i;
      rq->tail++;
    }
    FULL = 0;
  }
}


inline void fixState(RingQueue *rq) {

  uint64_t t, h, n;

  while (1) {
    uint64_t t = FAA64(&rq->tail, 0);
    uint64_t h = FAA64(&rq->head, 0);

    if (unlikely(rq->tail != t))
      continue;

    if (h > t) {
      if (CAS64(&rq->tail, t, h)) break;
      continue;
    }
    break;
  }
}

__thread RingQueue *nrq;
__thread RingQueue *hazardptr;

#ifdef RING_STATS
__thread uint64_t mycloses;
__thread uint64_t myunsafes;

uint64_t closes;
uint64_t unsafes;

inline void count_closed_crq(void) {
  mycloses++;
}


inline void count_unsafe_node(void) {
  myunsafes++;
}
#else
inline void count_closed_crq(void) { }
inline void count_unsafe_node(void) { }
#endif


inline int close_crq(RingQueue *rq, const uint64_t t, const int tries) {
  if (tries < 10)
    return CAS64(&rq->tail, t + 1, (t + 1)|(1ull<<63));
  else
    return BIT_TEST_AND_SET(&rq->tail, 63);
}

static void lcrq_put(uint64_t arg) {
  int try_close = 0;

  while (1) {
    RingQueue *rq = tail;

#ifdef HAVE_HPTRS
    SWAP(&hazardptr, rq);
    if (unlikely(tail != rq))
      continue;
#endif

    RingQueue *next = rq->next;

    if (unlikely(next != NULL)) {
      CASPTR(&tail, rq, next);
      continue;
    }

    uint64_t t = FAA64(&rq->tail, 1);

    if (crq_is_closed(t)) {
alloc:
      if (nrq == NULL) {
        nrq = malloc(sizeof(RingQueue));
        init_ring(nrq);
      }

      // Solo enqueue
      nrq->tail = 1, nrq->array[0].val = (uint64_t) arg, nrq->array[0].idx = 0;

      if (CASPTR(&rq->next, NULL, nrq)) {
        CASPTR(&tail, rq, nrq);
        nrq = NULL;
        return;
      }
      continue;
    }

    RingNode* cell = &rq->array[t & (RING_SIZE-1)];
    StorePrefetch(cell);

    uint64_t idx = cell->idx;
    uint64_t val = cell->val;

    if (likely(is_empty(val))) {
      if (likely(node_index(idx) <= t)) {
        if ((likely(!node_unsafe(idx)) || rq->head < t) &&
            CAS2((uint64_t*)cell, -1, idx, arg, t)) {
          return;
        }
      }
    }

    uint64_t h = rq->head;

    if (unlikely((int64_t)(t - h) >= (int64_t)RING_SIZE) &&
        close_crq(rq, t, ++try_close)) {
      count_closed_crq();
      goto alloc;
    }
  }
}

static uint64_t lcrq_get() {
  while (1) {
    RingQueue *rq = head;
    RingQueue *next;

#ifdef HAVE_HPTRS
    SWAP(&hazardptr, rq);
    if (unlikely(head != rq))
      continue;
#endif

    uint64_t h = FAA64(&rq->head, 1);

    RingNode* cell = &rq->array[h & (RING_SIZE-1)];
    StorePrefetch(cell);

    uint64_t tt;
    int r = 0;

    while (1) {

      uint64_t cell_idx = cell->idx;
      uint64_t unsafe = node_unsafe(cell_idx);
      uint64_t idx = node_index(cell_idx);
      uint64_t val = cell->val;

      if (unlikely(idx > h)) break;

      if (likely(!is_empty(val))) {
        if (likely(idx == h)) {
          if (CAS2((uint64_t*)cell, val, cell_idx, -1, unsafe | h + RING_SIZE))
            return val;
        } else {
          if (CAS2((uint64_t*)cell, val, cell_idx, val, set_unsafe(idx))) {
            count_unsafe_node();
            break;
          }
        }
      } else {
        if ((r & ((1ull << 10) - 1)) == 0)
          tt = rq->tail;

        // Optimization: try to bail quickly if queue is closed.
        int crq_closed = crq_is_closed(tt);
        uint64_t t = tail_index(tt);

        if (unlikely(unsafe)) { // Nothing to do, move along
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
        CASPTR(&head, rq, next);
    }
  }
}

#ifdef BENCHMARK
#include <stdint.h>

static int n = 10000000;

int init(int nprocs)
{
  SHARED_OBJECT_INIT();
  n /= nprocs;
  return n;
}

int test(int id)
{
  uint64_t val = id + 1;
  int i;

  for (i = 0; i < n; ++i) {
    lcrq_put(val);
    while ((val = lcrq_get()) == (uint64_t) -1);
  }

  return (int) val;
}

void thread_init(int id) {}
void thread_exit(int id) {}

#endif
