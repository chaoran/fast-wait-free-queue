#ifdef BENCHMARK
#include "rand.h"
#include "align.h"
#include "atomic.h"

static int n = 10000000;

int init(int nprocs) {
  n /= nprocs;
  return n;
}

void thread_init(int id, void * args)
{
  simSRandom(id + 1);
}

void thread_exit(int id, void * args) {}

int test(int id)
{
  int i, j;
  static volatile long P DOUBLE_CACHE_ALIGNED = 0;
  static volatile long C DOUBLE_CACHE_ALIGNED = 0;

  for (i = 0; i < n; ++i) {
    mfence();
    fetch_and_add(&P, 1);
    for (j = 0; j < simRandomRange(1, 64); ++j);

    mfence();
    fetch_and_add(&C, 1);
    for (j = 0; j < simRandomRange(1, 64); ++j);
  }

  return id + 1;
}

#endif
