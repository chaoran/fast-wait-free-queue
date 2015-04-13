#ifdef BENCHMARK
#include <stdint.h>
#include "align.h"
#include "atomic.h"

static int n = 10000000;

int init(int nprocs) {
  n /= nprocs;
  return n;
}

void thread_init(int id, void * args) {}
void thread_exit(int id, void * args) {}

int test(int id)
{
  int i;
  static int64_t P DOUBLE_CACHE_ALIGNED = 0;
  static int64_t C DOUBLE_CACHE_ALIGNED = 0;

  for (i = 0; i < n; ++i) {
    fetch_and_add(&P, 1);
    fetch_and_add(&C, 1);
  }

  return id + 1;
}

#endif
