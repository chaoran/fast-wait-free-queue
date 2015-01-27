#include "test.h"
#include "bench.h"

static size_t P __attribute__((aligned(128))) = 0;
static size_t C __attribute__((aligned(128))) = 0;

#define fetch_and_add(p, v) __atomic_fetch_add(p, v, __ATOMIC_RELAXED)

void init()
{
  n /= nprocs;
}

void prep(int id) {}

void enqueue(int id)
{
  fetch_and_add(&P, 1);
}

void dequeue(int id)
{
  fetch_and_add(&C, 1);
}

int verify()
{
  return 0;
}

