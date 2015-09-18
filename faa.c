#ifdef BENCHMARK
#include "align.h"
#include "delay.h"
#include "atomic.h"

static int n = NUM_OPS;

int init(int nprocs) {
  n /= nprocs;
  return n;
}

void thread_init(int id, void * args) {}

void thread_exit(int id, void * args) {}

int test(int id)
{
  int i, j;
  static volatile long P DOUBLE_CACHE_ALIGNED = 0;
  static volatile long C DOUBLE_CACHE_ALIGNED = 0;
  delay_t state;
  delay_init(&state, id);

  for (i = 0; i < n; ++i) {
    fetch_and_add(&P, 1);
    delay_exec(&state);

    fetch_and_add(&C, 1);
    delay_exec(&state);
  }

  return id + 1;
}

#endif
