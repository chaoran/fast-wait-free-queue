#ifdef BENCHMARK
#include "align.h"
#include "delay.h"
#include "atomic.h"

void * init(int nprocs) { return NULL; }

void * thread_init(int nprocs, int id, void * q) {
  void ** tid = malloc(sizeof(void *));
  *tid = (void *) (size_t) (id + 1);
  return tid;
}

static volatile long P DOUBLE_CACHE_ALIGNED = 0;
static volatile long C DOUBLE_CACHE_ALIGNED = 0;

void enqueue(void * q, void * th, void * val)
{
  fetch_and_add(&P, 1);
}

void * dequeue(void * q, void * th)
{
  fetch_and_add(&C, 1);
  return *(void **) th;
}

void * EMPTY = NULL;

#endif
