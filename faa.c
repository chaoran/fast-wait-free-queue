#ifdef BENCHMARK

typedef void * thread_local_t;

#include "bench.h"

#define fetch_and_add(p, v) __atomic_fetch_add(p, v, __ATOMIC_RELAXED)

void init(int nprocs) {}
void thread_init(int id, void * args) {}
void thread_exit(int id, void * args) {}

void enqueue(void * val, void * args)
{
  static size_t P __attribute__((aligned(64))) = 0;
  fetch_and_add(&P, 1);

  *(void **) args = val;
}

void * dequeue(void * args)
{
  static size_t C __attribute__((aligned(64))) = 0;
  fetch_and_add(&C, 1);

  return *(void **) args;
}

#endif
